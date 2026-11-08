// HfmDecode.h - conversion formulas for HFM-SFI raw bytes.
//
// Every formula is reconstructed (REV 1.1 s.7, HFMreader parameter_registry and
// hht_experimental/decoder). Integer paths reproduce the original truncating
// arithmetic exactly; trunc0 rounds toward zero (REV 1.4 s.11.1).
// Evidence classes: AV = author field-verified on ECU 013 545 13 32,
// AH = highly likely, AC = candidate. None is factory data.
#pragma once
#include <stdint.h>

namespace hfm {

inline int32_t trunc0(int32_t n, int32_t d) { return n >= 0 ? n / d : -((-n) / d); }

// ---- native live data (prefix 20 / 21) ------------------------------------
inline uint16_t rpm(uint8_t b7) { return (uint16_t)(25u * b7); }                 // 20:B7  AV
float coolantNativeC(uint8_t bf);                                                // 21:BF  AV (14-point)
bool iatNativeC(uint8_t be, float* out);                                         // 21:BE  AH (8-point, bounded)
inline uint16_t o2NativeMv(uint8_t r23) { return (uint16_t)(4u * r23); }         // 20:23  AH
inline float throttleDeg(uint8_t bd) { return bd * 90.0f / 256.0f; }             // 20:BD  AH
inline float loadPct(uint8_t c2) { return c2 / 2.0f; }                           // 20:C2  AH
inline float ignitionDeg(uint8_t c4) { return 55.0f - c4 * 90.0f / 256.0f; }     // 20:C4  AH
inline float mafKgH(uint8_t lo48, uint8_t hi49) {                                // 20:48/49 AV route
  return (float)(lo48 | (hi49 << 8)) / 10.0f;
}

// ---- HHT callbacks (AC) ------------------------------------------------------
int16_t coolantHhtC(uint8_t raw);    // TABLES.coolant[raw]
int16_t iatHhtC(uint8_t raw);        // TABLES.intake_air[raw]
inline int32_t o2HhtMv(uint8_t d) { return trunc0(51102 * (int32_t)d - 1928300, 10000); }
inline int32_t throttleHhtTenths(uint8_t d) { return (int32_t)((3515625ull * d) / 1000000ull); }
inline int32_t mafHhtKgH(uint8_t lo, uint8_t hi) { return (lo + 256 * hi) / 10; }
inline int32_t speedKmh(uint8_t d) { return 12 * d / 10; }
inline int32_t ignitionHhtDeg(uint8_t d0, uint8_t rpmByte) {
  return (25 * rpmByte < 100) ? 0 : trunc0(5500000 - 35157 * (int32_t)d0, 100000);
}
// llupm: word0 = idle target, word1 = rpm (0 when engine stopped)
void idleTargetHht(uint8_t d2, uint8_t d6, uint8_t d8, uint8_t i6a, uint16_t* target, uint16_t* rpmOut);
// Status bit via bit_v1_old[q & 0xFF], inverted when (q >> 8) & 2.
bool statusBitHht(uint8_t d0, uint16_t q);
uint8_t statusMaskHht(uint16_t q);

// ---- fuel / adaptation page (AC) -----------------------------------------
// Returns false when the gating rpm byte is 0 (engine stopped -> no data).
bool injectionMs(uint8_t lo54, uint8_t hi55, uint8_t rpmByte, float* out);
bool lambdaPct(uint8_t b9, uint8_t rpmByte, int32_t* out);
inline int32_t lambdaPctRaw(uint8_t b9) { return (int32_t)(390625ull * b9 / 1000000ull) - 50; }
inline float adaptIdleKgH(uint8_t r90) { return trunc0(275 * (int32_t)r90 - 35000, 1000) / 10.0f; }
inline float adaptFactor(uint8_t r) { return (trunc0(390625 * (int32_t)r - 50000000, 1000000) + 100) / 100.0f; }

// ---- configuration (27* 02 24* F0) ------------------------------------------
struct CodingInfo {
  bool valid = false;
  uint8_t coding = 0;   // c
  uint8_t mask = 0;     // m ("not applicable" mask)
  int8_t automatic = -1;      // 1 AT, 0 MT, -1 no data
  int8_t gears = -1;          // 4/5, -1 no data
  int8_t catalyst = -1;       // 1 CAT, 0 NO CAT, -1 no data
  int8_t usa = -1;            // 1 / 0 / -1
  int8_t efp = -1;            // EFP/TPM flag
  int8_t diagModule = -1;     // DM present flag
  int8_t throttleTaught = -1; // 1 taught, 0 not taught
  int16_t screen = -1;        // HHT screen candidate 103/104/107/108/109
};
CodingInfo decodeCoding(uint8_t c, uint8_t m);

// Coolant plausibility (HFMreader data_quality, CS).
bool coolantPlausible(float v, float last, uint32_t dtMs, bool haveLast);

}  // namespace hfm
