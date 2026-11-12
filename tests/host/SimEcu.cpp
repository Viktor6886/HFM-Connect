#include "SimEcu.h"

#include <math.h>
#include <string.h>

// Synthetic identification block (reference ECU layout):
// BOSCH, "M2 28320 VR0", 013 545 13 32, HW 35/92, SW 46/92, diag 41/00.
static const uint8_t kGoldenIdent[28] = {0x2C, 0x42, 0x4D, 0x32, 0x32, 0x38, 0x33, 0x32, 0x30, 0x56,
                                         0x52, 0x30, 0x30, 0x31, 0x33, 0x35, 0x34, 0x35, 0x31, 0x33,
                                         0x33, 0x32, 0x35, 0x92, 0x46, 0x92, 0x41, 0x00};

SimEcu::SimEcu() {
  memcpy(ident_, kGoldenIdent, 28);
  memset(dtc_, 0, sizeof dtc_);
}

void SimEcu::setIdent(const uint8_t* raw28) { memcpy(ident_, raw28, 28); }

void SimEcu::setFaultByte(uint8_t index, uint8_t value) { dtc_[index & 0x3F] = value; }

void SimEcu::setPageByte(uint8_t page, uint8_t sel, uint8_t value) {
  for (uint8_t i = 0; i < ovN_; i++)
    if (ovPage_[i] == page && ovSel_[i] == sel) { ovVal_[i] = value; return; }
  if (ovN_ < 32) { ovPage_[ovN_] = page; ovSel_[ovN_] = sel; ovVal_[ovN_] = value; ovN_++; }
}

void SimEcu::setScenario(Scenario s) {
  scenario_ = s;
  startMs_ = modelMs();
  memset(dtc_, 0, sizeof dtc_);
  if (s == Scenario::Faults) {
    // Synthetic scenarios: bank A 00=0x40 (DTC 10),
    // oxygen-stored (02=0x08 -> DTC 24), plus maf-current (10=0x40).
    dtc_[0x00] = 0x40;
    dtc_[0x02] = 0x08;
    dtc_[0x10] = 0x40;
  }
}

LinkStatus SimEcu::wakeup(const LinkTiming& t, uint8_t& echo, uint8_t& sync) {
  uint32_t start = nowMs();
  sleepMs(t.breakMs + t.postBreakMs);
  if (!open_) return LinkStatus::LinkDown;
  if (inject_.sessionTimeoutMs && initialized_ && start - lastByteMs_ < inject_.sessionTimeoutMs) {
    // Still in session: BREAK and AA are read as session bytes.
    echo = 0xAA;
    sync = 0x2C;
    sleepMs(t.byteDeadlineMs);
    lastByteMs_ = nowMs();
    return LinkStatus::NoSync;
  }
  if (inject_.noSync) {
    echo = 0xAA;
    sync = 0;
    sleepMs(t.byteDeadlineMs);
    return LinkStatus::NoSync;
  }
  echo = 0xAA;
  sync = 0x55;
  initialized_ = true;
  lastByteMs_ = nowMs();
  pageSet_ = false;
  controlStage_ = 0;
  return LinkStatus::Ok;
}

static uint8_t clampU8(float v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)(v + 0.5f)); }

