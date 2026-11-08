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
//     page context and is never retried automatically.
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

class HfmSession {
 public:
  HfmSession(ByteLink& link, TraceLog& trace) : link_(link), trace_(trace) {}

  void setTiming(const LinkTiming& t) { timing_ = t; }
  const LinkTiming& timing() const { return timing_; }

  // Opens the link and performs BREAK/AA/55 with the configured retries.
  LinkStatus connect();
  void disconnect();

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

  // Statistics
  uint32_t txCount() const { return txCount_; }
  uint32_t errorCount() const { return errCount_; }
  uint32_t lastActivityMs() const { return lastActivityMs_; }
  uint32_t connectedAtMs() const { return connectedAtMs_; }
  uint8_t lastSyncEcho() const { return syncEcho_; }
  uint8_t lastSyncByte() const { return syncByte_; }

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
  uint32_t txCount_ = 0;
  uint32_t errCount_ = 0;
  uint32_t lastActivityMs_ = 0;
  uint32_t connectedAtMs_ = 0;
  uint8_t syncEcho_ = 0;
  uint8_t syncByte_ = 0;
};
