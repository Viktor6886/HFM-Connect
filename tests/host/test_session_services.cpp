// Session/service tests against SimEcu (software model, synthetic data).
// These are SOFTWARE tests: they prove internal consistency with the
// reconstructed protocol, not behaviour of a real ECU.
#include "HfmDecode.h"
#include "HfmDtc.h"
#include "HfmLive.h"
#include "HfmServices.h"
#include "HfmSession.h"
#include "ImpulseDecoder.h"
#include "JsonWriter.h"
#include "ServiceGuard.h"
#include "SimEcu.h"
#include "minitest.h"

struct Rig {
  SimEcu sim;
  TraceLog trace;
  HfmSession s{sim, trace};
};

TEST(connect_break_sync_and_timing) {
  Rig r;
  CHECK(r.s.connect() == LinkStatus::Ok);
  CHECK(r.s.ready());
  CHECK_EQ(r.s.lastSyncEcho(), 0xAA);
  CHECK_EQ(r.s.lastSyncByte(), 0x55);
  CHECK(r.sim.nowMs() >= 177 + 214);
  TraceEvent ev[4];
  CHECK_EQ(r.trace.copySince(0, ev, 4), 2);
  CHECK_EQ(ev[0].phase, (uint8_t)TracePhase::Break);
  CHECK_EQ(ev[1].phase, (uint8_t)TracePhase::Sync);
}

TEST(no_sync_retries_three_times_then_error) {
  Rig r;
  SimEcu::Inject inj;
  inj.noSync = true;
  r.sim.setInject(inj);
  CHECK(r.s.connect() == LinkStatus::NoSync);
  CHECK(r.s.state() == SessionState::Error);
  TraceEvent ev[16];
  uint16_t n = r.trace.copySince(0, ev, 16);
  CHECK_EQ(n, 6);  // 3 x (BREAK + SYNC)
  CHECK(r.sim.nowMs() >= 2 * 1000);  // two retry pauses
}

TEST(reconnect_keeps_line_silent_until_ecu_left_session) {
  Rig r;
  SimEcu::Inject inj;
  inj.sessionTimeoutMs = 2000;
  r.sim.setInject(inj);
  CHECK(r.s.connect() == LinkStatus::Ok);
  CHECK_EQ(r.s.silenceNeededMs(), (uint32_t)r.s.timing().resyncSilenceMs);  // just synchronised
  uint8_t d;
  CHECK(r.s.prefix(0xF0) == LinkStatus::Ok);
  r.s.disconnect();
  r.s.link().open();
  // Immediate wake-up without silence: the ECU is still in session -> no 55.
  LinkTiming t = r.s.timing();
  t.resyncSilenceMs = 0;
  t.syncRetries = 1;
  r.s.setTiming(t);
  CHECK(r.s.connect() == LinkStatus::NoSync);
  CHECK_EQ(r.s.wakeFailStreak(), 1);
  // Default silence (2.5 s + 1 s per failure) lets the session time out.
  r.s.setTiming(LinkTiming());
  uint32_t t0 = r.sim.nowMs();
  CHECK(r.s.connect(1) == LinkStatus::Ok);
  CHECK(r.sim.nowMs() - t0 >= 3500);
  CHECK_EQ(r.s.wakeFailStreak(), 0);
  CHECK(r.s.selector(0x00, d) == LinkStatus::NotAllowed);  // no page after a new wake-up
}

TEST(resync_resumes_read_session_without_break) {
  Rig r;
  SimEcu::Inject inj;
  inj.sessionTimeoutMs = 2000;
  r.sim.setInject(inj);
  CHECK(r.s.connect() == LinkStatus::Ok);
  inj.timeoutAt = (int32_t)r.sim.exchanges() + 1;
  r.sim.setInject(inj);
  uint8_t id[28];
  CHECK(r.s.readIdent(id, 28) == LinkStatus::Timeout);
  CHECK(r.s.state() == SessionState::Error);
  CHECK(r.s.resyncAllowed());
  CHECK(r.s.resync() == LinkStatus::Ok);
  CHECK(r.s.ready());
  CHECK_EQ(r.s.resyncCount(), 1u);
  CHECK(r.s.readIdent(id, 28) == LinkStatus::Ok);
  CHECK_EQ(id[0], 0x2C);
  TraceEvent ev[200];
  uint16_t n = r.trace.copySince(0, ev, 200);
  uint16_t breaks = 0;
  for (uint16_t i = 0; i < n; i++)
    if (ev[i].phase == (uint8_t)TracePhase::Break) breaks++;
  CHECK_EQ(breaks, 1);  // resumed without a new BREAK
}

