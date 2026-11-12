// Config.h - hardware and build configuration of HFM Connect (ESP32-S3).
// Edit pins here to match your board / K-Line transceiver wiring.
// See docs/HARDWARE.md for the reference schematic.
#pragma once

#define HFM_FW_VERSION "0.9.0"
#define HFM_FW_NAME "HFM Connect"

// ---- K-Line transport ----------------------------------------------------------
// 1 = USB K-Line adapter with an FTDI chip (FT232R "KKL" cable) on the ESP32-S3
//     USB OTG port (USB host). Arduino IDE: "USB CDC On Boot: Disabled", upload
//     and Serial log via the UART ("COM") connector, adapter on the "USB" one.
//     The OTG port must supply 5 V VBUS to the adapter (see docs/HARDWARE.md).
// 0 = external K-Line transceiver on UART1 (pins below).
#define HFM_TRANSPORT_FTDI 1
#define HFM_FTDI_LATENCY_MS 1   // FTDI latency timer: fast echo/response delivery
// DTR/RTS default (bit0 DTR, bit1 RTS asserted): 0 = both deasserted, as the PC
// tools that work with FT232RL KKL cables leave them (D2XX: not touched;
// Win32: DTR_CONTROL_DISABLE / RTS_CONTROL_DISABLE). Changeable in settings.
#define HFM_FTDI_MODEM 0
// Delay before the USB root port is powered: lets a hub and the adapter
// behind it settle before the first enumeration.
#define HFM_USB_START_DELAY_MS 1500

// ---- K-Line transceiver (L9637D / MC33290 / SN65HVDA195 class) ----------------
// UART1 of the ESP32-S3; TX drives the transceiver TXD input, RX reads RXD.
#define HFM_KLINE_UART_NUM 1
#define HFM_KLINE_TX_PIN 17
#define HFM_KLINE_RX_PIN 18
// Set to 1 if the interface inverts the signal (e.g. discrete transistor stage
// where a HIGH GPIO pulls K-Line LOW). L9637D/MC33290: 0 (non-inverting).
#define HFM_KLINE_TX_INVERTED 0
#define HFM_KLINE_RX_INVERTED 0

// ---- WRITE-EN hardware interlock --------------------------------------------
// Physical key/jumper to GND. Open (HIGH, pull-up) = all state-changing
// operations refused by firmware regardless of UI or settings.
#define HFM_WRITE_EN_PIN 10
#define HFM_WRITE_EN_ACTIVE_LOW 1

// ---- Battery voltage measurement (optional) -----------------------------------
// Divider from K-Line connector +12 V (pin 16 / pin 3) to an ADC pin.
// -1 = not fitted: voltage is "unknown" and write preconditions require a
// manual confirmation. NEVER enable without the divider - a floating ADC pin
// could fake a valid voltage.
#define HFM_VBAT_ADC_PIN -1
#define HFM_VBAT_DIVIDER 5.545f  // (100k + 22k) / 22k
#define HFM_VBAT_CAL 1.000f

// ---- Indication (docs/HARDWARE.md "Индикация") ---------------------------------
// On-board addressable RGB LED (WS2812) mirrors the external LED. ESP32-S3 Zero: GPIO21;
// ESP32-S3-DevKitC-1: GPIO48 (the "ESP32S3 Dev Module" board setting assumes 48).
// -1 = none. Set HFM_ONBOARD_RGB_SWAP_RG to 1 if red and green appear swapped.
#define HFM_ONBOARD_RGB_PIN 21
#define HFM_ONBOARD_RGB_SWAP_RG 0
// External RGB LED, common cathode (common pin to GND), one resistor per colour:
// R 220 Ohm, G 100 Ohm, B 100 Ohm (about 5 mA each at 3.3 V). -1 = not fitted.
#define HFM_LED_R_PIN 5
#define HFM_LED_G_PIN 6
#define HFM_LED_B_PIN 7
#define HFM_LED_COMMON_ANODE 0   // 1 = common pin to 3.3 V (inverted PWM)
// Colour balance in % (green and blue dice look brighter than red at equal current).
#define HFM_LED_GAIN_R 100
#define HFM_LED_GAIN_G 55
#define HFM_LED_GAIN_B 70
// Passive piezo (no built-in generator): GPIO -> 100 Ohm -> piezo -> GND, or via an
// NPN/MOSFET stage for more volume. -1 = not fitted.
#define HFM_BUZZER_PIN 8

// ---- OLED display (optional, docs/HARDWARE.md "OLED-дисплей") ---------------------
// 0.96" 128x64, I2C, 4 pins. Output only: it shows the state of the diagnostic engine and
// never talks to the ECU. Needs the U8g2 library (Arduino Library Manager) and the
// partition scheme "Huge APP" (docs/BUILD.md). 0 = build without display and library.
#ifndef HFM_OLED_ENABLE
#define HFM_OLED_ENABLE 1
#endif
#define HFM_OLED_SDA_PIN 11       // GPIO8 (default I2C SDA) is taken by the piezo
#define HFM_OLED_SCL_PIN 9
#define HFM_OLED_CONTROLLER 0     // default: 0 = SSD1306, 1 = SH1106 (also in Settings)
#define HFM_OLED_I2C_HZ 400000

// ---- Wi-Fi ------------------------------------------------------------------------
// Access point name (no MAC suffix) and default password; the password can be
// changed in Settings. This default is public by design (documented) - it is
// not a secret. Devices that still use the previous default password are
// switched to the new one at boot; a password set by the user is kept.
#define HFM_AP_SSID "HFM Connect"
#define HFM_AP_DEFAULT_PASS "12345678"
#define HFM_AP_OLD_DEFAULT_PASS "hfm12345"
#define HFM_MDNS_NAME "hfm"
#define HFM_HTTP_PORT 80

// ---- Diagnostic engine ----------------------------------------------------------
#define HFM_ENGINE_TASK_CORE 0
#define HFM_ENGINE_TASK_STACK 12288
#define HFM_KEEPALIVE_MS 700        // idle read F0:00 while connected (REV 1.1 idle limit 1300 ms)
#define HFM_AUTOCONNECT_MIN_MS 2000  // auto-connect retry interval after boot / adapter plug-in
#define HFM_AUTOCONNECT_MAX_MS 10000 // back-off limit while the ECU does not answer
// Line silence before a new BREAK after bytes were sent (LinkTiming, D-027).
#define HFM_RESYNC_SILENCE_MS 2500
#define HFM_LIVE_MIN_CYCLE_MS 100   // polling interval 0.1 s (REV 1.1)
#define HFM_RECORD_FLUSH_MS 2000

// ---- Logging --------------------------------------------------------------------
#define HFM_SERIAL_BAUD 115200
