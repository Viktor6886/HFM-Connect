// 0.9.1: ECUs that keep no page context (D-037), byte-wise echo detection,
// norms by engine mode, DTC clear preconditions. Software tests with the ECU
// model; the "page forgotten" behaviour is modelled on the owner's
// observations of VDO 013 545 08 32 (first selector after a prefix gave data,
// the following ones their own addresses) and on the factory tester templates
// of vendor V ("22 01 72 00 22 01 73 00" - a prefix before every selector).
#include <string.h>

#include "HfmIdent.h"
#include "HfmLive.h"
#include "HfmServices.h"
#include "ServiceGuard.h"
#include "SimEcu.h"
#include "TraceLog.h"
#include "minitest.h"

namespace {
struct PRig {
  SimEcu sim;
  TraceLog trace;
  HfmSession s{sim, trace};
};

// Owner's ECU as far as known: engine stopped, ignition on.
void vdoEngineOff(SimEcu& sim) {
  sim.setPageByte(0x12, 0x76, 200);   // ECT byte
  sim.setPageByte(0x12, 0x7E, 0x00);  // rpm 0
  sim.setPageByte(0x12, 0x77, 160);   // IAT byte
  sim.setPageByte(0x12, 0x7A, 0x00);
  sim.setPageByte(0x12, 0x7B, 0x00);
  sim.setPageByte(0x12, 0x7F, 0x00);  // speed 0
  sim.setPageByte(0x22, 0x78, 0x05);  // throttle
  sim.setPageByte(0x22, 0x72, 0x00);  // MAF lo
  sim.setPageByte(0x22, 0x73, 0x00);  // MAF hi
  sim.setPageByte(0x22, 0x7F, 0x80);  // lambda control
  sim.setPageByte(0x22, 0x7A, 0x40);  // idle contact (VDO mask 0x40)
  sim.setPageByte(0x22, 0x7B, 0x00);
  sim.setPageByte(0x22, 0x74, 0x00);  // injection lo
  sim.setPageByte(0x22, 0x75, 0x00);  // injection hi
  sim.setPageByte(0x23, 0xA4, 0x00);
  sim.setPageByte(0x23, 0x7F, 0x00);
  sim.setPageByte(0x23, 0x7E, 0x00);
  sim.setPageByte(0x20, 0xF2, 0x60);  // O2
}
}  // namespace

TEST(page_forgotten_group_read_is_detected) {
  const uint8_t sel[6] = {0x76, 0x7E, 0x77, 0x7A, 0x7B, 0x7F};
  const uint8_t forgot[6] = {0xC8, 0x7E, 0x77, 0x7A, 0x7B, 0x7F};  // 3150 rpm = 25 x 0x7E
  const uint8_t real[6] = {0xC8, 0x00, 0xA0, 0x00, 0x00, 0x00};
  CHECK(live::pageNotKept(sel, forgot, 6));
  CHECK(!live::pageNotKept(sel, real, 6));
  CHECK(!live::pageNotKept(sel, sel, 6));        // all addresses: whole group echo (D-034)
  CHECK(!live::pageNotKept(sel, forgot, 2));     // two bytes cannot tell
  CHECK_EQ(live::echoMask(sel, forgot, 6), 0x3E);
  const uint8_t one[6] = {0xC8, 0x7E, 0xA0, 0x00, 0x00, 0x00};  // one byte may coincide
  CHECK_EQ(live::echoMask(sel, one, 6), 0);
}

TEST(vdo_plan_reads_every_selector_after_its_prefix) {
  PRig r;
  r.sim.setForgetPage(true);
  vdoEngineOff(r.sim);
  r.s.setVdoReads(true);  // ECU identified as VDO: 12/22/23 are reads (D-037)
  CHECK(r.s.connect() == LinkStatus::Ok);
  // The old group read on this ECU: data only for the first selector.
  const live::GroupDef& g12 = live::vdoGroup(live::V_P12);
  uint8_t out[6];
  CHECK(r.s.readGroup(g12.prefix, g12.sel, g12.n, out) == LinkStatus::Ok);
  CHECK_EQ(out[1], 0x7E);  // the 3150 rpm seen by the owner
  CHECK(live::pageNotKept(g12.sel, out, g12.n));
  // The VDO plan as the factory tester reads it: real values.
  live::Snapshot snap;
  CHECK(live::pollNative(r.s, snap, true) == LinkStatus::Ok);
  CHECK(!snap.planEcho);
  CHECK_EQ(snap.vdo[live::V_P12][1], 0x00);
  CHECK_EQ(snap.vdo[live::V_P22][1], 0x00);
  CHECK_EQ(snap.vdoEchoMask[live::V_P12], 0);
  live::Decoded d;
  live::decode(snap, prof::NormGroup::HFM_GENERIC, true, nullptr, d);
  CHECK(d.v[live::P_RPM].hasValue);
  CHECK_NEAR(d.v[live::P_RPM].v, 0, 1e-6);
  CHECK(d.mode == live::EngineMode::Off);
  CHECK(d.v[live::P_SPEED].hasValue);
  CHECK_NEAR(d.v[live::P_SPEED].v, 0, 1e-6);
  CHECK(d.v[live::P_MAF].hasValue);
  CHECK_NEAR(d.v[live::P_MAF].v, 0, 1e-6);
  CHECK(d.v[live::P_IAT].hasValue);
  CHECK(d.v[live::P_IDLE_CONTACT].hasValue && d.v[live::P_IDLE_CONTACT].v == 1.0f);
  // Engine stopped: no injection, no lambda control value (n = 0).
  CHECK(!d.v[live::P_INJ].hasValue);
  CHECK(!d.v[live::P_LAMBDA].hasValue);
}

