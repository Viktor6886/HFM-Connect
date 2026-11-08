// TraceLog.h - RAW TX/RX journal (ring buffer) of every K-Line byte exchange.
// Every read creates an immutable RAW event before any decoding (REV 1.4 s.59).
#pragma once
#include "HfmTypes.h"
#include "JsonWriter.h"

struct TraceEvent {
  uint32_t seq;       // monotonically increasing event number
  uint32_t tUs;       // transport monotonic microseconds
  uint8_t phase;      // TracePhase
  uint8_t parity;     // Parity
  uint8_t tx;         // transmitted byte (or 0 for BREAK/INFO)
  uint8_t echo;       // received echo
  uint8_t resp;       // received response / data
  uint8_t status;     // LinkStatus
  uint8_t risk;       // CommandRisk
  uint8_t rxCount;    // number of bytes actually received (0..2)
};

class TraceLog {
 public:
  static const uint16_t kCapacity = 1536;

  TraceLog() { clear(); }

  void clear() {
    head_ = 0;
    count_ = 0;
  }

  void setEnabled(bool en) { enabled_ = en; }
  bool enabled() const { return enabled_; }

  // Observer invoked for every appended event (used for SD/flash capture).
  typedef void (*Observer)(const TraceEvent& e, void* ctx);
  void setObserver(Observer o, void* ctx) { obs_ = o; obsCtx_ = ctx; }

  void append(uint32_t tUs, TracePhase ph, Parity par, uint8_t tx, uint8_t echo, uint8_t resp,
              LinkStatus st, CommandRisk risk, uint8_t rxCount);

  uint32_t lastSeq() const { return nextSeq_ - 1; }
  uint16_t count() const { return count_; }

  // Returns events with seq > afterSeq, oldest first, at most max events.
  uint16_t copySince(uint32_t afterSeq, TraceEvent* out, uint16_t max) const;

  // Serialise events with seq > afterSeq as a JSON array.
  void writeJson(JsonWriter& w, uint32_t afterSeq, uint16_t max) const;

  // One JSONL line per event (REV 1.4 s.20 field set, reduced).
  static void writeJsonl(std::string& out, const TraceEvent& e, const char* sessionId,
                         const char* transport, const char* ecuProfile);

  uint32_t errorCount() const { return errors_; }

  // Optional lock hooks (the engine task appends while HTTP handlers read).
  typedef void (*LockFn)(void* ctx);
  void setLock(LockFn lock, LockFn unlock, void* ctx) { lock_ = lock; unlock_ = unlock; lockCtx_ = ctx; }

 private:
  void lk() const { if (lock_) lock_(lockCtx_); }
  void ulk() const { if (unlock_) unlock_(lockCtx_); }
  LockFn lock_ = nullptr;
  LockFn unlock_ = nullptr;
  void* lockCtx_ = nullptr;
  TraceEvent buf_[kCapacity];
  uint16_t head_ = 0;   // index of next write
  uint16_t count_ = 0;
  uint32_t nextSeq_ = 1;
  uint32_t errors_ = 0;
  bool enabled_ = true;
  Observer obs_ = nullptr;
  void* obsCtx_ = nullptr;
};
