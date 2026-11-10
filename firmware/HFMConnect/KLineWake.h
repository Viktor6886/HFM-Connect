// KLineWake.h - HFM K-Line wake-up for byte-level adapters (FTDI KKL cables).
// Platform independent: the transport supplies the line operations.
//
// The sequence follows the PC tester that works with the owner's FT232RL KKL
// adapter (its connect routine was analysed, see docs/PROTOCOL.md §2.1):
//   * BREAK, then 9600 Bd with SPACE parity applied again;
//   * AA is sent at a fixed time from the start of BREAK (390 ms);
//   * the adapter echoes AA, the ECU answers with the inverted byte 55;
//   * without an answer the tester keeps the line awake without a new BREAK:
//     00 and 00 one second apart, then AA again (each answer awaited 800 ms);
//   * a 00 received first is the adapter's own reception of the BREAK.
// The modem lines DTR/RTS stay deasserted (the PC tester does not touch them,
// another PC tool opens the port with DTR_CONTROL_DISABLE / RTS_CONTROL_DISABLE).
#pragma once
#include <stdint.h>

#include "HfmTypes.h"

namespace kwake {

// Modem line bits for the adapter (FTDI SIO_MODEM_CTRL).
enum : uint8_t { kDtr = 1, kRts = 2 };

struct Profile {
  uint16_t breakMs = 177;
  uint8_t modem = 0;  // kDtr | kRts asserted; 0 = both deasserted (reference tools)
};

// Line operations of the transport. All calls block; read() returns 1 for a
// byte, 0 on timeout and -1 when the adapter is gone.
class LineOps {
 public:
  virtual ~LineOps() {}
  virtual bool setModem(uint8_t modem) = 0;
  virtual bool setBreak(bool on) = 0;     // 8N1 with/without BREAK
  virtual bool setSpace9600() = 0;        // 9600 Bd, 8 data bits, SPACE parity
  virtual bool write(uint8_t b) = 0;
  virtual int read(uint8_t& b, uint32_t timeoutMs) = 0;
  virtual uint32_t breakFlags() = 0;      // count of received BREAK conditions (line status)
  virtual uint32_t now() = 0;             // ms
  virtual void sleep(uint32_t ms) = 0;
};

// What one wake-up attempt observed on the line.
struct Diag {
  Profile profile;
  uint16_t breakActualMs = 0;
  bool breakOnRx = false;   // the adapter received the BREAK (00 byte or BREAK flag)
  bool echo = false;        // AA came back from the adapter
  bool ok = false;          // ECU answered 55
  uint8_t step = 0;         // 1..4 = byte of the AA 00 00 AA sequence that got the answer
  uint8_t ecuByte = 0;      // first byte after the echo of the last AA (0 = none)
  uint16_t aaAtMs = 0;      // time of the first AA from the start of BREAK
  uint8_t rx[24] = {0};     // bytes received during the attempt (in order)
  uint8_t rxCount = 0;
  uint8_t tx[4] = {0};      // bytes sent after the BREAK
  uint8_t txCount = 0;
};

static const uint16_t kAaAtMs = 390;      // AA after the start of BREAK (PC tester: t0 + 0x186)
static const uint16_t kStepMs = 1000;     // next byte of the sequence (0x3e8)
static const uint16_t kReplyMs = 800;     // answer window per byte (0x320)

// One wake-up attempt. aaAtMs: time of the first AA from the start of BREAK.
LinkStatus run(LineOps& io, const Profile& p, uint16_t aaAtMs, Diag& d);

// Order of profiles tried while the ECU stays silent. The configured profile
// comes first; then the BREAK lengths of the PC tester (0xa5 = 165 ms default,
// 0xa0 = 160 ms, 0x82 = 130 ms option), then DTR/RTS variants. A profile that
// woke the ECU is kept.
class Planner {
 public:
  void configure(const Profile& user, bool autoTune);
  const Profile& current() const { return list_[idx_]; }
  void result(bool ok);
  bool autoTune() const { return auto_; }
  bool locked() const { return locked_; }
  uint8_t index() const { return idx_; }
  uint8_t count() const { return n_; }

 private:
  void add(uint16_t breakMs, uint8_t modem);
  Profile list_[10];
  uint8_t n_ = 0, idx_ = 0;
  bool auto_ = true, locked_ = false;
};

}  // namespace kwake