TEST(vdo_pages_need_vdo_identification_or_optin) {
  PRig r;
  CHECK(r.s.connect() == LinkStatus::Ok);
  uint8_t b;
  CHECK(r.s.readEach(0x12, (const uint8_t*)"\x76", 1, &b) == LinkStatus::NotAllowed);
  r.s.resync();
  r.s.setVdoReads(true);
  CHECK(r.s.ready());
  CHECK(r.s.readEach(0x12, (const uint8_t*)"\x76", 1, &b) == LinkStatus::Ok);
}

TEST(bosch_plan_switches_to_prefix_per_selector) {
  // Reference: the same ECU model that keeps the page.
  PRig a;
  CHECK(a.s.connect() == LinkStatus::Ok);
  live::Snapshot sa;
  CHECK(live::pollNative(a.s, sa, false) == LinkStatus::Ok);
  CHECK(!sa.perSelector);
  live::Decoded da;
  live::decode(sa, prof::NormGroup::HFM_GENERIC, false, nullptr, da);
  // An ECU that forgets the page after the first selector.
  PRig b;
  b.sim.setForgetPage(true);
  CHECK(b.s.connect() == LinkStatus::Ok);
  live::Snapshot sb;
  CHECK(live::pollNative(b.s, sb, false) == LinkStatus::Ok);
  CHECK(sb.perSelector);  // detected and kept for the next cycles
  live::Decoded db;
  live::decode(sb, prof::NormGroup::HFM_GENERIC, false, nullptr, db);
  CHECK(db.v[live::P_RPM].hasValue);
  CHECK_NEAR(db.v[live::P_RPM].v, da.v[live::P_RPM].v, 1e-6);
  CHECK(db.v[live::P_ECT].hasValue);
  CHECK_NEAR(db.v[live::P_ECT].v, da.v[live::P_ECT].v, 1e-6);
  CHECK(db.v[live::P_MAF].hasValue);
  CHECK_NEAR(db.v[live::P_MAF].v, da.v[live::P_MAF].v, 1e-6);
  // Next cycle reads per selector from the start.
  uint32_t before = b.sim.exchanges();
  CHECK(live::pollNative(b.s, sb, false) == LinkStatus::Ok);
  CHECK_EQ(b.sim.exchanges() - before, 42u);  // 21 selectors, each with its prefix
}

TEST(echo_bytes_inside_a_real_group_are_not_values) {
  live::Snapshot s;
  s.vdoPlan = true;
  for (uint8_t g = 0; g < live::V_COUNT; g++) s.vdoOk[g] = true;
  const uint8_t a[6] = {200, 0x7E, 160, 0x00, 0x00, 0x7F};  // rpm and speed repeated their addresses
  memcpy(s.vdo[live::V_P12], a, 6);
  s.vdoEchoMask[live::V_P12] = live::echoMask(live::vdoGroup(live::V_P12).sel, a, 6);
  live::Decoded d;
  live::decode(s, prof::NormGroup::HFM_GENERIC, true, nullptr, d);
  CHECK(d.v[live::P_ECT].hasValue);
  CHECK(!d.v[live::P_RPM].hasValue);
  CHECK(d.v[live::P_RPM].q == live::Quality::NotSupported);
  CHECK(!d.v[live::P_SPEED].hasValue);  // not 152 km/h
  CHECK(d.v[live::P_IAT].hasValue);
  CHECK(d.mode == live::EngineMode::Unknown);
}

