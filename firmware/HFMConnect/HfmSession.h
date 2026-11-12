// HfmSession.h - HFM-SFI K-Line transaction layer (REV 1.1 section 4).
//
//   TX PREFIX (8N1)    -> RX echo, RX ACK = PREFIX ^ 0xFF   (page context set)
//   TX SELECTOR (8S1)  -> RX echo, RX DATA
//   TX ARGUMENT (8S1)  -> RX echo, RX reply (write pages; usually ~ARG)
//
// Fail-closed rules enforced here (not in the UI):
//   * only whitelisted prefixes are ever transmitted (no 0x00..0xFF scanning);
//   * state-changing prefixes require an armed write token (ServiceGuard);
//   * any echo/ACK/timeout error drops the session to Error, invalidates the
//     page context and is never retried automatically;
//   * a READ session may be resumed by resync(): one read prefix (F0 -> 0F)
//     re-establishes a known page without a new BREAK. Never after a write
//     page or an armed write token (D-027).
#pragma once
#include "ByteLink.h"
#include "TraceLog.h"

enum class SessionState : uint8_t { Disconnected, Connecting, Ready, Error };

inline const char* sessionStateCode(SessionState s) {
  switch (s) {
    case SessionState::Disconnected: return "disconnected";
    case SessionState::Connecting: return "connecting";
    case SessionState::Ready: return "ready";
    default: return "error";
  }
}

enum class StepKind : uint8_t { Prefix, Selector, Argument };

struct RecipeStep {
  uint8_t byte;
  StepKind kind;
  int16_t expect;     // -1: no check; otherwise required response byte
  uint16_t delayMs;   // pause after the step (DTC clear timing)
};

// Classification of prefixes (see docs/PROTOCOL.md "Command policy").
enum class PrefixClass : uint8_t { Unknown, Read, ExperimentalRead, Write };

PrefixClass classifyPrefix(uint8_t op);

// An ECU that does not serve an address answers each selector with the
// selector itself: observed on 013 545 08 32 (VDO, diag 40/00) - 22:72/73 ->
// 72 73, 24:90/92/93 -> 90 92 93, 27:02 + 24:F0 -> 02 F0 (D-034). A read whose
// every reply equals its request (at least two bytes, so a single coincidence
// does not count) carries no data and must not be decoded.
inline bool repliesEchoRequest(const uint8_t* req, const uint8_t* reply, uint8_t n) {
  if (n < 2) return false;
  for (uint8_t i = 0; i < n; i++)
    if (reply[i] != req[i]) return false;
  return true;
}

class HfmSession {
 public:
  HfmSession(ByteLink& link, TraceLog& trace) : link_(link), trace_(trace) {}

  void setTiming(const LinkTiming& t) { timing_ = t; }
  const LinkTiming& timing() const { return timing_; }

  // Opens the link and performs BREAK/AA/55 (attempts: 0 = configured
  // retries). Before every BREAK the line is kept silent for silenceNeededMs().
  LinkStatus connect(uint8_t attempts = 0);
  void disconnect();
  // Remaining line silence required before the next BREAK (0 = may wake now).
  uint32_t silenceNeededMs();
  // Read-only resume after a byte error: F0 prefix, expects echo + ACK 0F.
  LinkStatus resync();
  bool resyncAllowed() const;
  uint32_t resyncCount() const { return resyncCount_; }
  uint8_t wakeFailStreak() const { return wakeFails_; }

  SessionState state() const { return state_; }
  bool ready() const { return state_ == SessionState::Ready; }
  LinkStatus lastError() const { return lastError_; }
  uint8_t page() const { return page_; }
  bool pageValid() const { return pageValid_; }

  // --- primitives ---------------------------------------------------------
  LinkStatus prefix(uint8_t op);
  LinkStatus selector(uint8_t sel, uint8_t& data);
  LinkStatus argument(uint8_t arg, uint8_t& reply, int16_t expect = -1);

  // --- composite reads ----------------------------------------------------
  LinkStatus readGroup(uint8_t op, const uint8_t* sels, uint8_t n, uint8_t* out);
  // Same addresses, but every selector gets its own prefix ("op sel op sel ..."),
  // as the factory tester templates of the VDO variant read (D-037) and as F0/F1
  // are read: for ECUs that keep no page context after a selector.
  LinkStatus readEach(uint8_t op, const uint8_t* sels, uint8_t n, uint8_t* out);
  // F0 identification bytes 0..n-1 (n <= 28 for the read-only fingerprint).
  LinkStatus readIdent(uint8_t* out, uint8_t n = 28);
  // F1 fault-memory bytes [first, first+count).
  LinkStatus readFaultBytes(uint8_t first, uint8_t count, uint8_t* out);
  // Generic recipe; responses[i] receives the reply of step i (ACK for prefixes).
  LinkStatus runRecipe(const RecipeStep* steps, uint8_t n, uint8_t* responses);

  // --- policy -------------------------------------------------------------
  // Arms state-changing prefixes for exactly one operation (ServiceGuard).
  void armWrite(uint32_t token) { writeToken_ = token; }
  void disarmWrite() { writeToken_ = 0; }
  bool writeArmed() const { return writeToken_ != 0; }
  void setExperimentalReads(bool en) { experimentalReads_ = en; }
  bool experimentalReads() const { return experimentalReads_; }
  // ECU identified as VDO (vendor letter V): its read pages 12/22/23 of the
  // factory tester templates are normal reads for it (D-037).
  void setVdoReads(bool en) { vdoReads_ = en; }
  bool vdoReads() const { return vdoReads_; }

  // Statistics
  uint32_t txCount() const { return txCount_; }
  uint32_t errorCount() const { return errCount_; }
  uint32_t lastActivityMs() const { return lastActivityMs_; }
  uint32_t connectedAtMs() const { return connectedAtMs_; }
  uint8_t lastSyncEcho() const { return syncEcho_; }
  uint8_t lastSyncByte() const { return syncByte_; }
  // Byte received after the echo in the last exchange (ACK / data / reply),
  // also when the exchange failed - tells "wrong ACK" from "no answer".
  uint8_t lastReply() const { return lastReply_; }
  uint8_t lastTx() const { return lastTx_; }

  ByteLink& link() { return link_; }

 private:
  LinkStatus fail(LinkStatus st);
  LinkStatus xchg(uint8_t tx, Parity par, TracePhase ph, CommandRisk risk, uint8_t& resp);

  ByteLink& link_;
  TraceLog& trace_;
  LinkTiming timing_;
  SessionState state_ = SessionState::Disconnected;
  LinkStatus lastError_ = LinkStatus::Ok;
  uint8_t page_ = 0;
  bool pageValid_ = false;
  bool pageIsWrite_ = false;
  uint32_t writeToken_ = 0;
  bool experimentalReads_ = false;
  bool vdoReads_ = false;
  uint32_t txCount_ = 0;
  uint32_t errCount_ = 0;
  uint32_t lastActivityMs_ = 0;
  uint32_t connectedAtMs_ = 0;
  uint8_t syncEcho_ = 0;
  uint8_t syncByte_ = 0;
  bool lineUsed_ = false;       // bytes were sent since boot
  uint32_t lastLineMs_ = 0;     // end of the last byte/wake-up on the line
  uint8_t wakeFails_ = 0;       // failed wake-ups in a row
  uint32_t resyncCount_ = 0;
  bool lastWasWrite_ = false;   // last prefix was a write page (no resync)
  uint8_t lastReply_ = 0;
  uint8_t lastTx_ = 0;
};
