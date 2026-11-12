#ifdef ARDUINO
#include "FtdiLink.h"

#include <esp_log.h>
#include <stdarg.h>
#include <string.h>

#include "Config.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_dwc_struct.h"
#include "soc/usb_wrap_struct.h"

// FTDI SIO vendor requests (bmRequestType 0x40, host-to-device, vendor, device).
enum : uint8_t {
  SIO_RESET = 0,
  SIO_MODEM_CTRL = 1,
  SIO_SET_FLOW_CTRL = 2,
  SIO_SET_BAUDRATE = 3,
  SIO_SET_DATA = 4,
  SIO_SET_LATENCY_TIMER = 9,
};
static const uint16_t SIO_RESET_SIO = 0, SIO_RESET_PURGE_RX = 1, SIO_RESET_PURGE_TX = 2;
static const uint16_t FTDI_VID = 0x0403;

// Line status byte (second byte of each bulk IN packet).
static const uint8_t LSR_OE = 0x02, LSR_PE = 0x04, LSR_FE = 0x08, LSR_BI = 0x10;

// Multi-port chips (FT2232C/D, FT2232H, FT4232H) carry the port number in the
// baud-rate request index; single-port chips (FT232R, FT232H, FT-X) do not.
static bool isMultiPort(uint16_t bcd) { return bcd == 0x0500 || bcd == 0x0700 || bcd == 0x0800; }

// Divisor for the 3 MHz base clock (FT232R/FT232BM/FT-X): 9600 Bd -> 0x4138.
static uint32_t ftdiDivisor(uint32_t baud) {
  static const uint8_t frac[8] = {0, 3, 2, 4, 1, 5, 6, 7};
  uint32_t div8 = (3000000u * 8u + baud / 2) / baud;
  uint32_t enc = (div8 >> 3) | ((uint32_t)frac[div8 & 7] << 14);
  if (enc == 1) enc = 0;  // 3 Mbaud special case
  return enc;
}

const char* FtdiLink::chipName() const {
  if (!attached_) return "нет";
  switch (bcd_) {
    case 0x0400: return "FT232BM";
    case 0x0500: return "FT2232C";
    case 0x0600: return "FT232R";
    case 0x0700: return "FT2232H";
    case 0x0800: return "FT4232H";
    case 0x0900: return "FT232H";
    case 0x1000: return "FT-X";
    default: return "FTDI";
  }
}

// ESP-IDF log capture: boards like the ESP32-S3 Zero have no USB-UART bridge
// and their only USB port is taken by the host, so USB stack errors are kept
// for the web UI. Only ESP_LOGE is compiled into the Arduino core libraries.
static portMUX_TYPE gLogMux = portMUX_INITIALIZER_UNLOCKED;
static char gLog[FtdiLink::kLogLines][FtdiLink::kLogLen];
static uint32_t gLogHead = 0;  // total lines written
static vprintf_like_t gPrevVprintf = nullptr;
static std::atomic<bool> gEnumFailed{false};

