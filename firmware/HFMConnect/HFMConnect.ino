// HFM Connect for ESP32-S3, USB FTDI K-Line and the offline diagnostic WebUI.
// Release build: ESP32S3 Dev Module, Huge APP (3 MB), USB CDC On Boot disabled,
// Hardware CDC and JTAG, U8g2 for OLED. See docs/BUILD.md and docs/SCANNER_RU.md.

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
