#ifdef ARDUINO
#include "FtdiLink.h"

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

void FtdiLink::setNote(const char* fmt, ...) {
  char t[sizeof note_];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(t, sizeof t, fmt, ap);
  va_end(ap);
  portENTER_CRITICAL(&noteMux_);
  memcpy(note_, t, sizeof note_);
  portEXIT_CRITICAL(&noteMux_);
  if (t[0]) Serial.printf("[usb] %s\n", t);
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
  usb_host_config_t hc = {};
  hc.skip_phy_setup = false;
  hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
  installErr_ = usb_host_install(&hc);
  if (installErr_ != ESP_OK) {
    Serial.printf("[usb] usb_host_install: %s\n", esp_err_to_name(installErr_));
    return false;
  }
  xTaskCreatePinnedToCore(hostTask, "usb-host", 4096, this, 5, nullptr, 0);
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
  xTaskCreatePinnedToCore(clientTask, "usb-ftdi", 4096, this, 5, nullptr, 0);
  hostReady_ = true;
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
    if (!self->dev_) self->pendingAddr_ = msg->new_dev.address;
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
  for (;;) {
    usb_host_client_handle_events(self->client_, pdMS_TO_TICKS(20));
    if (self->pendingAddr_ >= 0) {
      int a = self->pendingAddr_;
      self->pendingAddr_ = -1;
      self->openDevice((uint8_t)a);
    }
    if (self->gone_ && !self->rxActive_) self->closeDevice();
    // A device enumerated before this client registered (adapter already
    // plugged in at boot) produces no NEW_DEV event: look for it explicitly.
    if (!self->dev_ && self->pendingAddr_ < 0 && millis() - self->lastScanMs_ >= 1000) {
      self->lastScanMs_ = millis();
      self->scanDevices();
    }
  }
}

void FtdiLink::scanDevices() {
  int n = usbDevices();
  if (n == lastScanCount_) return;  // rejected devices are not retried until the set changes
  lastScanCount_ = n;
  if (n <= 0) return;
  uint8_t addrs[8];
  int cnt = 0;
  if (usb_host_device_addr_list_fill(sizeof addrs, addrs, &cnt) != ESP_OK) return;
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
    usb_host_device_close(client_, dev);
    setNote("USB-устройство (адрес %u): нет дескриптора", addr);
    return;
  }
  seenVid_ = dd->idVendor;
  seenPid_ = dd->idProduct;
  if (dd->idVendor != FTDI_VID) {
    usb_host_device_close(client_, dev);  // not an FTDI adapter: ignore it
    setNote("найдено USB-устройство %04X:%04X, но это не FTDI (нужен VID 0403)", dd->idVendor, dd->idProduct);
    return;
  }
  const usb_config_desc_t* cfg = nullptr;
  if (usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK) {
    usb_host_device_close(client_, dev);
    setNote("FTDI %04X:%04X: нет дескриптора конфигурации", dd->idVendor, dd->idProduct);
    return;
  }
  // Walk the configuration: first interface with a bulk IN and a bulk OUT endpoint.
  const uint8_t* p = (const uint8_t*)cfg;
  uint16_t total = cfg->wTotalLength;
  int curIf = -1;
  uint8_t inEp = 0, outEp = 0;
  uint16_t inMps = 64;
  for (uint16_t i = 0; i + 2 <= total && p[i] >= 2; i = (uint16_t)(i + p[i])) {
    if (p[i + 1] == 0x04 && curIf < 0) curIf = p[i + 2];
    if (p[i + 1] == 0x05 && curIf >= 0 && (p[i + 3] & 0x03) == 0x02) {
      if (p[i + 2] & 0x80) {
        if (!inEp) { inEp = p[i + 2]; inMps = (uint16_t)(p[i + 4] | (p[i + 5] << 8)); }
      } else if (!outEp) {
        outEp = p[i + 2];
      }
    }
  }
  if (curIf < 0 || !inEp || !outEp) {
    usb_host_device_close(client_, dev);
    setNote("FTDI %04X:%04X: нет bulk-каналов в интерфейсе", dd->idVendor, dd->idProduct);
    return;
  }
  e = usb_host_interface_claim(client_, dev, (uint8_t)curIf, 0);
  if (e != ESP_OK) {
    usb_host_device_close(client_, dev);
    setNote("FTDI %04X:%04X: интерфейс %d не захвачен: %s", dd->idVendor, dd->idProduct, curIf, esp_err_to_name(e));
    return;
  }
  dev_ = dev;
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
  attachCount_++;
  // Start the endless bulk IN stream (status bytes arrive every latency period).
  rxX_->device_handle = dev_;
  rxX_->bEndpointAddress = epIn_;
  rxX_->num_bytes = mpsIn_;
  rxX_->callback = rxCb;
  rxX_->context = this;
  rxActive_ = usb_host_transfer_submit(rxX_) == ESP_OK;
  attached_ = true;
  setNote("");
  Serial.printf("[usb] FTDI adapter %04X:%04X bcd %04X attached (interface %u, IN %02X, OUT %02X)\n", vid_, pid_, bcd_,
                iface_, epIn_, epOut_);
}