// The event task must never wait for a caller which is waiting for its callback.
class IoGuard {
 public:
  explicit IoGuard(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
  ~IoGuard() { xSemaphoreGive(mutex_); }
 private:
  SemaphoreHandle_t mutex_;
};

void FtdiLink::logLine(const char* text) {
  char t[kLogLen];
  snprintf(t, sizeof t, "%lu %s", (unsigned long)(millis() / 1000), text);
  size_t n = strlen(t);
  while (n && (t[n - 1] == '\n' || t[n - 1] == '\r')) t[--n] = 0;
  if (!n) return;
  portENTER_CRITICAL(&gLogMux);
  memcpy(gLog[gLogHead % kLogLines], t, sizeof t);
  gLogHead++;
  portEXIT_CRITICAL(&gLogMux);
}

int FtdiLink::logHook(const char* fmt, va_list ap) {
  char t[kLogLen];
  va_list cp;
  va_copy(cp, ap);
  vsnprintf(t, sizeof t, fmt, cp);
  va_end(cp);
  // Keep USB stack messages only (tags of the ESP-IDF usb component).
  if (strstr(t, "USB") || strstr(t, "HUB") || strstr(t, "HCD") || strstr(t, "ENUM") || strstr(t, "EXT_") ||
      strstr(t, "usb") || strstr(t, "hub")) {
    logLine(t);
    // A port given up by the hub driver: schedule re-enumeration.
    if (strstr(t, "Port disabled") || strstr(t, "FAILED")) gEnumFailed = true;
  }
  return gPrevVprintf ? gPrevVprintf(fmt, ap) : 0;
}

int FtdiLink::usbLog(char (*out)[kLogLen], int max) const {
  portENTER_CRITICAL(&gLogMux);
  uint32_t head = gLogHead;
  int n = head < (uint32_t)kLogLines ? (int)head : kLogLines;
  if (n > max) n = max;
  for (int i = 0; i < n; i++) memcpy(out[i], gLog[(head - n + i) % kLogLines], kLogLen);
  portEXIT_CRITICAL(&gLogMux);
  return n;
}

int FtdiLink::seenDevices(SeenDevice* out, int max) const {
  portENTER_CRITICAL(&noteMux_);
  int n = seenCount_ < max ? seenCount_ : max;
  for (int i = 0; i < n; i++) out[i] = seen_[i];
  portEXIT_CRITICAL(&noteMux_);
  return n;
}

void FtdiLink::remember(uint8_t addr, const usb_device_desc_t* dd, const usb_device_info_t* di) {
  SeenDevice d;
  d.addr = addr;
  d.vid = dd->idVendor;
  d.pid = dd->idProduct;
  d.cls = dd->bDeviceClass;
  d.speed = di ? (uint8_t)di->speed : 0;
  d.behindHub = di && di->parent.dev_hdl;
  d.hubPort = di ? di->parent.port_num : 0;
  portENTER_CRITICAL(&noteMux_);
  int i = 0;
  while (i < seenCount_ && seen_[i].addr != addr) i++;
  bool changed = i == seenCount_ || seen_[i].vid != d.vid || seen_[i].pid != d.pid;
  if (i == seenCount_ && seenCount_ < kMaxSeen) seenCount_++;
  if (i < seenCount_) seen_[i] = d;
  portEXIT_CRITICAL(&noteMux_);
  if (!changed) return;
  char t[96];
  snprintf(t, sizeof t, "device addr %u %04X:%04X class %02X speed %u %s port %u", addr, d.vid, d.pid, d.cls, d.speed,
           d.behindHub ? "hub" : "root", d.hubPort);
  logLine(t);
}

void FtdiLink::setNote(const char* fmt, ...) {
  char t[sizeof note_];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(t, sizeof t, fmt, ap);
  va_end(ap);
  portENTER_CRITICAL(&noteMux_);
  bool changed = strcmp(note_, t) != 0;
  strlcpy(note_, t, sizeof note_);
  portEXIT_CRITICAL(&noteMux_);
  if (changed && t[0]) {
    Serial.printf("[usb] %s\n", t);
    logLine(t);
  }
}

void FtdiLink::note(char* out, size_t n) const {
  if (!n) return;
  if (attached_) {
    out[0] = 0;
    return;
  }
  if (!hostReady_) {
    snprintf(out, n, "USB-хост не запущен (%s)", esp_err_to_name(installErr_));
    return;
  }
  if (usbDevices() <= 0) {
    // Nothing enumerated: tell apart "no device on the wires" from a failed
    // enumeration using the root port state of the OTG controller.
    PortInfo pi = portInfo();
    if (!pi.phyOtg)
      strlcpy(out, "внутренний USB-PHY не переключён на OTG (ошибка прошивки/ядра)", n);
    else if (!pi.connected && pi.lineState == 0)
      strlcpy(out, "на D+/D− нет сигнала: у адаптера нет 5 В (VBUS) или он не в разъёме USB (OTG)", n);
    else
      snprintf(out, n, "устройство на D+/D− есть (линии %u), но не определилось — переподключите адаптер",
               pi.lineState);
    return;
  }
  portENTER_CRITICAL(&noteMux_);
  strlcpy(out, note_, n);
  portEXIT_CRITICAL(&noteMux_);
}

FtdiLink::PortInfo FtdiLink::portInfo() const {
  PortInfo pi;
  // Internal FSLS PHY routed to the OTG controller (USB Wrap): software mux
  // enabled and selected, pads enabled (ESP32-S3 TRM, RTC_CNTL_USB_CONF_REG).
  uint32_t uc = REG_READ(RTC_CNTL_USB_CONF_REG);
  pi.phyOtg = (uc & RTC_CNTL_SW_HW_USB_PHY_SEL) && (uc & RTC_CNTL_SW_USB_PHY_SEL) && USB_WRAP.otg_conf.pad_enable &&
              !USB_WRAP.otg_conf.phy_sel;
  if (hostReady_) {
    usb_dwc_hprt_reg_t h;
    h.val = USB_DWC.hprt_reg.val;
    pi.connected = h.prtconnsts;
    pi.enabled = h.prtena;
    pi.powered = h.prtpwr;
    pi.lineState = (uint8_t)h.prtlnsts;  // bit0 = D+, bit1 = D-
  }
  return pi;
}

int FtdiLink::usbDevices() const {
  if (!hostReady_) return -1;
  usb_host_lib_info_t info = {};
  if (usb_host_lib_info(&info) != ESP_OK) return -1;
  return info.num_devices;
}

bool FtdiLink::begin() {
  if (hostReady_) return true;
  ctrlDone_ = xSemaphoreCreateBinary();
  txDone_ = xSemaphoreCreateBinary();
  ioMtx_ = xSemaphoreCreateMutex();
  rx_ = xStreamBufferCreate(1024, 1);
  if (!rx_ || !ctrlDone_ || !txDone_ || !ioMtx_) {
    installErr_ = ESP_ERR_NO_MEM;
    return false;
  }
  gPrevVprintf = esp_log_set_vprintf(logHook);
  usb_host_config_t hc = {};
  hc.skip_phy_setup = false;
  hc.root_port_unpowered = true;  // Register the client before the first enumeration.
  hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
  installErr_ = usb_host_install(&hc);
  if (installErr_ != ESP_OK) {
    Serial.printf("[usb] usb_host_install: %s\n", esp_err_to_name(installErr_));
    logLine("usb_host_install failed");
    return false;
  }
  if (xTaskCreatePinnedToCore(hostTask, "usb-host", 4096, this, 5, nullptr, 0) != pdPASS) {
    installErr_ = ESP_ERR_NO_MEM;
    return false;
  }
  usb_host_client_config_t cc = {};
  cc.is_synchronous = false;
  cc.max_num_event_msg = 8;
  cc.async.client_event_callback = clientEvent;
  cc.async.callback_arg = this;
  installErr_ = usb_host_client_register(&cc, &client_);
  if (installErr_ != ESP_OK) {
    Serial.printf("[usb] usb_host_client_register: %s\n", esp_err_to_name(installErr_));
    return false;
  }
  if (xTaskCreatePinnedToCore(clientTask, "usb-ftdi", 4096, this, 5, nullptr, 0) != pdPASS) {
    installErr_ = ESP_ERR_NO_MEM;
    return false;
  }
  hostReady_ = true;
  logLine("USB host ready");
  Serial.println("[usb] USB host ready, waiting for the FTDI adapter on the USB (OTG) port");
  return true;
}

void FtdiLink::hostTask(void*) {
  for (;;) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

void FtdiLink::clientEvent(const usb_host_client_event_msg_t* msg, void* arg) {
  FtdiLink* self = static_cast<FtdiLink*>(arg);
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    self->devEvents_++;
    char t[40];
    snprintf(t, sizeof t, "NEW_DEV addr %u", msg->new_dev.address);
    logLine(t);
    self->scan_.requested = true;
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    if (self->dev_ && msg->dev_gone.dev_hdl == self->dev_) {
      self->gone_ = true;
      self->attached_ = false;
      self->configured_ = false;
    }
  }
}

void FtdiLink::clientTask(void* arg) {
  FtdiLink* self = static_cast<FtdiLink*>(arg);
  // Power the root port only after the hub and the adapter behind it have
  // settled: the first descriptor request to a device that has just powered
  // up behind a hub fails ("CHECK_SHORT_DEV_DESC FAILED", port disabled).
  vTaskDelay(pdMS_TO_TICKS(HFM_USB_START_DELAY_MS));
  if (usb_host_lib_set_root_port_power(true) != ESP_OK) {
    self->portOff_ = true;
    self->retry_.failed(millis());
    logLine("root port startup failed; retry scheduled");
  }
  for (;;) {
    usb_host_client_handle_events(self->client_, pdMS_TO_TICKS(20));
    if (self->gone_ && !self->rxActive_ && xSemaphoreTake(self->ioMtx_, 0) == pdTRUE) {
      self->closeDevice();
      xSemaphoreGive(self->ioMtx_);
    }
    const uint32_t now = millis();
    if (gEnumFailed.exchange(false)) {
      self->enumFailures_++;
      if (!self->dev_) self->retry_.failed(now);
    }
    // Always reconcile the actual address list, even when only the addresses
    // (or their readiness) changed and the device count stayed constant.
    if (!self->dev_ && self->scan_.due(now) &&
        xSemaphoreTake(self->ioMtx_, 0) == pdTRUE) {
      self->scan_.started(now);
      self->scanDevices();
      xSemaphoreGive(self->ioMtx_);
    }
    bool manual = self->reenumRequest_.exchange(false);
    if (!self->dev_ && (manual || self->retry_.due(millis()))) {
      auto port = self->portInfo();
      if (manual || self->portOff_ || port.connected || port.lineState) {
        self->reenumerate();
        self->retry_.attempted(millis(), self->reenumCount_);
      } else {
        self->retry_ = {};
        self->reenumCount_ = 0;
      }
    }
  }
}

void FtdiLink::reenumerate() {
  reenumCount_++;
  stallSinceMs_ = 0;
  char t[64];
  snprintf(t, sizeof t, "re-enumerate: root port power cycle %lu", (unsigned long)reenumCount_.load());
  logLine(t);
  if (!portOff_) {
    esp_err_t e = usb_host_lib_set_root_port_power(false);
    if (e != ESP_OK) {
      snprintf(t, sizeof t, "root port off: %s", esp_err_to_name(e));
      logLine(t);
      return;
    }
    portOff_ = true;
    for (int i = 0; i < 25; i++) usb_host_client_handle_events(client_, pdMS_TO_TICKS(20));
  }
  esp_err_t e = usb_host_lib_set_root_port_power(true);
  if (e != ESP_OK) {
    snprintf(t, sizeof t, "root port on: %s", esp_err_to_name(e));
    logLine(t);
    return;
  }
  portOff_ = false;
  scan_.requested = true;
  portENTER_CRITICAL(&noteMux_);
  seenCount_ = 0;
  portEXIT_CRITICAL(&noteMux_);
}

void FtdiLink::scanDevices() {
  uint8_t addrs[127];
  int cnt = 0;
  if (usb_host_device_addr_list_fill(sizeof addrs, addrs, &cnt) != ESP_OK) return;
  // The library counts a device before its address can be opened.
  const int devices = usbDevices();
  if (devices > cnt || (devices == 0 && cnt == 0 && portInfo().connected)) {
    uint32_t now = millis();
    if (!stallSinceMs_) stallSinceMs_ = now;
    if (now - stallSinceMs_ >= 4000) {
      stallSinceMs_ = now;
      logLine("enumeration stalled (no ready address)");
      enumFailures_++;
      retry_.failed(now);
    }
  } else {
    stallSinceMs_ = 0;
  }
  portENTER_CRITICAL(&noteMux_);
  for (int i = seenCount_ - 1; i >= 0; i--) {
    if (!memchr(addrs, seen_[i].addr, cnt)) seen_[i] = seen_[--seenCount_];
  }
  portEXIT_CRITICAL(&noteMux_);
  for (int i = 0; i < cnt && !dev_; i++) openDevice(addrs[i]);
}

void FtdiLink::openDevice(uint8_t addr) {
  usb_device_handle_t dev = nullptr;
  esp_err_t e = usb_host_device_open(client_, addr, &dev);
  if (e != ESP_OK) {
    setNote("USB-устройство (адрес %u) не открывается: %s", addr, esp_err_to_name(e));
    return;
  }
  const usb_device_desc_t* dd = nullptr;
  if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK) {
    setNote("USB-устройство (адрес %u): нет дескриптора", addr);
    usb_host_device_close(client_, dev);
    return;
  }
  seenVid_ = dd->idVendor;
  seenPid_ = dd->idProduct;
  usb_device_info_t di = {};
  remember(addr, dd, usb_host_device_info(dev, &di) == ESP_OK ? &di : nullptr);
  if (dd->idVendor != FTDI_VID) {
    if (dd->bDeviceClass == 9) setNote("USB-хаб %04X:%04X обнаружен, ожидание FTDI", dd->idVendor, dd->idProduct);
    else setNote("USB-устройство %04X:%04X, ожидание FTDI", dd->idVendor, dd->idProduct);
    usb_host_device_close(client_, dev);
    return;
  }
  const usb_config_desc_t* cfg = nullptr;
  if (usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK) {
    setNote("FTDI %04X:%04X: нет дескриптора конфигурации", dd->idVendor, dd->idProduct);
    usb_host_device_close(client_, dev);
    return;
  }
  // Walk the configuration: first interface with a bulk IN and a bulk OUT endpoint.
  const uint8_t* p = (const uint8_t*)cfg;
  uint16_t total = cfg->wTotalLength;
  int curIf = -1;
  uint8_t inEp = 0, outEp = 0;
  uint16_t inMps = 64;
  for (uint16_t i = 0; i + 2 <= total && p[i] >= 2; i = (uint16_t)(i + p[i])) {
    if (i + p[i] > total) break;
    if (p[i + 1] == 0x04 && p[i] >= 9) {
      if (inEp && outEp) break;
      curIf = p[i + 3] == 0 ? p[i + 2] : -1;
      inEp = outEp = 0;
    }
    if (p[i + 1] == 0x05 && p[i] >= 7 && curIf >= 0 && (p[i + 3] & 0x03) == 0x02) {
      if (p[i + 2] & 0x80) {
        if (!inEp) { inEp = p[i + 2]; inMps = (uint16_t)(p[i + 4] | (p[i + 5] << 8)); }
      } else if (!outEp) {
        outEp = p[i + 2];
      }
    }
  }
  if (curIf < 0 || !inEp || !outEp) {
    setNote("FTDI %04X:%04X: нет bulk-каналов в интерфейсе", dd->idVendor, dd->idProduct);
    usb_host_device_close(client_, dev);
    return;
  }
  e = usb_host_interface_claim(client_, dev, (uint8_t)curIf, 0);
  if (e != ESP_OK) {
    setNote("FTDI %04X:%04X: интерфейс %d не захвачен: %s", dd->idVendor, dd->idProduct, curIf, esp_err_to_name(e));
    usb_host_device_close(client_, dev);
    return;
  }
  dev_ = dev;
  ifaceClaimed_ = true;
  iface_ = (uint8_t)curIf;
  epIn_ = inEp;
  epOut_ = outEp;
  mpsIn_ = inMps ? inMps : 64;
  port_ = (uint16_t)(iface_ + 1);
  vid_ = dd->idVendor;
  pid_ = dd->idProduct;
  bcd_ = dd->bcdDevice;
  if (!ctrlX_) usb_host_transfer_alloc(64, 0, &ctrlX_);
  if (!txX_) usb_host_transfer_alloc(64, 0, &txX_);
  if (!rxX_) usb_host_transfer_alloc(mpsIn_, 0, &rxX_);
  gone_ = false;
  configured_ = false;
  lineKnown_ = false;
  if (!ctrlX_ || !txX_ || !rxX_) {
    logLine("FTDI transfer allocation failed");
    gone_ = true;
    usbErr_++;
    return;
  }
  // Start the endless bulk IN stream (status bytes arrive every latency period).
  rxX_->device_handle = dev_;
  rxX_->bEndpointAddress = epIn_;
  rxX_->num_bytes = mpsIn_;
  rxX_->callback = rxCb;
  rxX_->context = this;
  rxActive_ = usb_host_transfer_submit(rxX_) == ESP_OK;
  if (!rxActive_) {
    logLine("FTDI bulk IN submit failed");
    gone_ = true;
    usbErr_++;
    return;
  }
  attachCount_++;
  attached_ = true;
  retry_ = {};
  stallSinceMs_ = 0;
  reenumCount_ = 0;
  setNote("");
  Serial.printf("[usb] FTDI adapter %04X:%04X bcd %04X attached (interface %u, IN %02X, OUT %02X)\n", vid_, pid_, bcd_,
                iface_, epIn_, epOut_);
}

