// HfmServices.h - diagnostic services built on HfmSession.
//
// Read services are available per capability manifest. State-changing
// services are *quarantined recipes* reconstructed from HFMreader
// (REV 1.1 s.16, REV 1.4 s.12.1/16): they are only reachable through
// ServiceGuard (prepare -> confirm -> execute, hardware interlock,
// preconditions, no automatic retry).
#pragma once
#include <stdint.h>

#include "HfmDecode.h"
#include "HfmSession.h"

namespace svc {

// ---- read services ----------------------------------------------------------
struct FaultMemory {
  bool valid = false;
  uint8_t bankA[16] = {0};   // F1 00..0F
  uint8_t bankB[16] = {0};   // F1 10..1F
  bool extValid = false;
  uint8_t ext[6] = {0};      // F1 30,31,32,40,41,42 (HFMScan, experimental)
  uint32_t tMs = 0;
};
LinkStatus readFaultMemory(HfmSession& s, FaultMemory& fm, bool extended);

struct Adaptations {
  bool valid = false;
  uint8_t rawIdle = 0, rawLower = 0, rawUpper = 0;
  float idleKgH = 0;      // Bosch formula; VDO units unknown -> show RAW
  float lower = 1.0f, upper = 1.0f;
  uint32_t tMs = 0;
};
LinkStatus readAdaptations(HfmSession& s, Adaptations& a);

struct Coding {
  bool valid = false;
  hfm::CodingInfo info;
  uint32_t tMs = 0;
};
LinkStatus readCoding(HfmSession& s, Coding& c);

struct Vin {
  bool valid = false;
  char text[15] = {0};
  uint8_t raw[14] = {0};
  uint8_t len = 0;
};
LinkStatus readVin(HfmSession& s, Vin& v);

// ---- quarantined write recipes ------------------------------------------------
// All functions assume the session has been armed by ServiceGuard.

// DTC clear (HFMreader DtcClearProtocol.clear_and_verify). Returns Ok and
// verified=true only if all 32 bytes read 00 afterwards.
LinkStatus dtcClear(HfmSession& s, bool& verified, FaultMemory& after);

// Adaptation reset (HFMreader session._recipe). Requires 20:B7 == 0 checked by
// the caller. A key cycle is required afterwards; success is NOT verified.
LinkStatus adaptationReset(HfmSession& s, uint8_t* responses10);

// Purge valve Y58/1 test (HFMreader session purge/restore).
LinkStatus purgeEnter(HfmSession& s, bool& sondeSent);
LinkStatus purgeSet(HfmSession& s, bool on);
LinkStatus purgeRestore(HfmSession& s, bool sondeSent);

}  // namespace svc