void FtdiLink::closeDevice() {
  if (!dev_) return;
  usb_host_interface_release(client_, dev_, iface_);
  // Fails while a control/OUT transfer is still in flight: retried on the
  // next client-task loop, so the address is freed for a re-plugged adapter.
  if (usb_host_device_close(client_, dev_) != ESP_OK) return;
  dev_ = nullptr;
  gone_ = false;
  open_ = false;
  attached_ = false;
  configured_ = false;
  lastScanCount_ = -1;
  setNote("адаптер отключён от USB");
}

void FtdiLink::rxCb(usb_transfer_t* x) {
  FtdiLink* self = static_cast<FtdiLink*>(x->context);
  if (x->status != USB_TRANSFER_STATUS_COMPLETED) {
    if (x->status != USB_TRANSFER_STATUS_NO_DEVICE && x->status != USB_TRANSFER_STATUS_CANCELED) self->usbErr_++;
    if (self->gone_ || x->status == USB_TRANSFER_STATUS_NO_DEVICE) {
      self->rxActive_ = false;
      return;
    }
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
  if (usb_host_transfer_submit(x) != ESP_OK) self->rxActive_ = false;
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
  xSemaphoreTake(ctrlDone_, 0);
  if (usb_host_transfer_submit_control(client_, ctrlX_) != ESP_OK) {
    usbErr_++;
    return false;
  }
  if (xSemaphoreTake(ctrlDone_, pdMS_TO_TICKS(500)) != pdTRUE || !ctrlOk_) {
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

bool FtdiLink::configure() {
  uint32_t enc = ftdiDivisor(9600);
  uint16_t bValue = (uint16_t)(enc & 0xFFFF);
  uint16_t bIndex = isMultiPort(bcd_) ? (uint16_t)(((enc >> 8) & 0xFF00) | port_) : (uint16_t)(enc >> 16);
  bool ok = ctrl(SIO_RESET, SIO_RESET_SIO, port_) &&
            ctrl(SIO_SET_BAUDRATE, bValue, bIndex) &&
            ctrl(SIO_SET_FLOW_CTRL, 0, port_) &&
            ctrl(SIO_SET_LATENCY_TIMER, HFM_FTDI_LATENCY_MS, port_) &&
            // DTR and RTS asserted (one request each): many KKL cables use
            // these lines to enable or power the K-Line stage.
            ctrl(SIO_MODEM_CTRL, (uint16_t)(0x0100 | (HFM_FTDI_DTR ? 1 : 0)), port_) &&
            ctrl(SIO_MODEM_CTRL, (uint16_t)(0x0200 | (HFM_FTDI_RTS ? 2 : 0)), port_) &&
            ctrl(SIO_RESET, SIO_RESET_PURGE_RX, port_) && ctrl(SIO_RESET, SIO_RESET_PURGE_TX, port_);
  lineKnown_ = false;
  ok = ok && setLine(Parity::None, false);
  configured_ = ok;
  return ok;
}

bool FtdiLink::open() {
  if (!hostReady_ || !attached_) return false;
  if (!configured_ && !configure()) return false;
  purgeRx();
  open_ = true;
  return true;
}

void FtdiLink::close() {
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
  txX_->device_handle = dev_;
  txX_->bEndpointAddress = epOut_;
  txX_->data_buffer[0] = b;
  txX_->num_bytes = 1;
  txX_->callback = txCb;
  txX_->context = this;
  xSemaphoreTake(txDone_, 0);
  if (usb_host_transfer_submit(txX_) != ESP_OK) {
    usbErr_++;
    return false;
  }
  if (xSemaphoreTake(txDone_, pdMS_TO_TICKS(200)) != pdTRUE || !txOk_) {
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

LinkStatus FtdiLink::wakeup(const LinkTiming& t, uint8_t& echo, uint8_t& sync) {
  echo = sync = 0;
  if (!isOpen()) return LinkStatus::LinkDown;
  if (!setLine(Parity::None, true)) return LinkStatus::LinkDown;  // BREAK: K-Line held low
  delay(t.breakMs);
  if (!setLine(Parity::None, false)) return LinkStatus::LinkDown;
  delay(t.postBreakMs);
  purgeRx();
  if (!setLine(Parity::Space, false) || !writeByte(0xAA)) return LinkStatus::LinkDown;
  // Look for the AA (echo) 55 (ECU) pair among the first bytes; a stray
  // byte left by the BREAK edge is tolerated.
  uint8_t buf[4] = {0, 0, 0, 0};
  int n = 0;
  uint32_t t0 = millis();
  while (n < 4 && millis() - t0 < t.byteDeadlineMs) {
    if (readBytes(buf + n, 1, 20) == 1) {
      n++;
      if (n >= 2 && buf[n - 2] == 0xAA && buf[n - 1] == 0x55) break;
    }
    if (!attached_) return LinkStatus::LinkDown;
  }
  for (int i = 0; i + 1 < n; i++)
    if (buf[i] == 0xAA && buf[i + 1] == 0x55) {
      echo = 0xAA;
      sync = 0x55;
      return LinkStatus::Ok;
    }
  if (n >= 1) echo = buf[0];
  if (n >= 2) sync = buf[1];
  return LinkStatus::NoSync;
}

LinkStatus FtdiLink::exchange(uint8_t tx, Parity p, uint16_t deadlineMs, uint8_t& echo, uint8_t& resp) {
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