void FtdiLink::closeDevice() {
  if (!dev_) return;
  if (ctrlPending_ && xSemaphoreTake(ctrlDone_, 0) == pdTRUE) ctrlPending_ = false;
  if (txPending_ && xSemaphoreTake(txDone_, 0) == pdTRUE) txPending_ = false;
  if (ctrlPending_ || txPending_) return;
  if (ifaceClaimed_ && usb_host_interface_release(client_, dev_, iface_) != ESP_OK) return;
  ifaceClaimed_ = false;
  // Fails while a control/OUT transfer is still in flight: retried on the
  // next client-task loop, so the address is freed for a re-plugged adapter.
  if (usb_host_device_close(client_, dev_) != ESP_OK) return;
  dev_ = nullptr;
  gone_ = false;
  open_ = false;
  attached_ = false;
  configured_ = false;
  if (rxX_) usb_host_transfer_free(rxX_);
  rxX_ = nullptr;
  setNote("USB-канал закрыт, ожидание FTDI");
}

void FtdiLink::rxCb(usb_transfer_t* x) {
  FtdiLink* self = static_cast<FtdiLink*>(x->context);
  if (x->status != USB_TRANSFER_STATUS_COMPLETED) {
    if (x->status != USB_TRANSFER_STATUS_NO_DEVICE && x->status != USB_TRANSFER_STATUS_CANCELED) self->usbErr_++;
    // A failed IN endpoint can be halted. Reclaim it on a fresh attachment
    // instead of queueing the next receive on a stopped pipe.
    self->gone_ = true;
    self->attached_ = false;
    self->rxActive_ = false;
    return;
  } else if (x->actual_num_bytes >= 2) {
    // FTDI packs: [modem status][line status][data...] per max-packet chunk.
    for (int off = 0; off + 2 <= x->actual_num_bytes; off += self->mpsIn_) {
      uint8_t lsr = x->data_buffer[off + 1];
      if (lsr & LSR_PE) self->parityErr_++;
      if (lsr & LSR_FE) self->frameErr_++;
      if (lsr & LSR_BI) self->breakSeen_++;
      if (lsr & LSR_OE) self->overrun_++;
      int end = off + self->mpsIn_;
      if (end > x->actual_num_bytes) end = x->actual_num_bytes;
      int n = end - (off + 2);
      if (n > 0) xStreamBufferSend(self->rx_, x->data_buffer + off + 2, (size_t)n, 0);
    }
  }
  if (self->gone_) {
    self->rxActive_ = false;
    return;
  }
  x->num_bytes = self->mpsIn_;
  if (usb_host_transfer_submit(x) != ESP_OK) {
    self->usbErr_++;
    self->rxActive_ = false;
    self->gone_ = true;
    self->attached_ = false;
  }
}

