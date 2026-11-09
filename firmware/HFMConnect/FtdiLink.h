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
  // USB diagnostics for "adapter not detected": host install result, number
  // of enumerated USB devices, the last device seen and why it was not used.
  esp_err_t installError() const { return installErr_; }
  int usbDevices() const;
  uint32_t deviceEvents() const { return devEvents_; }
  uint16_t seenVid() const { return seenVid_; }
  uint16_t seenPid() const { return seenPid_; }
  // Human-readable reason (Russian) why no adapter is attached; "" when attached.
  void note(char* out, size_t n) const;
  // Root port of the OTG controller: PHY routing and D+/D- line levels.
  struct PortInfo {
    bool phyOtg = false;      // internal PHY routed to USB OTG
    bool powered = false;     // HPRT.PrtPwr
    bool connected = false;   // HPRT.PrtConnSts: a device pull-up is seen
    bool enabled = false;     // HPRT.PrtEna: port reset done, device enumerating/enumerated
    uint8_t lineState = 0;    // HPRT.PrtLnSts: bit0 = D+, bit1 = D-
  };
  PortInfo portInfo() const;

  // Every enumerated USB device seen on the OTG port (hubs included), for the
  // diagnostics page: an adapter behind a hub must show up here.
  struct SeenDevice {
    uint8_t addr = 0;
    uint16_t vid = 0, pid = 0;
    uint8_t cls = 0;          // bDeviceClass (0x09 = hub)
    uint8_t speed = 0;        // usb_speed_t: 0 = low, 1 = full, 2 = high
    uint8_t hubPort = 0;      // port on the parent hub, 0 = root port
    bool behindHub = false;
  };
  static const int kMaxSeen = 6;
  int seenDevices(SeenDevice* out, int max) const;
  // Last USB messages: ESP-IDF USB stack errors (ESP_LOGE) and this driver's notes.
  static const int kLogLines = 16;
  static const int kLogLen = 120;
  int usbLog(char (*out)[kLogLen], int max) const;

  // Re-enumeration: ESP-IDF disables a hub port after one failed enumeration
  // (e.g. "CHECK_SHORT_DEV_DESC FAILED" -> "Port disabled") and never retries.
  // The root port is then powered off and on, which re-enumerates the hub and
  // the devices behind it like a cable re-plug. Automatic, limited attempts.
  static const int kMaxReenum = 5;
  void requestReenumerate() { reenumRequest_ = true; }
  uint32_t reenumCount() const { return reenumCount_; }
  uint32_t enumFailures() const { return enumFailures_; }

 private:
  static void hostTask(void* arg);
  static void clientTask(void* arg);
  static void clientEvent(const usb_host_client_event_msg_t* msg, void* arg);
  static void ctrlCb(usb_transfer_t* x);
  static void txCb(usb_transfer_t* x);
  static void rxCb(usb_transfer_t* x);

  void openDevice(uint8_t addr);
  void scanDevices();
  void reenumerate();
  void setNote(const char* fmt, ...);
  void remember(uint8_t addr, const usb_device_desc_t* dd, const usb_device_info_t* di);
  static void logLine(const char* text);
  static int logHook(const char* fmt, va_list ap);
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
  esp_err_t installErr_ = ESP_OK;
  std::atomic<uint32_t> devEvents_{0};
  volatile uint16_t seenVid_ = 0, seenPid_ = 0;
  int lastScanCount_ = -1;
  uint32_t lastScanMs_ = 0;
  char note_[160] = {0};
  std::atomic<bool> reenumRequest_{false};
  std::atomic<uint32_t> enumFailures_{0};
  uint32_t enumFailMs_ = 0;
  uint32_t reenumCount_ = 0;  // automatic attempts since the adapter was last attached
  uint32_t lastReenumMs_ = 0;
  uint32_t lastStallCheckMs_ = 0;
  uint32_t stallSinceMs_ = 0;  // a device counted but not listed since (0 = none)
  SeenDevice seen_[kMaxSeen];
  int seenCount_ = 0;
  mutable portMUX_TYPE noteMux_ = portMUX_INITIALIZER_UNLOCKED;
  // Updated from the USB client task and the engine task, read by the UI.
  std::atomic<uint32_t> parityErr_{0}, frameErr_{0}, breakSeen_{0}, overrun_{0}, usbErr_{0};
  Parity curParity_ = Parity::None;
  bool curBreak_ = false;
  bool lineKnown_ = false;
};
#endif
