// K-Line wake-up (KLineWake): sequence and timing of the PC tester that works
// with the FT232RL KKL adapter, on a simulated line with a virtual clock.
#include <deque>
#include <functional>
#include <vector>

#include "KLineWake.h"
#include "minitest.h"

using namespace kwake;

namespace {

struct Ev {
  uint32_t t;
  uint8_t b;
};

// Simulated adapter + ECU. The adapter echoes every byte 2 ms later and
// receives its own BREAK as one 00 byte; `ecu` returns the ECU answer to a
// transmitted byte (-1 = silent) given how many bytes were sent so far.
struct FakeLine : LineOps {
  uint32_t t = 1000;
  std::deque<Ev> rx;
  std::vector<Ev> tx;
  std::vector<uint32_t> breakOn, breakOff;
  uint8_t modem = 0xFF;
  uint32_t flags = 0;
  bool breakChar = true, echo = true, failBreak = false;
  std::function<int(uint8_t, size_t)> ecu = [](uint8_t, size_t) { return -1; };

  void push(uint32_t at, uint8_t b) {
    auto it = rx.begin();
    while (it != rx.end() && it->t <= at) ++it;
    rx.insert(it, Ev{at, b});
  }
  bool setModem(uint8_t m) override {
    modem = m;
    return true;
  }
  bool setBreak(bool on) override {
    if (failBreak) return false;
    if (on) {
      breakOn.push_back(t);
      flags++;
      if (breakChar) push(t + 1, 0x00);
    } else {
      breakOff.push_back(t);
    }
    return true;
  }
  bool setSpace9600() override { return true; }
  bool write(uint8_t b) override {
    tx.push_back(Ev{t, b});
    if (echo) push(t + 2, b);
    int r = ecu(b, tx.size());
    if (r >= 0) push(t + 5, (uint8_t)r);
    return true;
  }
  int read(uint8_t& b, uint32_t timeoutMs) override {
    if (!rx.empty() && rx.front().t <= t + timeoutMs) {
      if (rx.front().t > t) t = rx.front().t;
      b = rx.front().b;
      rx.pop_front();
      return 1;
    }
    t += timeoutMs;
    return 0;
  }
  uint32_t breakFlags() override { return flags; }
  uint32_t now() override { return t; }
  void sleep(uint32_t ms) override { t += ms; }
};

}  // namespace

TEST(wake_aa_at_390_ms_and_ecu_answer) {
  FakeLine io;
  io.ecu = [](uint8_t b, size_t) { return b == 0xAA ? 0x55 : -1; };
  Diag d;
  Profile p;
  CHECK(run(io, p, kAaAtMs, d) == LinkStatus::Ok);
  CHECK(d.ok && d.echo && d.step == 1 && d.ecuByte == 0x55);
  CHECK(io.modem == 0);                          // DTR/RTS deasserted
  CHECK(io.breakOff[0] - io.breakOn[0] == 177);  // BREAK length
  CHECK(io.tx.size() == 1 && io.tx[0].b == 0xAA);
  CHECK(io.tx[0].t - io.breakOn[0] == 390);      // AA at start of BREAK + 390 ms
  CHECK(d.aaAtMs == 390);
  CHECK(d.breakOnRx);                            // BREAK came back as 00
  CHECK(d.rxCount >= 3 && d.rx[0] == 0x00);
}

TEST(wake_silent_ecu_sends_aa_00_00_aa) {
  FakeLine io;
  Diag d;
  Profile p;
  CHECK(run(io, p, kAaAtMs, d) == LinkStatus::NoSync);
  CHECK(io.tx.size() == 4);
  static const uint8_t seq[4] = {0xAA, 0x00, 0x00, 0xAA};
  for (size_t i = 0; i < io.tx.size() && i < 4; i++) {
    CHECK(io.tx[i].b == seq[i]);
    CHECK(io.tx[i].t - io.breakOn[0] == 390 + 1000 * i);  // one BREAK, bytes 1 s apart
  }
  CHECK(io.breakOn.size() == 1);
  CHECK(d.echo && !d.ok && d.ecuByte == 0);
}

TEST(wake_answer_to_the_second_aa) {
  FakeLine io;
  io.ecu = [](uint8_t b, size_t n) { return b == 0xAA && n == 4 ? 0x55 : -1; };
  Diag d;
  Profile p;
  CHECK(run(io, p, kAaAtMs, d) == LinkStatus::Ok);
  CHECK(d.step == 4);
}

TEST(wake_ff_to_second_00_sends_aa_at_once) {
  FakeLine io;
  io.ecu = [](uint8_t b, size_t) { return b == 0x00 ? 0xFF : -1; };
  Diag d;
  Profile p;
  CHECK(run(io, p, kAaAtMs, d) == LinkStatus::NoSync);
  CHECK(io.tx.size() == 4);
  if (io.tx.size() == 4) CHECK(io.tx[3].t - io.tx[2].t < 100);
}

TEST(wake_without_adapter_echo_and_without_break_char) {
  FakeLine io;
  io.echo = false;
  io.breakChar = false;
  io.ecu = [](uint8_t b, size_t) { return b == 0xAA ? 0x55 : -1; };
  Diag d;
  Profile p;
  CHECK(run(io, p, kAaAtMs, d) == LinkStatus::Ok);
  CHECK(!d.echo && d.ok);
  CHECK(d.breakOnRx);  // still seen through the BREAK flag of the line status
}

TEST(wake_profile_break_and_modem_applied) {
  FakeLine io;
  Diag d;
  Profile p;
  p.breakMs = 130;
  p.modem = kDtr | kRts;
  run(io, p, kAaAtMs, d);
  CHECK(io.modem == 3);
  CHECK(io.breakOff[0] - io.breakOn[0] == 130);
  CHECK(io.tx[0].t - io.breakOn[0] == 390);  // AA time does not depend on the BREAK length
}

TEST(wake_link_down) {
  FakeLine io;
  io.failBreak = true;
  Diag d;
  CHECK(run(io, Profile(), kAaAtMs, d) == LinkStatus::LinkDown);
}

TEST(wake_planner_order_rotation_and_lock) {
  Planner pl;
  Profile user;
  pl.configure(user, true);
  CHECK(pl.count() == 7);
  CHECK(pl.current().breakMs == 177 && pl.current().modem == 0);
  pl.result(false);
  CHECK(pl.current().breakMs == 165 && pl.current().modem == 0);
  pl.result(false);
  CHECK(pl.current().breakMs == 160);
  pl.result(false);
  CHECK(pl.current().breakMs == 130);
  pl.result(false);
  CHECK(pl.current().breakMs == 177 && pl.current().modem == 3);
  pl.result(true);  // works: kept
  pl.result(false); // one failure keeps it
  CHECK(pl.current().modem == 3);
  pl.result(false);
  CHECK(pl.current().modem == kDtr);
  pl.configure(user, true);  // same settings: position kept
  CHECK(pl.current().modem == kDtr);
  Planner fixed;
  fixed.configure(user, false);
  fixed.result(false);
  CHECK(fixed.count() == 1 && fixed.current().breakMs == 177);
}