TEST(resync_fails_when_ecu_left_session_and_never_after_write) {
  Rig r;
  SimEcu::Inject inj;
  inj.sessionTimeoutMs = 500;
  r.sim.setInject(inj);
  CHECK(r.s.connect() == LinkStatus::Ok);
  inj.timeoutAt = (int32_t)r.sim.exchanges();
  r.sim.setInject(inj);
  CHECK(r.s.prefix(0xF0) == LinkStatus::Timeout);
  r.sim.sleepMs(1000);
  CHECK(r.s.resync() == LinkStatus::Timeout);
  CHECK(r.s.state() == SessionState::Error);
  // After a write page the session is never resumed.
  Rig w;
  CHECK(w.s.connect() == LinkStatus::Ok);
  w.s.armWrite(1);
  CHECK(w.s.prefix(0xCF) == LinkStatus::Ok);
  w.s.disarmWrite();
  SimEcu::Inject wi;
  wi.timeoutAt = (int32_t)w.sim.exchanges();
  w.sim.setInject(wi);
  CHECK(w.s.prefix(0xF0) == LinkStatus::Timeout);
  CHECK(!w.s.resyncAllowed());
  CHECK(w.s.resync() == LinkStatus::NotAllowed);
}

TEST(ident_read_28_bytes_and_trace) {
  Rig r;
  r.s.connect();
  uint8_t id[28];
  CHECK(r.s.readIdent(id, 28) == LinkStatus::Ok);
  CHECK_EQ(id[0], 0x2C);
  CHECK_EQ(id[26], 0x41);
  CHECK_EQ(r.s.txCount(), 56u);  // 28 x (F0 prefix + index)
  TraceEvent ev[8];
  uint16_t n = r.trace.copySince(2, ev, 2);
  CHECK_EQ(n, 2);
  CHECK_EQ(ev[0].tx, 0xF0);
  CHECK_EQ(ev[0].resp, 0x0F);
  CHECK_EQ(ev[0].parity, (uint8_t)Parity::None);
  CHECK_EQ(ev[1].parity, (uint8_t)Parity::Space);
}

TEST(echo_mismatch_drops_session_without_retry) {
  Rig r;
  r.s.connect();
  SimEcu::Inject inj;
  inj.echoMismatchAt = 3;
  r.sim.setInject(inj);
  uint8_t id[28];
  CHECK(r.s.readIdent(id, 28) == LinkStatus::EchoMismatch);
  CHECK(r.s.state() == SessionState::Error);
  CHECK_EQ(r.s.txCount(), 4u);  // stopped at the failing byte
  uint8_t d;
  CHECK(r.s.selector(0x00, d) == LinkStatus::NotConnected);
}

TEST(bad_ack_and_timeout_detected) {
  Rig r;
  r.s.connect();
  SimEcu::Inject inj;
  inj.badAckAt = 0;
  r.sim.setInject(inj);
  CHECK(r.s.prefix(0x20) == LinkStatus::BadAck);
  Rig t;
  t.s.connect();
  SimEcu::Inject inj2;
  inj2.timeoutAt = 1;
  t.sim.setInject(inj2);
  uint8_t out[6];
  const uint8_t sel[1] = {0xB7};
  CHECK(t.s.readGroup(0x20, sel, 1, out) == LinkStatus::Timeout);
  TraceEvent ev[8];
  uint16_t n = t.trace.copySince(0, ev, 8);
  CHECK_EQ(ev[n - 1].rxCount, 1);  // echo received, response missing -> partial
}

