// DtcRefresh.h - when the engine reads the fault memory by itself (D-033).
// Platform independent (no Arduino.h), covered by host tests.
//
// A readout taken before the link was lost is not current any more, so after
// the link is back the fault memory is read again (F1, read only). The rules
// below keep that from ever becoming a loop on a bad line:
//   - a failed automatic read is repeated not sooner than kRetryMs later and
//     at most kMaxFails times in a row;
//   - if the link is lost again right after an automatic read (kSuspectMs)
//     kMaxSuspect times in a row, automatic reads stop until the user reads
//     the fault memory or connects again.
#pragma once
#include <stdint.h>

class DtcRefresh {
 public:
  static constexpr uint8_t kMaxFails = 3;
  static constexpr uint8_t kMaxSuspect = 2;
  static constexpr uint32_t kRetryMs = 3000;
  static constexpr uint32_t kSuspectMs = 3000;

  // The link (as the user sees it) left the "ready" state.
  void linkLost(uint32_t nowMs) {
    wanted_ = true;
    if (suspect_ < kMaxSuspect) {
      if (autoDone_ && nowMs - lastAutoMs_ < kSuspectMs) suspect_++;
      else suspect_ = 0;
    }
    autoDone_ = false;  // every automatic read is judged once
  }

  // A new session was opened by the automatic connection.
  void connected() { wanted_ = true; }

  // The user pressed "Connect": read once, earlier failures are forgotten.
  void userConnected() {
    wanted_ = true;
    fails_ = 0;
    suspect_ = 0;
    autoDone_ = false;
  }

  // A complete readout was stored (automatic = started by the engine itself).
  void readOk(uint32_t nowMs, bool automatic) {
    wanted_ = false;
    fails_ = 0;
    if (automatic) {
      autoDone_ = true;
      lastAutoMs_ = nowMs;
    } else {
      suspect_ = 0;
      autoDone_ = false;
    }
  }

  // A readout failed; only the engine's own attempts are counted.
  void readFailed(uint32_t nowMs, bool automatic) {
    if (!automatic) return;
    if (fails_ < 255) fails_++;
    lastFailMs_ = nowMs;
  }

  bool wanted() const { return wanted_; }
  bool suspended() const { return fails_ >= kMaxFails || suspect_ >= kMaxSuspect; }
  // True when the engine should read the fault memory now.
  bool due(uint32_t nowMs) const {
    if (!wanted_ || suspended()) return false;
    return fails_ == 0 || nowMs - lastFailMs_ >= kRetryMs;
  }

 private:
  bool wanted_ = false;    // the stored readout (if any) is not current
  bool autoDone_ = false;  // an automatic read completed since the last loss
  uint8_t fails_ = 0, suspect_ = 0;
  uint32_t lastAutoMs_ = 0, lastFailMs_ = 0;
};
