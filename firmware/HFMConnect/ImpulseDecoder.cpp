#include "ImpulseDecoder.h"

ImpulseResult decodeImpulses(const ImpulseEdge* e, uint16_t n, const ImpulseConfig& cfg) {
  ImpulseResult r;
  r.edgeCount = n;
  bool started = false;
  for (uint16_t i = 0; i < n; i++) {
    if (e[i].level == 0) {
      if (e[i].durationMs < cfg.minPulseMs) { r.glitches++; continue; }
      if (e[i].durationMs > cfg.maxPulseMs) { r.longLows++; continue; }
      if (r.pulses < 255) r.pulses++;
      started = true;
    } else if (started && e[i].durationMs >= cfg.endGapMs) {
      r.complete = true;
      break;
    }
  }
  return r;
}

// Transcribed from the factory DTC tables (columns "N" and "A").
static const ImpulseCodeEntry kImp[] = {
    {2, IMP_BOTH, 4, {2, 3, 4, 5}},
    {3, IMP_BOTH, 3, {6, 7, 8}},
    {4, IMP_BOTH, 2, {9, 10}},
    {5, IMP_BOTH, 3, {11, 12, 13}},
    {6, IMP_BOTH, 3, {14, 15, 16}},     // M104: not applicable for U.S.A. version
    {7, IMP_BOTH, 3, {17, 18, 19}},     // M104: not applicable for U.S.A. version
    {8, IMP_BOTH, 2, {20, 21}},
    {9, IMP_BOTH, 3, {23, 24, 25}},
    {10, IMP_BOTH, 3, {26, 27, 28}},    // M104: except model 124
    {11, IMP_BOTH, 3, {29, 30, 31}},
    {12, IMP_BOTH, 3, {32, 33, 34}},    // M104: except model 124
    {13, IMP_BOTH, 2, {35, 36}},
    {14, IMP_BOTH, 2, {37, 38}},
    {15, IMP_BOTH, 2, {39, 40}},
    {16, IMP_BOTH, 2, {41, 42}},
    {17, IMP_BOTH, 2, {43, 44}},
    {18, IMP_M104, 2, {45, 46}},
    {19, IMP_M104, 2, {47, 48}},
    {20, IMP_BOTH, 6, {49, 50, 51, 52, 53, 54}},
    {21, IMP_M104, 3, {61, 62, 63}},
    {22, IMP_BOTH, 3, {55, 56, 57}},
    {23, IMP_BOTH, 3, {58, 59, 60}},
    {24, IMP_BOTH, 3, {64, 65, 66}},
    {25, IMP_BOTH, 1, {67}},
    {27, IMP_BOTH, 2, {70, 71}},
    {28, IMP_BOTH, 2, {72, 73}},
    {30, IMP_BOTH, 1, {76}},
    {32, IMP_BOTH, 2, {79, 80}},
    {33, IMP_BOTH, 2, {81, 82}},
    {34, IMP_BOTH, 2, {83, 84}},
    {35, IMP_BOTH, 1, {85}},
    {36, IMP_BOTH, 2, {86, 87}},
    {37, IMP_BOTH, 1, {88}},
    {38, IMP_BOTH, 2, {89, 90}},
    {39, IMP_BOTH, 2, {91, 92}},
    {40, IMP_M104, 4, {93, 94, 95, 96}},
    {41, IMP_BOTH, 1, {97}},
    {42, IMP_M104, 3, {98, 99, 100}},
    {42, IMP_M111, 1, {100}},
    {43, IMP_BOTH, 1, {101}},
    {45, IMP_M104, 1, {104}},
    {46, IMP_M104, 2, {105, 106}},
    {48, IMP_BOTH, 2, {108, 109}},
    {49, IMP_BOTH, 2, {110, 111}},
    {50, IMP_BOTH, 1, {112}},
};

const ImpulseCodeEntry* impulseTable(uint16_t* count) {
  *count = sizeof(kImp) / sizeof(kImp[0]);
  return kImp;
}

uint8_t impulseLookup(uint8_t n, uint8_t engineMask, const ImpulseCodeEntry** out, uint8_t max) {
  uint8_t k = 0;
  for (uint16_t i = 0; i < sizeof(kImp) / sizeof(kImp[0]) && k < max; i++)
    if (kImp[i].n == n && (kImp[i].engines & engineMask)) out[k++] = &kImp[i];
  return k;
}
