// Replies that only repeat the request (D-034) and impossible values: never
// shown as data. Software tests with the ECU model; the behaviour of the real
// ECU 013 545 08 32 is taken from the owner's screenshots (values equal to the
// requested addresses), not from a trace.
#include <string.h>

#include "HfmLive.h"
#include "HfmServices.h"
#include "SimEcu.h"
#include "TraceLog.h"
#include "minitest.h"

namespace {
struct EchoRig {
  SimEcu sim;
  TraceLog trace;
  HfmSession s{sim, trace};
};
}  // namespace

TEST(echo_rule_needs_two_matching_bytes) {
  const uint8_t req[3] = {0x90, 0x92, 0x93};
  const uint8_t same[3] = {0x90, 0x92, 0x93};
  const uint8_t real[3] = {0x90, 0x80, 0x93};
  CHECK(repliesEchoRequest(req, same, 3));
  CHECK(!repliesEchoRequest(req, real, 3));  // one real byte: data
  CHECK(!repliesEchoRequest(req, same, 1));  // a single byte may coincide
}

TEST(echo_adaptations_and_coding_are_not_decoded) {
  EchoRig r;
  CHECK(r.s.connect() == LinkStatus::Ok);
  r.sim.setPageByte(0x24, 0x90, 0x90);
  r.sim.setPageByte(0x24, 0x92, 0x92);
  r.sim.setPageByte(0x24, 0x93, 0x93);
  svc::Adaptations a;
  CHECK(svc::readAdaptations(r.s, a) == LinkStatus::Ok);
  CHECK(a.echo);
  CHECK(!a.valid);  // "RAW 90" and "1,07" were the addresses, not values
  r.sim.setPageByte(0x27, 0x02, 0x02);
  r.sim.setPageByte(0x24, 0xF0, 0xF0);
  svc::Coding c;
  CHECK(svc::readCoding(r.s, c) == LinkStatus::Ok);
  CHECK(c.echo);
  CHECK(!c.valid);  // "02 / F0"
  // A genuine coding byte that happens to equal its address alone is data.
  r.sim.setPageByte(0x24, 0xF0, 0x00);
  CHECK(svc::readCoding(r.s, c) == LinkStatus::Ok);
  CHECK(c.valid && !c.echo);
  CHECK_EQ(c.info.coding, 0x02);
}

TEST(echo_fault_memory_rejected) {
  EchoRig r;
  CHECK(r.s.connect() == LinkStatus::Ok);
  for (uint8_t i = 0; i < 0x20; i++) r.sim.setFaultByte(i, i);
  svc::FaultMemory fm;
  readFaultMemory(r.s, fm, false);
  CHECK(fm.echo);
  CHECK(!fm.valid);
  r.sim.setFaultByte(0x05, 0x00);  // one real byte: a fault bitmap
  readFaultMemory(r.s, fm, false);
  CHECK(fm.valid && !fm.echo);
}

TEST(vin_not_supported_is_a_clean_answer) {
  EchoRig r;
  CHECK(r.s.connect() == LinkStatus::Ok);
  SimEcu::Inject inj;
  inj.badAckAt = (int32_t)r.sim.exchanges();  // the F9 prefix gets a wrong ACK
  r.sim.setInject(inj);
  svc::Vin v;
  CHECK(svc::readVin(r.s, v) == LinkStatus::BadAck);
  CHECK(v.unsupported);
  CHECK(!v.valid);
  // Read only: the session is resumed without a new BREAK.
  CHECK(r.s.resyncAllowed());
  CHECK(r.s.resync() == LinkStatus::Ok);
  CHECK(r.s.ready());
  // An ECU that answers F9 but stores no VIN.
  svc::Vin v2;
  CHECK(svc::readVin(r.s, v2) == LinkStatus::Ok);
  CHECK(v2.notStored && !v2.unsupported && !v2.valid);
}

TEST(echo_vdo_plan_values_are_not_shown) {
  EchoRig r;
  r.s.setExperimentalReads(true);
  CHECK(r.s.connect() == LinkStatus::Ok);
  for (uint8_t g = 0; g < live::V_COUNT; g++) {
    const live::GroupDef& d = live::vdoGroup(g);
    for (uint8_t i = 0; i < d.n; i++) r.sim.setPageByte(d.prefix, d.sel[i], d.sel[i]);
  }
  live::Snapshot snap;
  CHECK(live::pollNative(r.s, snap, true) == LinkStatus::Ok);
  CHECK(snap.planEcho);
  live::Decoded d;
  live::decode(snap, prof::NormGroup::HFM_GENERIC, true, nullptr, d);
  // 2955.4 kg/h (72 73), 152 km/h (7F), 60 ms (74 75) must not appear.
  CHECK(!d.v[live::P_MAF].hasValue);
  CHECK(d.v[live::P_MAF].q == live::Quality::NotSupported);
  CHECK(!d.v[live::P_SPEED].hasValue);
  CHECK(!d.v[live::P_INJ].hasValue);
  CHECK(!d.v[live::P_RPM].hasValue);
  CHECK(!d.v[live::P_O2].hasValue);
  CHECK(d.v[live::P_RPM].norm != live::NormState::Above);
}

