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
