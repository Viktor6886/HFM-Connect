#include "KLineWake.h"

namespace kwake {

namespace {

void record(Diag& d, uint8_t b) {
  if (d.rxCount < sizeof d.rx) d.rx[d.rxCount++] = b;
}

// Receive until `until`; bytes are only recorded (the line should be idle).
// Returns false when the adapter is gone.
bool drain(LineOps& io, Diag& d, uint32_t until) {
  for (;;) {
    uint32_t now = io.now();
    if ((int32_t)(until - now) <= 0) return true;
    uint8_t b = 0;
    int r = io.read(b, until - now);
    if (r < 0) return false;
    if (r == 1) {
      record(d, b);
      if (b == 0x00) d.breakOnRx = true;  // the BREAK itself, received as 00
    }
  }
}

}  // namespace

LinkStatus run(LineOps& io, const Profile& p, uint16_t aaAtMs, Diag& d) {
  d = Diag();
  d.profile = p;
  static const uint8_t kSeq[4] = {0xAA, 0x00, 0x00, 0xAA};
  if (!io.setModem(p.modem)) return LinkStatus::LinkDown;
  uint32_t bi0 = io.breakFlags();
  uint32_t t0 = io.now();
  if (!io.setBreak(true)) return LinkStatus::LinkDown;
  io.sleep(p.breakMs);
  if (!io.setBreak(false)) return LinkStatus::LinkDown;
  d.breakActualMs = (uint16_t)(io.now() - t0);
  if (!io.setSpace9600()) return LinkStatus::LinkDown;
  uint32_t deadline = t0 + aaAtMs;
  if ((int32_t)(deadline - io.now()) < 10) deadline = io.now() + 10;

  for (uint8_t step = 0; step < 4; step++) {
    if (!drain(io, d, deadline)) return LinkStatus::LinkDown;
    uint8_t x = kSeq[step];
    if (!io.write(x)) return LinkStatus::LinkDown;
    if (d.txCount < sizeof d.tx) d.tx[d.txCount++] = x;
    if (step == 0) d.aaAtMs = (uint16_t)(io.now() - t0);
    bool echo = false;
    int resp = -1;
    uint32_t end = io.now() + kReplyMs;
    for (;;) {
      uint32_t now = io.now();
      if ((int32_t)(end - now) <= 0) break;
      uint8_t b = 0;
      int r = io.read(b, end - now);
      if (r < 0) return LinkStatus::LinkDown;
      if (r == 0) break;
      record(d, b);
      if (!echo && b == x) {
        echo = true;
        continue;
      }
      if (!echo && x == 0xAA && b == 0x00) {  // reception of the BREAK, ignored by the tester
        d.breakOnRx = true;
        continue;
      }
      resp = b;  // first byte after the echo (or without an echo)
      break;
    }
    if (x == 0xAA) {
      if (echo) d.echo = true;
      d.ecuByte = resp >= 0 ? (uint8_t)resp : 0;
      if (resp == 0x55) {
        d.ok = true;
        d.step = (uint8_t)(step + 1);
        if (io.breakFlags() != bi0) d.breakOnRx = true;
        return LinkStatus::Ok;
      }
    } else if (step == 2 && resp == 0xFF) {
      deadline -= kStepMs;  // the second 00 was answered: send the final AA at once
    }
    deadline += kStepMs;
  }
  if (io.breakFlags() != bi0) d.breakOnRx = true;
  return LinkStatus::NoSync;
}

void Planner::add(uint16_t breakMs, uint8_t modem) {
  for (uint8_t i = 0; i < n_; i++)
    if (list_[i].breakMs == breakMs && list_[i].modem == modem) return;
  if (n_ >= sizeof list_ / sizeof list_[0]) return;
  list_[n_].breakMs = breakMs;
  list_[n_].modem = modem;
  n_++;
}

void Planner::configure(const Profile& user, bool autoTune) {
  if (n_ && list_[0].breakMs == user.breakMs && list_[0].modem == user.modem && auto_ == autoTune) return;
  n_ = idx_ = 0;
  locked_ = false;
  auto_ = autoTune;
  add(user.breakMs, user.modem);
  if (!autoTune) return;
  static const uint16_t kBreaks[] = {165, 160, 130, 177};
  for (uint16_t b : kBreaks) add(b, user.modem);
  static const uint8_t kModems[] = {kDtr | kRts, kDtr, kRts, 0};
  for (uint8_t m : kModems) add(user.breakMs, m);
}

void Planner::result(bool ok) {
  if (ok) {
    locked_ = true;
    return;
  }
  if (locked_) {  // one failure (ignition off, unplugged) does not drop a working profile
    locked_ = false;
    return;
  }
  if (auto_ && n_) idx_ = (uint8_t)((idx_ + 1) % n_);
}

}  // namespace kwake
