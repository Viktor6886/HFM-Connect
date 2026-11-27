// ByteLink.h - abstract half-duplex HFM K-Line byte transport.
//
// Implementations:
//   KLineUart (ESP32-S3 UART + external transceiver, Arduino only)
//   SimEcu    (software ECU model, host tests only: tests/host/SimEcu.*)
//   ReplayLink (tests/host: replays a recorded trace)
#pragma once
#include "HfmTypes.h"
#include "EngineSignals.h"

// Timing profile of the link layer. Defaults are profile "R" from REV 1.1
// (BREAK 177 ms, post-break 214 ms, 2-byte read deadline 750 ms). They are reconstructed values (REK), not factory data.
struct LinkTiming {
  uint16_t breakMs = 177;
  uint16_t postBreakMs = 214;
  uint16_t byteDeadlineMs = 750;  // per echo+response pair
  uint16_t syncRetries = 3;       // up to 3 initialisation attempts
  uint16_t syncRetryPauseMs = 1000;
  // Line silence before a new BREAK once bytes were sent (D-027): an ECU still
  // in its diagnostic session treats BREAK + AA as session bytes and never
  // answers 55; the PC tester keeps a session alive with a byte every 1300 ms,
  // so a longer silence lets the ECU drop the session first. Grows by 1 s per
  // failed wake-up (up to +4 s). GIP: the ECU session timeout is not documented.
  uint16_t resyncSilenceMs = 2500;
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

  // Raw 8N1 transport for framed protocols; independent of the KS-3 echo/reply pair.
  virtual bool frameBaud(uint32_t) { return false; }
  virtual bool frameBreak(bool) { return false; }
  virtual bool frameWrite(uint8_t) { return false; }
  virtual bool frameRead(uint8_t&, uint16_t) { return false; }
  virtual void frameDrain() {}
  virtual bool supportsDuty() const {return false;}
  virtual bool sampleDuty(scanner::DutySample&) {return false;}
  virtual bool supportsKeBlink() const {return false;}
  virtual uint8_t readKeBlink(bool& idle,bool /*senseIdle*/) {idle=false;return 0;}

  // Number of actual echo/reply bytes from the last exchange; -1 for old replay adapters.
  virtual int receiveCount() const { return -1; }
  virtual void sleepMs(uint32_t ms) = 0;
  virtual uint32_t nowMs() = 0;
  // Monotonic microseconds for trace timestamps.
  virtual uint32_t nowUs() = 0;

  // Human readable transport name for traces ("kline-uart1", "sim", "replay").
  virtual const char* name() const = 0;
};