uint8_t SimEcu::pageValue(uint8_t page, uint8_t sel) {
  for (uint8_t i = 0; i < ovN_; i++)
    if (ovPage_[i] == page && ovSel_[i] == sel) return ovVal_[i];
  float t = (modelMs() - startMs_) / 1000.0f;
  bool running = scenario_ != Scenario::EngineOff;
  float wob = sinf(t * 2.1f) + 0.5f * sinf(t * 5.3f);
  // ECT raw: warm idle ~ 0xBF (79..80 °C native), cold start warms from 0x40.
  float ectRaw = scenario_ == Scenario::ColdStart ? 64 + (t * 1.2f > 130 ? 130 : t * 1.2f) : 196 + 0.5f * sinf(t * 0.1f);
  float rpm = running ? (scenario_ == Scenario::ColdStart && t < 40 ? 1100 : 750) + 15 * wob : 0;
  uint8_t rpmB = clampU8(rpm / 25.0f);
  float o2 = running ? 112 + 95 * sinf(t * 3.0f) : 110;  // raw*4 mV
  uint16_t maf = running ? (uint16_t)(170 + 6 * wob) : 0;  // raw16 /10 kg/h
  uint16_t inj = running ? (uint16_t)(1600 + 40 * wob) : 0;
  if (page == 0x20) {
    switch (sel) {
      case 0xB7: return rpmB;
      case 0x48: return (uint8_t)(maf & 0xFF);
      case 0x49: return (uint8_t)(maf >> 8);
      case 0x54: return (uint8_t)(inj & 0xFF);
      case 0x55: return (uint8_t)(inj >> 8);
      case 0xBD: return running ? clampU8(3.5f + 0.3f * wob) : 3;   // ~1.2°
      case 0xC2: return running ? clampU8(40 + 3 * wob) : 0;
      case 0xC4: return running ? clampU8(126 - 4 * wob) : 157;     // ~10.7°
      case 0xB9: return running ? clampU8(128 + 10 * sinf(t * 3.0f)) : 128;
      case 0x23: return clampU8(o2);
      case 0x25: return clampU8(running ? 140 : 40);
      case 0x99: return 0x00;
      case 0x68: return 0x00;
      case 0x47: return 0;
      case 0x67: return 30;   // 750 rpm idle target
      case 0x98: return running ? 0x80 : 0x80;
      default: return 0;
    }
  }
  if (page == 0x21) {
    switch (sel) {
      case 0xBF: return clampU8(ectRaw);
      case 0xBE: return clampU8(160 + 2 * sinf(t * 0.05f));
      case 0xB7: return rpmB;
      case 0xD7: return 30;
      case 0x69: return (scenario_ == Scenario::ColdStart && t < 60) ? 0x0C : 0x00;
      case 0x67: return 0x00;
      default: return 0;
    }
  }
  if (page == 0x24) {
    switch (sel) {
      case 0x90: return adaptReset_ ? 127 : adapt_[0];
      case 0x92: return adaptReset_ ? 128 : adapt_[1];
      case 0x93: return adaptReset_ ? 128 : adapt_[2];
      case 0xF0: return 0x00;  // configuration "not applicable" mask
      default: return 0;
    }
  }
  if (page == 0x27 && sel == 0x02) return 0x11;  // AT, 5 gears, CAT
  return 0;
}

LinkStatus SimEcu::exchange(uint8_t tx, Parity p, uint16_t deadlineMs, uint8_t& echo, uint8_t& resp) {
  echo = 0;
  resp = 0;
  if (!open_) return LinkStatus::LinkDown;
  int32_t n = (int32_t)count_++;
  if (inject_.sessionTimeoutMs && initialized_ && nowMs() - lastByteMs_ >= inject_.sessionTimeoutMs)
    initialized_ = false;  // session timed out
  clockUs_ += 3600;  // ~3 byte times at 9600 Bd + ECU latency
  lastByteMs_ = nowMs();
  if (!initialized_) {
    clockUs_ += (uint64_t)deadlineMs * 1000;
    return LinkStatus::Timeout;
  }
  if (n == inject_.timeoutAt) {
    echo = tx;
    clockUs_ += (uint64_t)deadlineMs * 1000;
    return LinkStatus::Timeout;
  }
  echo = (n == inject_.echoMismatchAt) ? (uint8_t)(tx ^ 0x10) : tx;

  if (p == Parity::None) {
    page_ = tx;
    pageSet_ = true;
    pageUsed_ = false;
    resp = (n == inject_.badAckAt) ? tx : (uint8_t)(tx ^ 0xFF);
    switch (tx) {
      case 0x10: case 0xF3: case 0xCF: case 0xC1: case 0xC0: case 0xE0:
      case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4:
        lastWritePrefix_ = tx;
        break;
      default:
        break;
    }
    return LinkStatus::Ok;
  }
  if (!pageSet_) {
    clockUs_ += (uint64_t)deadlineMs * 1000;
    return LinkStatus::Timeout;
  }
  if (forgetPage_ && pageUsed_) {
    resp = tx;  // page forgotten: the bare selector comes back as it was sent
    return LinkStatus::Ok;
  }
  pageUsed_ = true;
  switch (page_) {
    case 0xF0: resp = tx < 28 ? ident_[tx] : 0xFF; break;
    case 0xF1: resp = dtc_[tx & 0x3F]; break;
    case 0xF9: resp = 0xFF; break;  // VIN not stored
    case 0x10:
    case 0xF3:
      if (page_ == 0xF3 && tx == 0xFF) memset(dtc_, 0, sizeof dtc_);
      resp = (uint8_t)(tx ^ 0xFF);
      break;
    case 0xCF: case 0xC1: case 0xE0: case 0xC0:
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4:
      if (tx == 0x96) {
        resp = 0x40;  // "ready" bit
      } else {
        resp = (uint8_t)(tx ^ 0xFF);
        if (page_ == 0xC0 && tx == 0xFC) adaptReset_ = true;
        if (page_ == 0xD4) purgeOn_ = (tx == 0x1A);
      }
      break;
    default:
      resp = pageValue(page_, tx);
  }
  return LinkStatus::Ok;
}