TEST(command_policy_blocks_unknown_and_write_prefixes) {
  Rig r;
  r.s.connect();
  CHECK(r.s.prefix(0x55) == LinkStatus::NotAllowed);   // unknown opcode never sent
  CHECK(r.s.prefix(0xF3) == LinkStatus::NotAllowed);   // erase without token
  CHECK(r.s.prefix(0xF9) == LinkStatus::NotAllowed);   // VIN experimental, gated
  CHECK_EQ(r.s.txCount(), 0u);
  CHECK(r.s.ready());  // policy rejection does not tear down the session
  uint8_t d;
  CHECK(r.s.selector(0x00, d) == LinkStatus::NotAllowed);  // no page context
  r.s.setExperimentalReads(true);
  CHECK(r.s.prefix(0xF9) == LinkStatus::Ok);
  CHECK(r.s.argument(0x00, d) == LinkStatus::NotAllowed);  // argument only on write page
}

TEST(fault_memory_and_live_snapshot_on_sim) {
  Rig r;
  r.sim.setScenario(SimEcu::Scenario::Faults);
  r.s.connect();
  svc::FaultMemory fm;
  CHECK(svc::readFaultMemory(r.s, fm, false) == LinkStatus::Ok);
  CHECK_EQ(fm.bankA[0], 0x40);
  CHECK_EQ(fm.bankA[2], 0x08);
  CHECK_EQ(fm.bankB[0], 0x40);
  live::Snapshot snap;
  CHECK(live::pollNative(r.s, snap) == LinkStatus::Ok);
  for (int g = 0; g < live::G_COUNT; g++) CHECK(snap.groupOk[g]);
  live::Decoded d;
  live::decode(snap, prof::NormGroup::M104_CODED, false, nullptr, d);
  CHECK(d.v[live::P_RPM].hasValue);
  CHECK(d.v[live::P_RPM].v > 600 && d.v[live::P_RPM].v < 900);
  CHECK(d.v[live::P_ECT].v > 80);
  CHECK(d.conditionsWarmIdle);
  CHECK(d.v[live::P_RPM].norm == live::NormState::InSpec);
  CHECK(d.v[live::P_MAF].hasValue);
  CHECK(d.v[live::P_INJ].hasValue);
  CHECK(d.v[live::P_SPEED].q == live::Quality::NotRead);
}

TEST(live_engine_off_gates_lambda_and_injection) {
  Rig r;
  r.sim.setScenario(SimEcu::Scenario::EngineOff);
  r.s.connect();
  live::Snapshot snap;
  live::pollNative(r.s, snap);
  live::Decoded d;
  live::decode(snap, prof::NormGroup::M104_CODED, false, nullptr, d);
  CHECK_EQ((int)d.v[live::P_RPM].v, 0);
  CHECK(d.v[live::P_INJ].q == live::Quality::NoData);
  CHECK(d.v[live::P_LAMBDA].q == live::Quality::NoData);
  CHECK(d.v[live::P_RPM].norm == live::NormState::NotApplicable);
}

TEST(partial_cycle_invalidates_maf_pair) {
  Rig r;
  r.s.connect();
  SimEcu::Inject inj;
  inj.timeoutAt = 27;  // last of 28 exchanges: air_mass_high selector 49
  r.sim.setInject(inj);
  live::Snapshot snap;
  CHECK(live::pollNative(r.s, snap) != LinkStatus::Ok);
  CHECK(!snap.groupOk[live::G_MAF_LO]);
  CHECK(!snap.groupOk[live::G_MAF_HI]);
}

TEST(adaptations_and_coding_read) {
  Rig r;
  r.s.connect();
  svc::Adaptations a;
  CHECK(svc::readAdaptations(r.s, a) == LinkStatus::Ok);
  CHECK_EQ(a.rawIdle, 131);
  CHECK_NEAR(a.idleKgH, hfm::adaptIdleKgH(131), 1e-6);
  svc::Coding c;
  CHECK(svc::readCoding(r.s, c) == LinkStatus::Ok);
  CHECK_EQ(c.info.automatic, 1);
}

static guard::Facts readyFacts() {
  guard::Facts f;
  f.sessionReady = true;
  f.interlockClosed = true;
  f.engineStopped = 1;
  f.rpm = 0;
  f.ect = 85;
  f.liveAgeMs = 500;
  f.batteryV = 12.4f;
  f.faultMemAgeMs = 1000;
  f.faultMemNonZero = true;
  f.adaptAgeMs = 1000;
  return f;
}

static void refProfile(const EcuIdent& id, prof::EcuProfile& p, bool engineer) {
  prof::ResolveOptions o;
  o.engineerMode = engineer;
  prof::resolve(id, o, p);
}

