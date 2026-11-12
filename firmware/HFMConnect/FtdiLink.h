// FtdiLink.h - ByteLink over a USB K-Line adapter with an FTDI chip
// (FT232R/FT232RL "KKL" cables, FT-X), using the ESP32-S3 USB OTG port in
// host mode (ESP-IDF usb_host library from the Arduino core). Arduino only.
//
// The adapter is driven like the PC tester that works with it (D2XX calls
// FT_SetBaudRate / FT_SetDataCharacteristics / FT_SetBreakOn/Off, DTR and RTS
// left deasserted): 9600 Bd, 8 data bits, parity NONE for prefixes and SPACE
// for selectors (SIO_SET_DATA), BREAK via SIO_SET_DATA bit 14. Every bulk IN
// packet carries 2 status bytes (modem, line status with parity/framing/break
// flags) followed by the received data. The wake-up sequence itself is in
// KLineWake (platform independent, host-tested).
// The adapter is detected on plug-in (hot-plug supported, also behind a hub);
// the diagnostic engine connects automatically when it appears.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/stream_buffer.h>

#include <atomic>

#include "ByteLink.h"
#include "ImpulseDecoder.h"
#include "KLineWake.h"
#include "UsbRetry.h"
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
  // the devices behind it like a cable re-plug. Fast retries followed by 30 s intervals.
  static const int kMaxReenum = UsbRetry::quickAttempts;
  void requestReenumerate() { reenumRequest_ = true; }
  uint32_t reenumCount() const { return reenumCount_; }
  uint32_t enumFailures() const { return enumFailures_; }

  // Wake-up profile: configured BREAK length and DTR/RTS state, optionally
  // followed by the alternatives of kwake::Planner while the ECU is silent.
  // learnedBreakMs = 0: no learned profile (NVS).
  void setWakeConfig(uint16_t breakMs, uint8_t modem, bool autoTune, uint16_t learnedBreakMs = 0,
                     uint8_t learnedModem = 0);
  // Profile that woke the ECU (to be stored in NVS); false if none yet.
  bool goodProfile(kwake::Profile& p) const;
  // Last wake-up attempt (false before the first one) and the profile in use.
  bool lastWake(kwake::Diag& d) const;
  kwake::Profile wakeProfile() const;
  bool wakeLocked() const { return wakeLocked_; }

  // Pulse-counter mode (OEM Engine 104/111 11/5) through the adapter: BREAK
  // holds the K-Line low = the button of the pulse counter. A fall of the line
  // driven by the ECU reaches the UART as a 0x00 byte / break flag, so the
  // code N is the number of falls in one burst; the low time itself is not
  // visible (phases are stored fall-to-fall). Not verified on hardware.
  uint16_t impulseCapture(const ImpulseConfig& cfg, ImpulseEdge* edges, uint16_t maxEdges, ImpulseResult& res);
  bool impulseHold(uint32_t ms);
  uint32_t wakeAttempts() const { return wakeAttempts_; }

 private:
  // kwake::LineOps over this adapter (used inside wakeup(), under ioMtx_).
  class Line : public kwake::LineOps {
   public:
    explicit Line(FtdiLink& f) : f_(f) {}
    bool setModem(uint8_t modem) override;
    bool setBreak(bool on) override;
    bool setSpace9600() override;
    bool write(uint8_t b) override;
    int read(uint8_t& b, uint32_t timeoutMs) override;
    uint32_t breakFlags() override { return f_.breakSeen_; }
    uint32_t now() override { return millis(); }
    void sleep(uint32_t ms) override { delay(ms); }
   private:
    FtdiLink& f_;
  };
  bool setModemLines(uint8_t modem);
  bool setBaud9600();

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

  std::atomic<bool> hostReady_{false};
  std::atomic<bool> attached_ = false;
  std::atomic<bool> configured_ = false;
  std::atomic<bool> open_ = false;
  volatile bool rxActive_ = false;
  volatile bool gone_ = false;
  UsbAddressScan scan_;
  bool portOff_ = false;
  bool ifaceClaimed_ = false;
  bool ctrlPending_ = false, txPending_ = false;
  volatile bool ctrlOk_ = false;
  volatile bool txOk_ = false;

  uint8_t iface_ = 0;
  uint8_t epIn_ = 0x81, epOut_ = 0x02;
  uint16_t mpsIn_ = 64;
  uint16_t port_ = 1;  // FTDI "interface A" index for vendor requests
  uint16_t vid_ = 0, pid_ = 0, bcd_ = 0;
  std::atomic<uint32_t> attachCount_{0};
  esp_err_t installErr_ = ESP_OK;
  std::atomic<uint32_t> devEvents_{0};
  volatile uint16_t seenVid_ = 0, seenPid_ = 0;
  char note_[160] = {0};
  std::atomic<bool> reenumRequest_{false};
  std::atomic<uint32_t> enumFailures_{0};
  UsbRetry retry_;
  std::atomic<uint32_t> reenumCount_{0};  // automatic attempts since the adapter was last attached
  uint32_t stallSinceMs_ = 0;  // a device counted but not listed since (0 = none)
  SeenDevice seen_[kMaxSeen];
  int seenCount_ = 0;
  mutable portMUX_TYPE noteMux_ = portMUX_INITIALIZER_UNLOCKED;
  // Updated from the USB client task and the engine task, read by the UI.
  std::atomic<uint32_t> parityErr_{0}, frameErr_{0}, breakSeen_{0}, overrun_{0}, usbErr_{0};
  Parity curParity_ = Parity::None;
  bool curBreak_ = false;
  bool lineKnown_ = false;
  uint8_t modem_ = 0xFF;  // DTR/RTS state applied to the chip (0xFF = unknown)
  // Wake-up: requested configuration (any task) and planner (engine task only).
  kwake::Profile wakeUser_;
  kwake::Profile wakeLearned_;
  bool haveLearned_ = false;
  bool wakeAuto_ = true;
  std::atomic<bool> wakeConfigChanged_{true};
  kwake::Planner planner_;
  kwake::Diag lastWake_;
  bool haveWake_ = false;
  std::atomic<bool> wakeLocked_{false};
  std::atomic<uint32_t> wakeAttempts_{0};
  mutable portMUX_TYPE wakeMux_ = portMUX_INITIALIZER_UNLOCKED;
};
#endif
