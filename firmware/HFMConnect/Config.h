// Config.h - hardware and build configuration of HFM Connect (ESP32-S3).
// Edit pins here to match your board / K-Line transceiver wiring.
// See docs/HARDWARE.md for the reference schematic.
#pragma once

#define HFM_FW_VERSION "0.3.3"
#define HFM_FW_NAME "HFM Connect"

// ---- K-Line transport ----------------------------------------------------------
// 1 = USB K-Line adapter with an FTDI chip (FT232R "KKL" cable) on the ESP32-S3
//     USB OTG port (USB host). Arduino IDE: "USB CDC On Boot: Disabled", upload
//     and Serial log via the UART ("COM") connector, adapter on the "USB" one.
//     The OTG port must supply 5 V VBUS to the adapter (see docs/HARDWARE.md).
// 0 = external K-Line transceiver on UART1 (pins below).
#define HFM_TRANSPORT_FTDI 1
#define HFM_FTDI_LATENCY_MS 1   // FTDI latency timer: fast echo/response delivery
#define HFM_FTDI_DTR 1          // assert DTR (many KKL cables enable the K-Line driver with it)
#define HFM_FTDI_RTS 1          // assert RTS

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

// ---- Status LED -------------------------------------------------------------------
// Uses the on-board RGB LED (RGB_BUILTIN) if the board defines it.
#define HFM_USE_RGB_LED 1

// ---- Wi-Fi ------------------------------------------------------------------------
// Default access point; change the password in Settings on first start.
// This default is public by design (documented) - it is not a secret.
#define HFM_AP_SSID_PREFIX "HFM-Connect-"
#define HFM_AP_DEFAULT_PASS "hfm12345"
#define HFM_MDNS_NAME "hfm"
#define HFM_HTTP_PORT 80

// ---- Diagnostic engine ----------------------------------------------------------
#define HFM_ENGINE_TASK_CORE 0
#define HFM_ENGINE_TASK_STACK 12288
#define HFM_KEEPALIVE_MS 700        // idle read F0:00 while connected (REV 1.1 idle limit 1300 ms)
#define HFM_AUTOCONNECT_MIN_MS 2000  // auto-connect retry interval after boot / adapter plug-in
#define HFM_AUTOCONNECT_MAX_MS 15000 // back-off limit while the ECU does not answer
#define HFM_LIVE_MIN_CYCLE_MS 100   // polling interval 0.1 s (REV 1.1)
#define HFM_RECORD_FLUSH_MS 2000

// ---- Logging --------------------------------------------------------------------
#define HFM_SERIAL_BAUD 115200