TEST(echo_bosch_groups_and_real_groups_side_by_side) {
  EchoRig r;
  CHECK(r.s.connect() == LinkStatus::Ok);
  // Coolant group repeats its addresses, airflow is real.
  const live::GroupDef& co = live::group(live::G_COOLANT);
  for (uint8_t i = 0; i < co.n; i++) r.sim.setPageByte(co.prefix, co.sel[i], co.sel[i]);
  live::Snapshot snap;
  CHECK(live::pollNative(r.s, snap, false) == LinkStatus::Ok);
  CHECK(snap.groupEcho[live::G_COOLANT]);
  CHECK(!snap.groupEcho[live::G_AIRFLOW]);
  CHECK(!snap.planEcho);  // only one main group: the plan works
  live::Decoded d;
  live::decode(snap, prof::NormGroup::HFM_GENERIC, false, nullptr, d);
  CHECK(d.v[live::P_RPM].hasValue);
  CHECK(!d.v[live::P_ECT].hasValue);
  CHECK(d.v[live::P_ECT].q == live::Quality::NotSupported);
}

TEST(impossible_values_are_hidden) {
  live::Snapshot s;
  for (uint8_t g = 0; g < live::G_COUNT; g++) s.groupOk[g] = true;
  s.g[live::G_AIRFLOW][2] = 0x20;  // 800 rpm
  s.g[live::G_COOLANT][1] = 200;   // ~85 degC
  s.g[live::G_MAF_LO][0] = 0x10;   // (0x10 + 256*0xFF) / 10 = 6529.6 kg/h
  s.g[live::G_MAF_HI][0] = 0xFF;
  s.g[live::G_RPM_LOW][0] = 0x00;  // 0x7500 // 50 / 10 = 59.9 ms
  s.g[live::G_RPM_HIGH][0] = 0x75;
  live::Decoded d;
  live::decode(s, prof::NormGroup::HFM_GENERIC, false, nullptr, d);
  CHECK(d.v[live::P_RPM].hasValue);
  CHECK(!d.v[live::P_MAF].hasValue);
  CHECK(d.v[live::P_MAF].q == live::Quality::Implausible);
  CHECK(!d.v[live::P_INJ].hasValue);
  CHECK(d.v[live::P_INJ].q == live::Quality::Implausible);
  // Plausible values stay.
  s.g[live::G_MAF_LO][0] = 170;  // 17.0 kg/h
  s.g[live::G_MAF_HI][0] = 0;
  s.g[live::G_RPM_LOW][0] = 0xDC;  // 1500 // 50 / 10 = 3.0 ms
  s.g[live::G_RPM_HIGH][0] = 0x05;
  live::decode(s, prof::NormGroup::HFM_GENERIC, false, nullptr, d);
  CHECK(d.v[live::P_MAF].hasValue);
  CHECK(d.v[live::P_INJ].hasValue);
}

TEST(generic_norms_apply_without_a_vehicle) {
  CHECK(prof::normFor("rpm", prof::NormGroup::HFM_GENERIC, false) != nullptr);
  CHECK(prof::normFor("maf", prof::NormGroup::HFM_GENERIC, false) != nullptr);
  const prof::Norm* n = prof::normFor("adapt_idle", prof::NormGroup::HFM_GENERIC, true);
  CHECK(n != nullptr && n->lo < -10);  // VDO idle adaptation in %
  // The generic range covers every engine-specific one.
  const prof::NormGroup gs[] = {prof::NormGroup::M104_EARLY, prof::NormGroup::M104_CODED, prof::NormGroup::M111_961,
                                prof::NormGroup::M104_995};
  const char* keys[] = {"rpm", "inj_ms", "maf", "ign_deg", "throttle"};
  for (const char* k : keys) {
    const prof::Norm* g = prof::normFor(k, prof::NormGroup::HFM_GENERIC, false);
    for (prof::NormGroup ng : gs) {
      const prof::Norm* e = prof::normFor(k, ng, false);
      if (!e) continue;
      CHECK(g->lo <= e->lo && g->hi >= e->hi);
    }
  }
}
