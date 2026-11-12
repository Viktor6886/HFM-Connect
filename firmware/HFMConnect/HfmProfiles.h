// HfmProfiles.h - vehicle/engine database, connector profiles, ECU profile
// resolution, capability manifest and factory norms.
//
// Selection is deterministic and fail-closed (REV 1.4 s.7, s.48):
//   * vehicle data (chassis, engine, production date) only FILTERS norms,
//     wiring hints and expectations - it never unlocks a command;
//   * commands are unlocked by the ECU identification (vendor, part number,
//     diagnostic index) and the evidence level stored for that ECU;
//   * a missing field is never a wildcard for a write operation.
#pragma once
#include <stdint.h>

#include "HfmDtc.h"
#include "HfmIdent.h"
#include "HfmTypes.h"

namespace prof {

// Diagnostic generation (REV 1.4 s.29).
enum class Generation : uint8_t { Unknown, EarlyAuto, Transition, Coded, Hfm41 };
const char* generationCode(Generation g);
const char* generationName(Generation g);

// Norm group (REV 1.4 s.10, Test and Adjustment Data [И07]).
// HFM_GENERIC: no vehicle selected - the widest factory range over all HFM-SFI
// engines of I07 (a value outside it is outside the norm of every engine).
enum class NormGroup : uint8_t { None, M104_EARLY, M104_CODED, M111_961, M104_995, HFM_GENERIC };
const char* normGroupCode(NormGroup g);

struct ConnectorPin {
  uint8_t pin;
  const char* role;
};

struct ConnectorProfile {
  const char* id;        // "W124_X11_4_16"
  const char* name;      // "X11/4, 16-контактный (W124)"
  uint8_t pins;          // 16 / 38 / 8
  const ConnectorPin* map;
  uint8_t mapLen;
  Confidence conf;
  const char* source;
  const char* warning;
};

struct VehicleEntry {
  const char* chassis;   // "W124"
  const char* model;     // "124.032/052/066/092"
  const char* name;      // "E320"
  const char* engine;    // "104.992"
  uint32_t from;         // YYYYMM, 0 = unknown
  uint32_t to;           // YYYYMM, 0 = open / not stated in source
  Generation gen;
  NormGroup norms;
  const char* connector; // ConnectorProfile id
  const char* dm;        // "DM 8.5", "DM 8.6" or "" (separate N59/1 module, not HFM)
  Confidence conf;
  const char* source;
};

const VehicleEntry* vehicles(uint16_t* count);
const ConnectorProfile* connectors(uint16_t* count);
const ConnectorProfile* connectorById(const char* id);
const VehicleEntry* vehicleByIndex(uint16_t idx);

// Generation from production date (YYYYMM) and engine; date alone never
// decides write capability (REV 1.4 s.13, s.48).
Generation generationFor(const char* engine, uint32_t yyyymm);

// ---- capabilities -------------------------------------------------------
enum Feature : uint8_t {
  F_IDENT = 0,
  F_DTC_READ,
  F_DTC_EXTENDED,   // F1 30..32 / 40..42 (REV 1.1, experimental)
  F_LIVE_NATIVE,    // full8 groups
  F_LIVE_EXTENDED,  // extended recipes (speed, O2 rear, idle target, status 98)
  F_ADAPT_READ,
  F_CODING_READ,
  F_VIN_READ,       // F9 00..0D (REV 1.1, experimental)
  F_DTC_CLEAR,
  F_ADAPT_RESET,
  F_PURGE_TEST,
  F_VERSION_CODING_WRITE,
  F_THROTTLE_LEARN,
  F_IMPULSE_READ,
  F_IMPULSE_CLEAR,
  F_COUNT
};
const char* featureCode(uint8_t f);
const char* featureName(uint8_t f);   // plain name for the user
const char* featureTech(uint8_t f);   // commands / technical detail (shown on demand)
bool featureIsWrite(uint8_t f);

struct Capability {
  bool allowed;       // firmware will execute (subject to runtime guards)
  Confidence conf;
  const char* reason; // why blocked / what is required
};

struct EcuProfile {
  char id[32];            // "BOSCH_0135451332_41" / "GENERIC_HFM_45_59" / "UNRESOLVED"
  char name[112];
  bool resolved;          // identified as HFM-SFI
  bool referenceEcu;      // reference ECU 013 545 13 32 / 41-00
  bool vdoPlanFirst;      // VDO: the VDO live-data plan of the factory templates (D-037)
  dtc::Variant variant;
  dtc::BankMapping bankMapping;
  Confidence bankMappingConf;
  bool vdoUnits;          // vendor letter 'V': VDO live-data addresses, adaptation RAW
  bool vdoCandidate;      // unknown vendor letter in the HFM range: live plan is probed
  Capability caps[F_COUNT];
};

struct ResolveOptions {
  bool experimentalReads = false; // user opt-in for unknown ECU reads
  bool engineerMode = false;      // kept for compatibility; service ops no longer need it (D-035)
  uint8_t cylinders = 0;          // from the selected vehicle (M104 = 6, M111 = 4), 0 = unknown
};

// Cylinder count from the engine designation ("104.992" -> 6, "111.961" -> 4).
uint8_t cylindersForEngine(const char* engine);

// Resolves the profile from identification (may be absent: ident.present=false).
void resolve(const EcuIdent& ident, const ResolveOptions& opt, EcuProfile& out);

// ---- norms ---------------------------------------------------------------
enum class NormCond : uint8_t { WarmIdle, WarmRunning, WarmAny, Always };

struct Norm {
  const char* param;   // parameter key (HfmLive registry)
  NormGroup group;
  float lo, hi;
  NormCond cond;
  const char* note;
  const char* source;
};

// First norm for param/group, nullptr if none.
const Norm* normFor(const char* param, NormGroup g, bool vdo);
const Norm* norms(uint16_t* count);

}  // namespace prof