void FtdiLink::ctrlCb(usb_transfer_t* x) {
  FtdiLink* self = static_cast<FtdiLink*>(x->context);
  self->ctrlOk_ = x->status == USB_TRANSFER_STATUS_COMPLETED;
  xSemaphoreGive(self->ctrlDone_);
}

void FtdiLink::txCb(usb_transfer_t* x) {
  FtdiLink* self = static_cast<FtdiLink*>(x->context);
  self->txOk_ = x->status == USB_TRANSFER_STATUS_COMPLETED;
  xSemaphoreGive(self->txDone_);
}

bool FtdiLink::ctrl(uint8_t request, uint16_t value, uint16_t index) {
  if (!attached_ || !dev_) return false;
  if (ctrlPending_ && xSemaphoreTake(ctrlDone_, 0) != pdTRUE) return false;
  ctrlPending_ = false;
  usb_setup_packet_t* s = (usb_setup_packet_t*)ctrlX_->data_buffer;
  s->bmRequestType = 0x40;
  s->bRequest = request;
  s->wValue = value;
  s->wIndex = index;
  s->wLength = 0;
  ctrlX_->device_handle = dev_;
  ctrlX_->bEndpointAddress = 0;
  ctrlX_->num_bytes = sizeof(usb_setup_packet_t);
  ctrlX_->callback = ctrlCb;
  ctrlX_->context = this;
  if (usb_host_transfer_submit_control(client_, ctrlX_) != ESP_OK) {
    usbErr_++;
    return false;
  }
  ctrlPending_ = xSemaphoreTake(ctrlDone_, pdMS_TO_TICKS(500)) != pdTRUE;
  if (ctrlPending_ || !ctrlOk_) {
    usbErr_++;
    return false;
  }
  return true;
}