TEST(guard_full_dtc_clear_flow_on_sim) {
  Rig r;
  r.sim.setScenario(SimEcu::Scenario::Faults);
  r.s.connect();
  uint8_t raw[28];
  r.s.readIdent(raw, 28);
  EcuIdent id;
  parseIdent(raw, 28, id);
  prof::EcuProfile p;
  refProfile(id, p, true);
  guard::ServiceGuard g;
  guard::Plan plan;
  guard::Facts f = readyFacts();
  CHECK(g.prepare(guard::Op::DtcClear, p, id, f, 1000, 0xCAFE, plan));
  CHECK_STR(plan.phrase, "CLEAR DTC 0135451332");
  CHECK(g.authorize(guard::Op::DtcClear, 0xBEEF, plan.phrase, p, id, f, 1500) == guard::AuthResult::BadToken);
  CHECK(g.authorize(guard::Op::DtcClear, 0xCAFE, "clear", p, id, f, 1500) == guard::AuthResult::BadPhrase);
  CHECK(g.authorize(guard::Op::DtcClear, 0xCAFE, plan.phrase, p, id, f, 1500) == guard::AuthResult::Ok);
  CHECK(g.authorize(guard::Op::DtcClear, 0xCAFE, plan.phrase, p, id, f, 1600) != guard::AuthResult::Ok);  // one-time token
  g.begin(guard::Op::DtcClear);
  r.s.armWrite(1);
  bool verified = false;
  svc::FaultMemory after;
  CHECK(svc::dtcClear(r.s, verified, after) == LinkStatus::Ok);
  CHECK(verified);
  CHECK(!r.s.writeArmed());
  g.finish(verified);
  CHECK(g.state() == guard::State::Idle);
}

TEST(guard_blocks_without_interlock_engineer_or_fresh_data) {
  Rig r;
  r.s.connect();
  uint8_t raw[28];
  r.s.readIdent(raw, 28);
  EcuIdent id;
  parseIdent(raw, 28, id);
  prof::EcuProfile p;
  guard::ServiceGuard g;
  guard::Plan plan;
  guard::Facts f = readyFacts();
  refProfile(id, p, false);
  CHECK(!g.prepare(guard::Op::DtcClear, p, id, f, 0, 1, plan));  // engineer mode off
  refProfile(id, p, true);
  f.interlockClosed = false;
  CHECK(!g.prepare(guard::Op::DtcClear, p, id, f, 0, 1, plan));
  f = readyFacts();
  f.batteryV = -1;  // no ADC and no manual confirmation -> unknown -> blocked
  CHECK(!g.prepare(guard::Op::AdaptReset, p, id, f, 0, 1, plan));
  f.batteryManualConfirm = true;
  f.liveAgeMs = 10000;  // stale rpm -> unknown -> blocked
  CHECK(!g.prepare(guard::Op::AdaptReset, p, id, f, 0, 1, plan));
  f = readyFacts();
  CHECK(g.prepare(guard::Op::AdaptReset, p, id, f, 0, 7, plan));
  CHECK(g.authorize(guard::Op::AdaptReset, 7, plan.phrase, p, id, f, 25000) == guard::AuthResult::Expired);
}

TEST(guard_fault_stop_requires_acknowledge) {
  guard::ServiceGuard g;
  g.begin(guard::Op::AdaptReset);
  g.finish(false);
  CHECK(g.locked());
  EcuIdent id;
  prof::EcuProfile p;
  prof::resolve(id, prof::ResolveOptions(), p);
  guard::Plan plan;
  CHECK(!g.prepare(guard::Op::DtcClear, p, id, readyFacts(), 0, 1, plan));
  CHECK(g.authorize(guard::Op::DtcClear, 1, "x", p, id, readyFacts(), 0) == guard::AuthResult::Locked);
  g.acknowledgeFault();
  CHECK(!g.locked());
}

