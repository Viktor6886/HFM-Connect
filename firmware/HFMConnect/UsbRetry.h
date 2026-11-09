#pragma once
#include <stdint.h>

// All scheduling runs on the USB client task. A single enumeration failure
// keeps recovery armed until a usable adapter appears or the root is unplugged.
struct UsbRetry {
  static constexpr unsigned quickAttempts = 5;
  bool armed = false;
  uint32_t since = 0, interval = 0;
  void failed(uint32_t now) {
    if (armed) return;
    armed = true;
    since = now;
    interval = 1500;
  }
  void attempted(uint32_t now, unsigned count) {
    armed = true;
    since = now;
    interval = count < quickAttempts ? 6000 : 30000;
  }
  bool due(uint32_t now) const { return armed && now - since >= interval; }
};

// An event requests an immediate scan. Polling also catches addresses that
// appear later, even if the stack never delivers another NEW_DEV event.
struct UsbAddressScan {
  bool requested = true;
  uint32_t last = 0;
  bool due(uint32_t now) const { return requested || now - last >= 1000; }
  void started(uint32_t now) {
    requested = false;
    last = now;
  }
};