TEST(norms_follow_the_engine_mode) {
  PRig r;
  r.sim.setScenario(SimEcu::Scenario::WarmIdle);
  CHECK(r.s.connect() == LinkStatus::Ok);
  live::Snapshot snap;
  CHECK(live::pollNative(r.s, snap, false) == LinkStatus::Ok);
  live::Decoded d;
  live::decode(snap, prof::NormGroup::HFM_GENERIC, false, nullptr, d);
  CHECK(d.mode == live::EngineMode::Idle);
  CHECK(d.warm);
  CHECK(d.why[live::P_RPM] == live::NormWhy::Applied);
  CHECK(d.v[live::P_RPM].norm != live::NormState::NotApplicable);
  // 3150 rpm: the idle norm 600..850 is not compared.
  snap.g[live::G_AIRFLOW][2] = 126;
  live::decode(snap, prof::NormGroup::HFM_GENERIC, false, nullptr, d);
  CHECK(d.mode == live::EngineMode::Raised);
  CHECK(d.why[live::P_RPM] == live::NormWhy::NotIdle);
  CHECK(d.v[live::P_RPM].norm == live::NormState::NotApplicable);
  CHECK(d.v[live::P_RPM].normRef != nullptr);  // the norm is known, only not for this mode
  // Cold engine at idle: no warm-idle norm either.
  snap.g[live::G_AIRFLOW][2] = 30;     // 750 rpm
  snap.g[live::G_COOLANT][1] = 100;    // cold
  live::decode(snap, prof::NormGroup::HFM_GENERIC, false, nullptr, d);
  CHECK(d.mode == live::EngineMode::Idle);
  CHECK(!d.warm);
  CHECK(d.why[live::P_RPM] == live::NormWhy::Cold);
  // Engine stopped.
  snap.g[live::G_AIRFLOW][2] = 0;
  live::decode(snap, prof::NormGroup::HFM_GENERIC, false, nullptr, d);
  CHECK(d.mode == live::EngineMode::Off);
  CHECK(d.why[live::P_MAF] != live::NormWhy::Applied);
}

namespace {
guard::Facts clearFacts() {
  guard::Facts f;
  f.sessionReady = true;
  f.batteryV = -1;
  f.batteryManualConfirm = true;  // switch in the dialog
  f.faultMemAgeMs = 2000;         // read by the device right before planning
  f.faultMemNonZero = true;
  f.liveAgeMs = 300;
  return f;
}
void identRef(EcuIdent& id, prof::EcuProfile& p) {
  PRig r;
  r.s.connect();
  uint8_t raw[28];
  r.s.readIdent(raw, 28);
  parseIdent(raw, 28, id);
  prof::resolve(id, prof::ResolveOptions(), p);
}
}  // namespace

TEST(dtc_clear_conditions_respect_the_users_confirmation) {
  EcuIdent id;
  prof::EcuProfile p;
  identRef(id, p);
  guard::ServiceGuard g;
  guard::Plan plan;
  guard::Facts f = clearFacts();
  // rpm reading not conclusive (VDO scale not confirmed): the switch decides.
  f.engineStopped = -1;
  CHECK(!g.prepare(guard::Op::DtcClear, p, id, f, 0, 1, plan));
  f.engineStoppedConfirm = true;
  CHECK(g.prepare(guard::Op::DtcClear, p, id, f, 0, 1, plan));
  // 0 rpm read: stopped even without the switch.
  f.engineStoppedConfirm = false;
  f.engineStopped = 1;
  CHECK(g.prepare(guard::Op::DtcClear, p, id, f, 0, 1, plan));
  // Running on a confirmed rpm address: refused with a clear reason.
  f.engineStoppedConfirm = true;
  f.engineStopped = 0;
  CHECK(!g.prepare(guard::Op::DtcClear, p, id, f, 0, 1, plan));
  CHECK(plan.blockReason != nullptr && strstr(plan.blockReason, "работает") != nullptr);
  // An empty fault memory does not block (nothing unsafe in erasing it).
  f = clearFacts();
  f.engineStoppedConfirm = true;
  f.faultMemNonZero = false;
  CHECK(g.prepare(guard::Op::DtcClear, p, id, f, 0, 1, plan));
  // The "before" readout is required: it is what the result is compared with.
  f.faultMemAgeMs = 0xFFFFFFFF;
  CHECK(!g.prepare(guard::Op::DtcClear, p, id, f, 0, 1, plan));
  // Battery switch off and no measurement: refused.
  f = clearFacts();
  f.engineStoppedConfirm = true;
  f.batteryManualConfirm = false;
  CHECK(!g.prepare(guard::Op::DtcClear, p, id, f, 0, 1, plan));
}

TEST(dtc_clear_on_an_ecu_without_page_memory) {
  PRig r;
  r.sim.setScenario(SimEcu::Scenario::Faults);
  r.sim.setForgetPage(true);
  CHECK(r.s.connect() == LinkStatus::Ok);
  svc::FaultMemory fm;
  CHECK(svc::readFaultMemory(r.s, fm, false) == LinkStatus::Ok);  // F1: prefix before every byte
  CHECK(fm.valid);
  bool any = false;
  for (int i = 0; i < 16; i++) any = any || fm.bankA[i] || fm.bankB[i];
  CHECK(any);
  r.s.armWrite(1);
  bool verified = false;
  svc::FaultMemory after;
  CHECK(svc::dtcClear(r.s, verified, after) == LinkStatus::Ok);
  CHECK(verified);
  CHECK(after.valid);
}