bool FtdiLink::setLine(Parity p, bool brk) {
  if (lineKnown_ && p == curParity_ && brk == curBreak_) return true;
  // 8 data bits, 1 stop bit; parity NONE (0) or SPACE (4); bit 14 = BREAK.
  uint16_t v = (uint16_t)(8 | ((p == Parity::Space ? 4 : 0) << 8) | (brk ? (1u << 14) : 0));
  if (!ctrl(SIO_SET_DATA, v, port_)) return false;
  curParity_ = p;
  curBreak_ = brk;
  lineKnown_ = true;
  return true;
}

bool FtdiLink::setBaud9600() {
  uint32_t enc = ftdiDivisor(9600);
  uint16_t bValue = (uint16_t)(enc & 0xFFFF);
  uint16_t bIndex = isMultiPort(bcd_) ? (uint16_t)(((enc >> 8) & 0xFF00) | port_) : (uint16_t)(enc >> 16);
  return ctrl(SIO_SET_BAUDRATE, bValue, bIndex);
}

// SIO_MODEM_CTRL: high byte = lines to change (bit0 DTR, bit1 RTS), low byte
// = new state (1 = asserted, pin low). One request per line, as libftdi does.
bool FtdiLink::setModemLines(uint8_t modem) {
  if (modem == modem_) return true;
  bool ok = ctrl(SIO_MODEM_CTRL, (uint16_t)(0x0100 | (modem & kwake::kDtr)), port_) &&
            ctrl(SIO_MODEM_CTRL, (uint16_t)(0x0200 | (modem & kwake::kRts)), port_);
  modem_ = ok ? modem : 0xFF;
  return ok;
}