TEST(adaptation_reset_and_purge_recipes_on_sim) {
  Rig r;
  r.s.connect();
  r.s.armWrite(1);
  uint8_t resp[10];
  CHECK(svc::adaptationReset(r.s, resp) == LinkStatus::Ok);
  CHECK_EQ(resp[1], 0x00);
  CHECK_EQ(resp[3], 0x03);
  CHECK(r.sim.adaptationsReset());
  CHECK(!r.s.writeArmed());
  r.s.armWrite(2);
  bool sonde = false;
  CHECK(svc::purgeEnter(r.s, sonde) == LinkStatus::Ok);
  CHECK(sonde);
  CHECK(svc::purgeSet(r.s, true) == LinkStatus::Ok);
  CHECK(r.sim.purgeOn());
  CHECK(svc::purgeRestore(r.s, sonde) == LinkStatus::Ok);
  CHECK(!r.sim.purgeOn());
  CHECK(!r.s.writeArmed());
}

TEST(trace_jsonl_line_format) {
  TraceEvent e{5, 1234, (uint8_t)TracePhase::Prefix, (uint8_t)Parity::None, 0xF0, 0xF0, 0x0F,
               (uint8_t)LinkStatus::Ok, (uint8_t)CommandRisk::Read, 2};
  std::string s;
  TraceLog::writeJsonl(s, e, "S1", "sim", "BOSCH_0135451332_41");
  CHECK(s.find("\"tx_hex\":\"F0\"") != std::string::npos);
  CHECK(s.find("\"rx_hex\":\"F0 0F\"") != std::string::npos);
  CHECK(s.find("\"echo_ok\":true") != std::string::npos);
  CHECK(s.find("\"ack_ok\":true") != std::string::npos);
  CHECK(s.find("\"partial\":false") != std::string::npos);
}

TEST(impulse_decoder_counts_pulses) {
  ImpulseConfig cfg;
  ImpulseEdge e[] = {{1, 800}, {0, 500}, {1, 500}, {0, 500}, {1, 500}, {0, 20}, {1, 10}, {0, 500}, {1, 3000}};
  ImpulseResult r = decodeImpulses(e, 9, cfg);
  CHECK(r.complete);
  CHECK_EQ(r.pulses, 3);
  CHECK_EQ(r.glitches, 1);
  ImpulseEdge e2[] = {{0, 500}, {1, 500}};
  CHECK(!decodeImpulses(e2, 2, cfg).complete);
}

TEST(impulse_factory_table_lookup) {
  const ImpulseCodeEntry* e[2];
  CHECK_EQ(impulseLookup(2, IMP_M104, e, 2), 1);
  CHECK_EQ(e[0]->count, 4);
  CHECK_EQ(e[0]->a[0], 2);
  CHECK_EQ(impulseLookup(18, IMP_M111, e, 2), 0);   // injectors 5/6: M104 only
  CHECK_EQ(impulseLookup(18, IMP_M104, e, 2), 1);
  CHECK_EQ(impulseLookup(42, IMP_M111, e, 2), 1);
  CHECK_EQ(e[0]->count, 1);
  CHECK_EQ(e[0]->a[0], 100);
  CHECK_EQ(impulseLookup(42, IMP_BOTH, e, 2), 2);
  uint16_t n = 0;
  const ImpulseCodeEntry* t = impulseTable(&n);
  for (uint16_t i = 0; i < n; i++)
    for (uint8_t c = 0; c < t[i].count; c++) CHECK(dtc::lookup(t[i].a[c]) != nullptr);
}

TEST(live_status_masks_bosch_and_vdo) {
  // Bosch masks equal the bit_v1_old table used by hfm::statusMask.
  for (uint8_t q = 0; q < 12; q++) CHECK_EQ(live::statusMaskBosch(q), hfm::statusMask(q));
  CHECK_EQ(live::statusMaskBosch(0), 0x80);  // idle contact
  CHECK_EQ(live::statusMaskVdo(0), 0x40);
  CHECK_EQ(live::statusMaskVdo(1), 0x80);    // starter
  // VDO bits sharing one byte (22:7A) never collide.
  uint8_t q22[] = {0, 9, 6, 4, 10};
  uint8_t seen = 0;
  for (uint8_t q : q22) {
    CHECK((seen & live::statusMaskVdo(q)) == 0);
    seen |= live::statusMaskVdo(q);
  }
}

