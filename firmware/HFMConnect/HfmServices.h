// HfmServices.h - diagnostic services built on HfmSession.
//
// Read services are available per capability manifest. State-changing
// services are *quarantined recipes* (REV 1.1 s.16, REV 1.4 s.12.1/16): they are only reachable through
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
  uint8_t ext[6] = {0};      // F1 30,31,32,40,41,42 (experimental)
  bool echo = false;         // every byte equals its address: no fault data (D-034)
  uint32_t tMs = 0;
};
// True if a readout carries no fault data: each byte equals its own address.
bool faultMemoryIsEcho(const FaultMemory& fm);
LinkStatus readFaultMemory(HfmSession& s, FaultMemory& fm, bool extended);

struct Adaptations {
  bool valid = false;
  uint8_t rawIdle = 0, rawLower = 0, rawUpper = 0;
  float idleKgH = 0;      // Bosch formula; VDO units unknown -> show RAW
  float lower = 1.0f, upper = 1.0f;
  bool echo = false;      // ECU repeated the addresses 90/92/93: not supported (D-034)
  uint32_t tMs = 0;
};
LinkStatus readAdaptations(HfmSession& s, Adaptations& a);

struct Coding {
  bool valid = false;
  hfm::CodingInfo info;
  bool echo = false;      // ECU repeated the addresses 02/F0: not supported (D-034)
  uint32_t tMs = 0;
};
LinkStatus readCoding(HfmSession& s, Coding& c);

struct Vin {
  bool valid = false;
  char text[15] = {0};
  uint8_t raw[14] = {0};
  uint8_t len = 0;
  bool notStored = false;   // first byte 00/FF: no VIN in this ECU
  bool unsupported = false; // F9 not accepted (wrong ACK / no answer to the prefix)
  uint8_t ackSeen = 0;      // what the ECU answered instead of the ACK 06
};
LinkStatus readVin(HfmSession& s, Vin& v);

// ---- quarantined write recipes ------------------------------------------------
// All functions assume the session has been armed by ServiceGuard.

// DTC clear (REV 1.1 s.16; factory tester template F3 FF, PC tester "Erase").
// Returns Ok and verified=true only if all 32 bytes read 00 afterwards; codes
// of faults that are still present come back at once (ECU sets them again).
// confirmed (optional): the ECU acknowledged the whole erase sequence (F3 FF
// answered 00); a non-Ok status with confirmed = true means only the
// verification read that follows failed.
LinkStatus dtcClear(HfmSession& s, bool& verified, FaultMemory& after, bool* confirmed = nullptr);

// Adaptation reset (REV 1.1 s.16). Requires 20:B7 == 0 checked by
// the caller. A key cycle is required afterwards; success is NOT verified.
LinkStatus adaptationReset(HfmSession& s, uint8_t* responses10);

// Purge valve Y58/1 test (REV 1.1 s.16).
LinkStatus purgeEnter(HfmSession& s, bool& sondeSent);
LinkStatus purgeSet(HfmSession& s, bool on);
LinkStatus purgeRestore(HfmSession& s, bool sondeSent);

}  // namespace svc