// Same order as the PC tester after FT_Open: flow control off, 9600 Bd,
// 8N1, timeouts, latency timer. DTR/RTS follow the wake-up profile
// (deasserted by default, like both reference tools).
bool FtdiLink::configure() {
  modem_ = 0xFF;
  portENTER_CRITICAL(&wakeMux_);
  uint8_t modem = planner_.count() ? planner_.current().modem : wakeUser_.modem;
  portEXIT_CRITICAL(&wakeMux_);
  bool ok = ctrl(SIO_RESET, SIO_RESET_SIO, port_) && ctrl(SIO_SET_FLOW_CTRL, 0, port_) && setBaud9600() &&
            ctrl(SIO_SET_LATENCY_TIMER, HFM_FTDI_LATENCY_MS, port_) && setModemLines(modem) &&
            ctrl(SIO_RESET, SIO_RESET_PURGE_RX, port_) && ctrl(SIO_RESET, SIO_RESET_PURGE_TX, port_);
  lineKnown_ = false;
  ok = ok && setLine(Parity::None, false);
  configured_ = ok;
  return ok;
}

bool FtdiLink::open() {
  if (!ioMtx_) return false;
  IoGuard guard(ioMtx_);
  if (!hostReady_ || !attached_) return false;
  if (!configured_ && !configure()) return false;
  purgeRx();
  open_ = true;
  return true;
}