TEST(live_o2_primary_is_factory_formula) {
  live::Snapshot s;
  s.groupOk[live::G_MIXTURE] = true;
  s.g[live::G_MIXTURE][0] = 0x80;
  live::Decoded d;
  live::decode(s, prof::NormGroup::None, false, nullptr, d);
  CHECK_EQ((int)d.v[live::P_O2].v, 461);      // trunc0(51102*128 - 1928300, 10000)
  CHECK_EQ((int)d.v[live::P_O2_ALT].v, 512);  // 4 x raw
  s.g[live::G_MIXTURE][0] = 0x00;
  live::decode(s, prof::NormGroup::None, false, nullptr, d);
  CHECK_EQ((int)d.v[live::P_O2].v, -192);     // negative region of the factory norm
}

TEST(live_idle_target_source_by_diag_byte) {
  live::Snapshot s;
  s.groupOk[live::G_AIRFLOW] = s.groupOk[live::G_COOLANT] = true;
  s.g[live::G_AIRFLOW][2] = 28;   // rpm byte (700 rpm)
  s.g[live::G_COOLANT][0] = 30;   // 21:D7 -> 750
  s.extRead = true;
  s.extOk[live::X_IDLE_TARGET] = true;
  s.ext[live::X_IDLE_TARGET] = 32;  // 20:67 -> 800
  live::Decoded d;
  live::decode(s, prof::NormGroup::None, false, nullptr, d, 0x40);
  CHECK_EQ((int)d.v[live::P_IDLE_TARGET].v, 750);
  live::decode(s, prof::NormGroup::None, false, nullptr, d, 0x41);
  CHECK_EQ((int)d.v[live::P_IDLE_TARGET].v, 800);
  s.g[live::G_AIRFLOW][2] = 0;    // engine stopped
  live::decode(s, prof::NormGroup::None, false, nullptr, d, 0x41);
  CHECK_EQ((int)d.v[live::P_IDLE_TARGET].v, 0);
}

TEST(live_vdo_plan_decode) {
  live::Snapshot s;
  s.vdoPlan = true;
  for (int g = 0; g < live::V_COUNT; g++) s.vdoOk[g] = true;
  s.vdo[live::V_P12][1] = 30;     // 12:7E rpm byte -> 750
  s.vdo[live::V_P22][4] = 0x40;   // 22:7A idle contact (VDO mask)
  s.vdo[live::V_P23][0] = 0x80;   // 23:A4 starter
  s.vdo[live::V_O2][0] = 0x80;    // 20:F2
  live::Decoded d;
  live::decode(s, prof::NormGroup::None, true, nullptr, d);
  CHECK_EQ((int)d.v[live::P_RPM].v, 750);
  CHECK_EQ((int)d.v[live::P_IDLE_CONTACT].v, 1);
  CHECK_EQ((int)d.v[live::P_FULL_LOAD].v, 0);
  CHECK_EQ((int)d.v[live::P_STARTER].v, 1);
  CHECK_EQ((int)d.v[live::P_O2].v, 461);
  CHECK(d.v[live::P_IGN].q == live::Quality::NotRead);  // VDO ignition formula unknown
  CHECK(d.v[live::P_SPEED].hasValue);
}

TEST(vdo_prefixes_need_experimental_optin) {
  CHECK(classifyPrefix(0x12) == PrefixClass::ExperimentalRead);
  CHECK(classifyPrefix(0x22) == PrefixClass::ExperimentalRead);
  CHECK(classifyPrefix(0x23) == PrefixClass::ExperimentalRead);
  Rig r;
  r.s.connect();
  CHECK(r.s.prefix(0x22) == LinkStatus::NotAllowed);
}

TEST(json_drops_utf8_cut_by_buffer) {
  // A Russian message truncated by a fixed-size buffer must not end with a
  // broken UTF-8 byte (the browser showed it as "?").
  char msg[12];
  snprintf(msg, sizeof msg, "%s", "\xD0\x9F\xD0\xB8\xD1\x82\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5");  // "Питание"
  JsonWriter w;
  w.beginObject().kv("m", msg).endObject();
  CHECK(w.str() == "{\"m\":\"\xD0\x9F\xD0\xB8\xD1\x82\xD0\xB0\xD0\xBD\"}");
  JsonWriter w2;
  w2.beginObject().kv("m", "a\xB8\xD1\x8F\xF0\x9F\x98\x80z").endObject();
  CHECK(w2.str() == "{\"m\":\"a\xD1\x8F\xF0\x9F\x98\x80z\"}");
}
