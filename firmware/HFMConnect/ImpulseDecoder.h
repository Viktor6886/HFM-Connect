// ImpulseDecoder.h - blink-code (pulse counter) decoding.
//
// Factory procedure (OEM Engine 111 11/5, REV 1.4 s.32/50.2): with ignition ON
// the diagnostic contact is grounded for 2-4 s; after release the ECU
// indicates the code number N by pulling the line low N times (LED of the
// pulse counter lights). The first code repeats after the last one.
// Valid only for diagnosis versions <= 46; not for model 210 (WIS).
// Pulse widths are not specified in the documents: thresholds below are
// configurable defaults and every raw edge is kept as evidence.
#pragma once
#include <stdint.h>

struct ImpulseConfig {
  uint16_t pressMs = 3000;      // OEM window 2-4 s
  uint16_t minPulseMs = 60;     // shorter low phases are treated as glitches
  uint16_t maxPulseMs = 2000;   // longer low phase = not a code pulse
  uint16_t endGapMs = 2500;     // high (idle) phase that terminates a code
  uint16_t listenMs = 20000;    // max listening time after release
};

struct ImpulseEdge {
  uint8_t level;       // line level during the phase (0 = low/LED on)
  uint16_t durationMs;
};

struct ImpulseResult {
  bool complete = false;   // terminated by end gap (not by timeout)
  uint8_t pulses = 0;      // N = code number (1 = no fault)
  uint8_t glitches = 0;
  uint8_t longLows = 0;
  uint16_t edgeCount = 0;
};

// Decodes a sequence of phases recorded after the button release.
ImpulseResult decodeImpulses(const ImpulseEdge* edges, uint16_t n, const ImpulseConfig& cfg);

// ---- Factory blink-code table N -> three-digit code A -------------------------
// Diagnostic Manual Engines 09/00: Engine 104 (1.1 HFM-SFI 11/8..11/15) and
// Engine 111 (1.2 HFM-SFI 11/8..11/15). N exists only up to end of MY1995.
// N = 1 means "no malfunction stored".
enum : uint8_t { IMP_M104 = 1, IMP_M111 = 2, IMP_BOTH = 3 };

struct ImpulseCodeEntry {
  uint8_t n;
  uint8_t engines;      // IMP_* mask
  uint8_t count;
  uint16_t a[6];        // three-digit codes covered by this N
};

// Returns entries for N (0..2 entries: common and engine specific).
uint8_t impulseLookup(uint8_t n, uint8_t engineMask, const ImpulseCodeEntry** out, uint8_t max);
const ImpulseCodeEntry* impulseTable(uint16_t* count);