void FtdiLink::close() {
  if (!ioMtx_) return;
  IoGuard guard(ioMtx_);
  // The adapter stays configured and attached; only the session ends.
  if (attached_ && configured_) setLine(Parity::None, false);
  open_ = false;
}

void FtdiLink::purgeRx() {
  uint8_t tmp[64];
  while (xStreamBufferReceive(rx_, tmp, sizeof tmp, 0) > 0) {
  }
}

bool FtdiLink::writeByte(uint8_t b) {
  if (!attached_ || !dev_) return false;
  if (txPending_ && xSemaphoreTake(txDone_, 0) != pdTRUE) return false;
  txPending_ = false;
  txX_->device_handle = dev_;
  txX_->bEndpointAddress = epOut_;
  txX_->data_buffer[0] = b;
  txX_->num_bytes = 1;
  txX_->callback = txCb;
  txX_->context = this;
  if (usb_host_transfer_submit(txX_) != ESP_OK) {
    usbErr_++;
    return false;
  }
  txPending_ = xSemaphoreTake(txDone_, pdMS_TO_TICKS(200)) != pdTRUE;
  if (txPending_ || !txOk_) {
    usbErr_++;
    return false;
  }
  return true;
}

int FtdiLink::readBytes(uint8_t* buf, int n, uint32_t timeoutMs) {
  int got = 0;
  uint32_t t0 = millis();
  while (got < n) {
    uint32_t el = millis() - t0;
    if (el >= timeoutMs) break;
    size_t r = xStreamBufferReceive(rx_, buf + got, (size_t)(n - got), pdMS_TO_TICKS(timeoutMs - el));
    got += (int)r;
    if (!attached_) break;
  }
  return got;
}

bool FtdiLink::Line::setModem(uint8_t modem) { return f_.setModemLines(modem); }
bool FtdiLink::Line::setBreak(bool on) { return f_.setLine(Parity::None, on); }
bool FtdiLink::Line::setSpace9600() {
  // The tester applies baud rate and data format again after the BREAK.
  f_.lineKnown_ = false;
  return f_.setBaud9600() && f_.setLine(Parity::Space, false);
}
bool FtdiLink::Line::write(uint8_t b) { return f_.writeByte(b); }
int FtdiLink::Line::read(uint8_t& b, uint32_t timeoutMs) {
  if (!f_.attached_) return -1;
  if (f_.readBytes(&b, 1, timeoutMs) == 1) return 1;
  return f_.attached_ ? 0 : -1;
}

void FtdiLink::setWakeConfig(uint16_t breakMs, uint8_t modem, bool autoTune, uint16_t learnedBreakMs,
                             uint8_t learnedModem) {
  portENTER_CRITICAL(&wakeMux_);
  wakeUser_.breakMs = breakMs;
  wakeUser_.modem = modem & (kwake::kDtr | kwake::kRts);
  wakeAuto_ = autoTune;
  haveLearned_ = learnedBreakMs != 0;
  wakeLearned_.breakMs = learnedBreakMs;
  wakeLearned_.modem = learnedModem & (kwake::kDtr | kwake::kRts);
  portEXIT_CRITICAL(&wakeMux_);
  wakeConfigChanged_ = true;
}

bool FtdiLink::lastWake(kwake::Diag& d) const {
  portENTER_CRITICAL(&wakeMux_);
  bool have = haveWake_;
  if (have) d = lastWake_;
  portEXIT_CRITICAL(&wakeMux_);
  return have;
}

bool FtdiLink::goodProfile(kwake::Profile& p) const {
  portENTER_CRITICAL(&wakeMux_);
  bool have = planner_.good(p);
  portEXIT_CRITICAL(&wakeMux_);
  return have;
}

kwake::Profile FtdiLink::wakeProfile() const {
  portENTER_CRITICAL(&wakeMux_);
  kwake::Profile p = planner_.count() ? planner_.current() : wakeUser_;
  portEXIT_CRITICAL(&wakeMux_);
  return p;
}

bool FtdiLink::impulseHold(uint32_t ms) {
  if (!ioMtx_) return false;
  IoGuard guard(ioMtx_);
  if (!isOpen()) return false;
  if (!setLine(Parity::None, true)) return false;  // line low = button pressed
  delay(ms);
  bool ok = setLine(Parity::None, false);
  delay(50);
  xStreamBufferReset(rx_);  // our own BREAK came back as received bytes
  lineKnown_ = false;
  return ok;
}

