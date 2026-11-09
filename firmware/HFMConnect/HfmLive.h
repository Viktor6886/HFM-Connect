// HfmLive.h - Live Data polling plan, parameter registry and evaluation.
//
// Three independent layers per value (REV 1.4 s.10.2):
//   protocol       VALID_RAW / PARTIAL / TIMEOUT        (group read status)
//   interpretation VALID_VALUE / NO_DATA / OUT_OF_DOMAIN / IMPLAUSIBLE
//   norm           IN_SPEC / BELOW / ABOVE / NOT_APPLICABLE / NO_NORM
// A value is flagged red only when all three layers are valid and the
// factory norm applies to the measured conditions.
#pragma once
#include <stdint.h>

#include "HfmProfiles.h"
#include "HfmSession.h"

namespace live {

// ---- polling plan ------------------------------------------------------------
// Native groups: polling profile full8 (REV 1.1 s.6).
enum GroupId : uint8_t { G_RPM_LOW, G_RPM_HIGH, G_MIXTURE, G_AIRFLOW, G_COOLANT, G_MAF_LO, G_MAF_HI, G_COUNT };
// Extended reads (AC, gated by F_LIVE_EXTENDED).
enum ExtId : uint8_t { X_SPEED, X_O2_REAR, X_IDLE_TARGET, X_STATUS98, X_COUNT };
// VDO variant plan (factory tester templates, vendor V): other addresses.
enum VdoGroupId : uint8_t { V_P12, V_P22, V_INJ, V_P23, V_O2, V_COUNT };

struct GroupDef {
  const char* name;
  uint8_t prefix;
  uint8_t n;
  uint8_t sel[6];
};
const GroupDef& group(uint8_t g);
const GroupDef& extRead(uint8_t x);
const GroupDef& vdoGroup(uint8_t g);

struct Snapshot {
  uint32_t seq = 0;
  uint32_t tMs = 0;
  uint16_t cycleMs = 0;       // duration of the poll cycle
  bool groupOk[G_COUNT] = {};
  uint8_t g[G_COUNT][6] = {};
  bool extRead = false;
  bool extOk[X_COUNT] = {};
  uint8_t ext[X_COUNT] = {};
  bool vdoPlan = false;       // snapshot taken with the VDO address plan
  bool vdoOk[V_COUNT] = {};
  uint8_t vdo[V_COUNT][6] = {};
  LinkStatus lastStatus = LinkStatus::Ok;
};

// Reads the native groups (full8: 7 prefixes, 21 selectors). MAF low/high are
// sampled in the same cycle (REV 1.1 s.6). vdo = use the VDO address plan
// (experimental read prefixes 12/22/23, VDO candidate profiles only).
LinkStatus pollNative(HfmSession& s, Snapshot& snap, bool vdo = false);
LinkStatus pollExtended(HfmSession& s, Snapshot& snap);

// ---- registry -------------------------------------------------------------------
enum class Category : uint8_t { Base, Mixture, Ignition, Throttle, Status, Extended };
enum class Quality : uint8_t { Valid, NotRead, Partial, NoData, OutOfDomain, Implausible };
enum class NormState : uint8_t { NoNorm, NotApplicable, InSpec, Below, Above };

const char* qualityCode(Quality q);
const char* normStateCode(NormState n);

enum ParamId : uint8_t {
  P_RPM, P_ECT, P_IAT, P_MAF, P_THROTTLE, P_LOAD, P_IGN, P_O2, P_O2_ALT, P_LAMBDA, P_INJ,
  P_AC, P_DECEL_CUT, P_POSTSTART, P_WARMUP, P_FULL_LOAD, P_PREHEAT, P_AIR_PUMP, P_CAMSHAFT, P_RESONANCE,
  P_SPEED, P_O2_REAR, P_IDLE_TARGET, P_IDLE_CONTACT, P_STARTER,
  P_COUNT
};

struct ParamDef {
  const char* key;
  const char* name;      // RU display name
  const char* unit;
  Category cat;
  const char* source;    // "20:B7"
  const char* formula;
  Confidence conf;
  const char* evidence;  // AV / AH / AC evidence classes (REV 1.1)
  float chartMin, chartMax;
  uint8_t decimals;
  bool isBool;
};
const ParamDef& param(uint8_t p);

struct Value {
  float v = 0;
  bool hasValue = false;
  Quality q = Quality::NotRead;
  NormState norm = NormState::NoNorm;
  float normLo = 0, normHi = 0;
  const prof::Norm* normRef = nullptr;
  uint8_t raw0 = 0, raw1 = 0;
  uint8_t rawCount = 0;
};

struct Decoded {
  uint32_t seq = 0;
  uint32_t tMs = 0;
  Value v[P_COUNT];
  bool conditionsWarmIdle = false;
  bool conditionsWarmRunning = false;
};

// Decodes a snapshot. prev (may be null) is used for plausibility checks.
// diagRaw = F0:26 byte: selects the idle-target source (< 0x41: 21:D7).
void decode(const Snapshot& s, prof::NormGroup ng, bool vdo, const Decoded* prev, Decoded& out,
            uint8_t diagRaw = 0x41);

// Status-bit masks by display index q (factory tester tables): Bosch and VDO.
uint8_t statusMaskBosch(uint8_t q);
uint8_t statusMaskVdo(uint8_t q);

}  // namespace live
