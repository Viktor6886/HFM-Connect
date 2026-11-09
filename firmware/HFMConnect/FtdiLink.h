// FtdiLink.h - ByteLink over a USB K-Line adapter with an FTDI chip
// (FT232R/FT232RL "KKL" cables, FT-X), using the ESP32-S3 USB OTG port in
// host mode (ESP-IDF usb_host library from the Arduino core). Arduino only.
//
// The FTDI chip provides exactly what the HFM line needs:
//   * 9600 Bd, 8 data bits, parity NONE for prefixes and SPACE for selectors
//     (SIO_SET_DATA, parity code 4) - no EVEN/ODD emulation needed;
//   * BREAK (SIO_SET_DATA bit 14) for the 177 ms wake-up;
//   * every bulk IN packet carries 2 status bytes (modem, line status with
//     parity/framing/break flags) followed by the received data.
// The adapter is detected on plug-in (hot-plug supported); the diagnostic
// engine connects automatically when it appears.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/stream_buffer.h>

#include <atomic>

#include "ByteLink.h"
#include "usb/usb_host.h"

class FtdiLink : public ByteLink {
 public:
  // Installs the USB host stack and starts its tasks (call once at boot).
  bool begin();

  bool open() override;
  void close() override;
  bool isOpen() const override { return open_ && attached_; }
  LinkStatus wakeup(const LinkTiming& t, uint8_t& echo, uint8_t& sync) override;
  LinkStatus exchange(uint8_t tx, Parity p, uint16_t deadlineMs, uint8_t& echo, uint8_t& resp) override;
  void sleepMs(uint32_t ms) override { delay(ms); }
  uint32_t nowMs() override { return millis(); }
  uint32_t nowUs() override { return micros(); }
  const char* name() const override { return "ftdi-usb"; }

  // Adapter state for the UI.
  bool hostReady() const { return hostReady_; }
  bool attached() const { return attached_; }
  bool configured() const { return configured_; }
  uint16_t vid() const { return vid_; }
  uint16_t pid() const { return pid_; }
  uint16_t bcdDevice() const { return bcd_; }
  const char* chipName() const;
  uint32_t attachCount() const { return attachCount_; }
  uint32_t parityErrors() const { return parityErr_; }
  uint32_t frameErrors() const { return frameErr_; }
  uint32_t breaksSeen() const { return breakSeen_; }
  uint32_t overruns() const { return overrun_; }
  uint32_t usbErrors() const { return usbErr_; }

 private:
  static void hostTask(void* arg);
  static void clientTask(void* arg);
  static void clientEvent(const usb_host_client_event_msg_t* msg, void* arg);
  static void ctrlCb(usb_transfer_t* x);
  static void txCb(usb_transfer_t* x);
  static void rxCb(usb_transfer_t* x);

  void openDevice(uint8_t addr);
  void closeDevice();
  bool configure();
  bool ctrl(uint8_t request, uint16_t value, uint16_t index);
  bool setLine(Parity p, bool brk);
  bool writeByte(uint8_t b);
  int readBytes(uint8_t* buf, int n, uint32_t timeoutMs);
  void purgeRx();

  usb_host_client_handle_t client_ = nullptr;
  usb_device_handle_t dev_ = nullptr;
  usb_transfer_t* ctrlX_ = nullptr;
  usb_transfer_t* txX_ = nullptr;
  usb_transfer_t* rxX_ = nullptr;
  SemaphoreHandle_t ctrlDone_ = nullptr;
  SemaphoreHandle_t txDone_ = nullptr;
  SemaphoreHandle_t ioMtx_ = nullptr;
  StreamBufferHandle_t rx_ = nullptr;

  volatile bool hostReady_ = false;
  volatile bool attached_ = false;
  volatile bool configured_ = false;
  volatile bool open_ = false;
  volatile bool rxActive_ = false;
  volatile bool gone_ = false;
  volatile int pendingAddr_ = -1;
  volatile bool ctrlOk_ = false;
  volatile bool txOk_ = false;

  uint8_t iface_ = 0;
  uint8_t epIn_ = 0x81, epOut_ = 0x02;
  uint16_t mpsIn_ = 64;
  uint16_t port_ = 1;  // FTDI "interface A" index for vendor requests
  uint16_t vid_ = 0, pid_ = 0, bcd_ = 0;
  uint32_t attachCount_ = 0;
  // Updated from the USB client task and the engine task, read by the UI.
  std::atomic<uint32_t> parityErr_{0}, frameErr_{0}, breakSeen_{0}, overrun_{0}, usbErr_{0};
  Parity curParity_ = Parity::None;
  bool curBreak_ = false;
  bool lineKnown_ = false;
};
#endif