uint16_t FtdiLink::impulseCapture(const ImpulseConfig& cfg, ImpulseEdge* edges, uint16_t maxEdges,
                                  ImpulseResult& res) {
  res = ImpulseResult();
  if (!ioMtx_) return 0;
  IoGuard guard(ioMtx_);
  if (!isOpen()) return 0;
  if (!setLine(Parity::None, true)) return 0;
  delay(cfg.pressMs);
  setLine(Parity::None, false);
  delay(50);
  xStreamBufferReset(rx_);
  lineKnown_ = false;
  // Falls closer than minPulseMs belong to the same pulse (bytes of one long
  // low phase, contact bounce).
  uint16_t n = 0;
  uint32_t t0 = millis(), last = 0, brk = breakSeen_;
  bool any = false;
  while (millis() - t0 < cfg.listenMs) {
    uint8_t b;
    bool fall = readBytes(&b, 1, 5) == 1;
    if (breakSeen_ != brk) {
      brk = breakSeen_;
      fall = true;
    }
    uint32_t now = millis();
    if (fall) {
      if (!any || now - last >= cfg.minPulseMs) {
        if (any && n < maxEdges) edges[n++] = ImpulseEdge{1, (uint16_t)(now - last > 65535 ? 65535 : now - last)};
        if (n < maxEdges) edges[n++] = ImpulseEdge{0, 0};
        res.pulses++;
        any = true;
      } else {
        res.glitches++;
      }
      last = now;
    } else if (any && now - last >= cfg.endGapMs) {
      if (n < maxEdges) edges[n++] = ImpulseEdge{1, (uint16_t)(now - last)};
      res.complete = true;
      break;
    }
    if (!attached_) break;
  }
  res.edgeCount = n;
  xStreamBufferReset(rx_);
  return n;
}

LinkStatus FtdiLink::wakeup(const LinkTiming& t, uint8_t& echo, uint8_t& sync) {
  if (!ioMtx_) return LinkStatus::LinkDown;
  IoGuard guard(ioMtx_);
  echo = sync = 0;
  if (!isOpen()) return LinkStatus::LinkDown;
  portENTER_CRITICAL(&wakeMux_);
  if (wakeConfigChanged_.exchange(false)) {
    planner_.configure(wakeUser_, wakeAuto_);
    if (haveLearned_) planner_.setLearned(wakeLearned_);
  }
  kwake::Profile p = planner_.current();
  portEXIT_CRITICAL(&wakeMux_);
  // AA at the configured BREAK + pause from the start of BREAK (177 + 214 =
  // 391 ms, the tester uses 390 ms), whatever BREAK length the profile has.
  uint32_t aaAt = (uint32_t)t.breakMs + t.postBreakMs;
  if (aaAt < (uint32_t)p.breakMs + 20) aaAt = p.breakMs + 20;
  if (aaAt > 2000) aaAt = 2000;
  kwake::Diag d;
  Line line(*this);
  LinkStatus st = kwake::run(line, p, (uint16_t)aaAt, d);
  wakeAttempts_++;
  portENTER_CRITICAL(&wakeMux_);
  if (st != LinkStatus::LinkDown) planner_.result(st == LinkStatus::Ok);
  wakeLocked_ = planner_.locked();
  lastWake_ = d;
  haveWake_ = true;
  portEXIT_CRITICAL(&wakeMux_);
  purgeRx();
  char m[96];
  snprintf(m, sizeof m, "wake BREAK %u ms DTR %u RTS %u: break on line %s, echo %s, ECU %02X -> %s", (unsigned)p.breakMs,
           (p.modem & kwake::kDtr) ? 1 : 0, (p.modem & kwake::kRts) ? 1 : 0, d.breakOnRx ? "yes" : "no",
           d.echo ? "AA" : "none", (unsigned)d.ecuByte, linkStatusCode(st));
  logLine(m);
  echo = d.echo ? 0xAA : 0;
  sync = d.ok ? 0x55 : d.ecuByte;
  return st;
}

LinkStatus FtdiLink::exchange(uint8_t tx, Parity p, uint16_t deadlineMs, uint8_t& echo, uint8_t& resp) {
  if (!ioMtx_) return LinkStatus::LinkDown;
  IoGuard guard(ioMtx_);
  echo = resp = 0;
  if (!isOpen()) return LinkStatus::LinkDown;
  purgeRx();
  // Parity is switched only between bytes: the previous byte's echo and the
  // ECU reply have already been received, so the line is idle here.
  if (!setLine(p, false) || !writeByte(tx)) return attached_ ? LinkStatus::Timeout : LinkStatus::LinkDown;
  uint8_t buf[2] = {0, 0};
  int n = readBytes(buf, 2, deadlineMs);
  if (n >= 1) echo = buf[0];
  if (n >= 2) resp = buf[1];
  if (!attached_) return LinkStatus::LinkDown;
  return n < 2 ? LinkStatus::Timeout : LinkStatus::Ok;
}
#endif
