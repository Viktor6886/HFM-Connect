// KLineUart.h - ByteLink over the ESP32-S3 UART and an external K-Line
// transceiver. Arduino/ESP-IDF only.
//
//  * 9600 Bd, 8 data bits, 1 stop bit.
//  * Parity "none" (8N1) for prefixes; "space" (8S1) emulated per byte by
//    choosing EVEN or ODD so that the parity bit is 0 (ESP32 has no SPACE).
//  * BREAK: TX line held low by inverting the TXD signal for breakMs
//    (uart_write_bytes_with_break is limited to 255 bit times = 26.6 ms).
//  * After each TX the receiver is switched to EVEN parity so that both
//    10-bit (8N1) and 11-bit (8S1/8E1) ECU frames are received; parity and
//    framing error events are counted and exposed as evidence, never used to
//    discard bytes (HFMreader ignores RX parity as well).
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <driver/uart.h>

#include "ByteLink.h"
#include "ImpulseDecoder.h"

class KLineUart : public ByteLink {
 public:
  KLineUart(int uartNum, int txPin, int rxPin, bool txInv, bool rxInv);

  bool open() override;
  void close() override;
  bool isOpen() const override { return open_; }
  LinkStatus wakeup(const LinkTiming& t, uint8_t& echo, uint8_t& sync) override;
  LinkStatus exchange(uint8_t tx, Parity p, uint16_t deadlineMs, uint8_t& echo, uint8_t& resp) override;
  void sleepMs(uint32_t ms) override { delay(ms); }
  uint32_t nowMs() override { return millis(); }
  uint32_t nowUs() override { return micros(); }
  const char* name() const override { return "kline-uart"; }

  // Raw line level of RX (1 = idle/high) for diagnostics and impulse mode.
  int rxLevel() const;
  // Pulse-counter procedure: hold line low pressMs, then record edges.
  uint16_t impulseCapture(const ImpulseConfig& cfg, ImpulseEdge* edges, uint16_t maxEdges);

  uint32_t parityErrors() const { return parityErr_; }
  uint32_t frameErrors() const { return frameErr_; }
  uint32_t breaksSent() const { return breaks_; }

 private:
  void setParity(uart_parity_t p);
  void lineLow(bool low);
  void drainEvents();

  uart_port_t port_;
  int tx_, rx_;
  bool txInv_, rxInv_;
  bool open_ = false;
  QueueHandle_t evq_ = nullptr;
  uart_parity_t parity_ = UART_PARITY_DISABLE;
  uint32_t parityErr_ = 0, frameErr_ = 0, breaks_ = 0;
};
#endif
