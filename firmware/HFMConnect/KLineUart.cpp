#ifdef ARDUINO
#include "KLineUart.h"

#include <driver/gpio.h>

KLineUart::KLineUart(int uartNum, int txPin, int rxPin, bool txInv, bool rxInv)
    : port_((uart_port_t)uartNum), tx_(txPin), rx_(rxPin), txInv_(txInv), rxInv_(rxInv) {}

bool KLineUart::open() {
  if (open_) return true;
  uart_config_t cfg = {};
  cfg.baud_rate = 9600;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(port_, 512, 0, 16, &evq_, 0) != ESP_OK) return false;
  if (uart_param_config(port_, &cfg) != ESP_OK || uart_set_pin(port_, tx_, rx_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    uart_driver_delete(port_);
    evq_ = nullptr;
    return false;
  }
  parity_ = UART_PARITY_DISABLE;
  lineLow(false);
  uart_flush_input(port_);
  open_ = true;
  return true;
}

void KLineUart::close() {
  if (!open_) return;
  lineLow(false);
  uart_driver_delete(port_);
  evq_ = nullptr;
  open_ = false;
}

void KLineUart::setParity(uart_parity_t p) {
  if (p == parity_) return;
  uart_set_parity(port_, p);
  parity_ = p;
}

void KLineUart::lineLow(bool low) {
  // Idle UART TX is HIGH; inverting it holds the line LOW (BREAK).
  uint32_t mask = 0;
  if (txInv_ != low) mask |= UART_SIGNAL_TXD_INV;
  if (rxInv_) mask |= UART_SIGNAL_RXD_INV;
  uart_set_line_inverse(port_, mask ? mask : UART_SIGNAL_INV_DISABLE);
}

void KLineUart::drainEvents() {
  if (!evq_) return;
  uart_event_t ev;
  while (xQueueReceive(evq_, &ev, 0) == pdTRUE) {
    if (ev.type == UART_PARITY_ERR) parityErr_++;
    else if (ev.type == UART_FRAME_ERR) frameErr_++;
  }
}

LinkStatus KLineUart::wakeup(const LinkTiming& t, uint8_t& echo, uint8_t& sync) {
  echo = sync = 0;
  if (!open_) return LinkStatus::LinkDown;
  setParity(UART_PARITY_DISABLE);
  uart_flush_input(port_);
  lineLow(true);
  breaks_++;
  delay(t.breakMs);
  lineLow(false);
  delay(t.postBreakMs);
  uart_flush_input(port_);
  drainEvents();
  // 0xAA has four ones -> EVEN parity gives parity bit 0 (= space).
  setParity(UART_PARITY_EVEN);
  const uint8_t aa = 0xAA;
  uart_write_bytes(port_, (const char*)&aa, 1);
  uart_wait_tx_done(port_, pdMS_TO_TICKS(50));
  uint8_t buf[2] = {0, 0};
  int n = uart_read_bytes(port_, buf, 2, pdMS_TO_TICKS(t.byteDeadlineMs));
  drainEvents();
  if (n >= 1) echo = buf[0];
  if (n >= 2) sync = buf[1];
  if (n < 2) return LinkStatus::NoSync;
  if (buf[0] != 0xAA || buf[1] != 0x55) return LinkStatus::NoSync;
  return LinkStatus::Ok;
}

LinkStatus KLineUart::exchange(uint8_t tx, Parity p, uint16_t deadlineMs, uint8_t& echo, uint8_t& resp) {
  echo = resp = 0;
  if (!open_) return LinkStatus::LinkDown;
  uart_parity_t txPar = UART_PARITY_DISABLE;
  if (p == Parity::Space) txPar = (__builtin_popcount(tx) & 1) ? UART_PARITY_ODD : UART_PARITY_EVEN;
  uart_flush_input(port_);
  setParity(txPar);
  uart_write_bytes(port_, (const char*)&tx, 1);
  uart_wait_tx_done(port_, pdMS_TO_TICKS(20));
  // Echo of an 8N1 prefix is already being received; switch the receiver to
  // a parity mode that accepts both 10- and 11-bit ECU response frames.
  if (txPar == UART_PARITY_DISABLE) setParity(UART_PARITY_EVEN);
  uint8_t buf[2] = {0, 0};
  int n = uart_read_bytes(port_, buf, 2, pdMS_TO_TICKS(deadlineMs));
  drainEvents();
  if (n >= 1) echo = buf[0];
  if (n >= 2) resp = buf[1];
  if (n < 2) return LinkStatus::Timeout;
  return LinkStatus::Ok;
}

int KLineUart::rxLevel() const {
  int l = gpio_get_level((gpio_num_t)rx_);
  return rxInv_ ? !l : l;
}

uint16_t KLineUart::impulseCapture(const ImpulseConfig& cfg, ImpulseEdge* edges, uint16_t maxEdges) {
  if (!open_) return 0;
  lineLow(true);  // "button pressed": diagnostic contact grounded
  delay(cfg.pressMs);
  lineLow(false);
  uint16_t n = 0;
  uint32_t t0 = millis();
  int level = rxLevel();
  uint32_t phaseStart = t0;
  while (millis() - t0 < cfg.listenMs && n < maxEdges) {
    int l = rxLevel();
    uint32_t now = millis();
    if (l != level) {
      uint32_t d = now - phaseStart;
      edges[n++] = ImpulseEdge{(uint8_t)level, (uint16_t)(d > 65535 ? 65535 : d)};
      level = l;
      phaseStart = now;
    } else if (level == 1 && n > 0 && now - phaseStart >= cfg.endGapMs) {
      edges[n++] = ImpulseEdge{1, (uint16_t)(now - phaseStart)};
      break;
    }
    delay(1);
  }
  uart_flush_input(port_);
  return n;
}
#endif
