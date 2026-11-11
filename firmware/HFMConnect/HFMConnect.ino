// HFM Connect - diagnostic tool for Mercedes-Benz Bosch HFM-SFI (M104/M111)
// over K-Line: USB FTDI K-Line adapter (ESP32-S3 USB host) or UART transceiver, offline WebUI.
//
// Arduino IDE: Board "ESP32S3 Dev Module", Partition Scheme "Default 4MB with
// spiffs" (or larger), USB CDC On Boot "Disabled", USB Mode "Hardware CDC and
// JTAG". Upload/log via the UART connector; FTDI K-Line adapter on the USB one.
// No external libraries are required. See docs/BUILD.md.
//
// Safety: all state-changing ECU operations are disabled by default and need
// (1) a profile with an evidence level allowing them, (2) Engineer Mode,
// (3) the WRITE-EN hardware key, (4) fresh preconditions and (5) a typed
// confirmation phrase. Read docs/DIAGNOSTIC_FUNCTIONS.md before use.

#include <LittleFS.h>

#include "Config.h"
#include "DiagEngine.h"
#include "Indicator.h"
#include "NetManager.h"
#include "OledDisplay.h"
#include "Settings.h"
#include "WebApi.h"

// The FTDI adapter uses the USB OTG port in host mode: the native USB must not
// be used for the Serial console at the same time.
#if HFM_TRANSPORT_FTDI && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#error "FTDI-адаптер: установите Инструменты -> USB CDC On Boot: Disabled (лог и загрузка через UART-разъём)"
#endif
#if HFM_TRANSPORT_FTDI && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0
#error "FTDI-адаптер: установите Инструменты -> USB Mode: Hardware CDC and JTAG (не USB-OTG/TinyUSB)"
#endif

void setup() {
  Serial.begin(HFM_SERIAL_BAUD);
  delay(200);
  Serial.printf("\n%s %s starting\n", HFM_FW_NAME, HFM_FW_VERSION);

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed (recordings/reports disabled)");
  AppSettings.load();
  Engine.begin(&AppSettings);  // drives the K-Line TX idle HIGH as early as possible
  indicatorSetLevels(AppSettings.soundLevel, AppSettings.lightLevel);
  indicatorBegin();            // piezo + RGB LED follow the engine state (own task)
  oledConfigure(AppSettings.oledOn, AppSettings.oledType, AppSettings.oledFlip, AppSettings.oledLevel);
  oledBegin();                 // OLED: own low-priority task, output only (no-op without HFM_OLED_ENABLE)
  netBegin();
  webBegin();

  Serial.printf("Wi-Fi AP: %s  ->  http://192.168.4.1/  or  http://%s.local/\n", netApSsid(), HFM_MDNS_NAME);
  Serial.printf("K-Line UART%d TX=GPIO%d RX=GPIO%d\n", HFM_KLINE_UART_NUM, HFM_KLINE_TX_PIN, HFM_KLINE_RX_PIN);
}

void loop() {
  netLoop();
  webLoop();

  delay(2);
}
