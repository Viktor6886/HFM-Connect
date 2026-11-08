// ByteLink.h - abstract half-duplex HFM K-Line byte transport.
//
// Implementations:
//   KLineUart (ESP32-S3 UART + external transceiver, Arduino only)
//   SimEcu    (software ECU model for host tests and the on-device simulator)
//   ReplayLink (tests/host: replays a recorded trace)
#pragma once
#include "HfmTypes.h"

// Timing profile of the link layer. Defaults are profile "R" from REV 1.1
// (HFMreader serial_transport: BREAK 177 ms, post-break 214 ms, 2-byte read
// deadline 750 ms). They are reconstructed values (REK), not factory data.
struct LinkTiming {
  uint16_t breakMs = 177;
  uint16_t postBreakMs = 214;
  uint16_t byteDeadlineMs = 750;  // per echo+response pair
  uint16_t syncRetries = 3;       // HFMreader reader_service: up to 3 inits
  uint16_t syncRetryPauseMs = 1000;
};

class ByteLink {
 public:
  virtual ~ByteLink() {}

  // Open/prepare the transport. Returns false on hardware failure.
  virtual bool open() = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;

  // BREAK (line low for timing.breakMs), pause, TX 0xAA (space parity),
  // expect RX echo 0xAA followed by ECU 0x55.
  // echo/sync receive the actually observed bytes (0 if not received).
  virtual LinkStatus wakeup(const LinkTiming& t, uint8_t& echo, uint8_t& sync) = 0;

  // Transmit one byte with the given parity and receive exactly two bytes:
  // the transceiver echo and the ECU response.
  virtual LinkStatus exchange(uint8_t tx, Parity p, uint16_t deadlineMs, uint8_t& echo,
                              uint8_t& resp) = 0;

  virtual void sleepMs(uint32_t ms) = 0;
  virtual uint32_t nowMs() = 0;
  // Monotonic microseconds for trace timestamps.
  virtual uint32_t nowUs() = 0;

  // Human readable transport name for traces ("kline-uart1", "sim", "replay").
  virtual const char* name() const = 0;
};
