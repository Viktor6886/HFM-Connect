// HFM Connect - diagnostic tool for Mercedes-Benz Bosch HFM-SFI (M104/M111)
// over K-Line, ESP32-S3 + external K-Line transceiver, offline WebUI.
//
// Arduino IDE: Board "ESP32S3 Dev Module", Partition Scheme
// "Default 4MB with spiffs" (or larger), USB CDC On Boot "Enabled".
// No external libraries are required. See docs/BUILD.md.
//
// Safety: all state-changing ECU operations are disabled by default and need
// (1) a profile with an evidence level allowing them, (2) Engineer Mode,
// (3) the WRITE-EN hardware key, (4) fresh preconditions and (5) a typed
// confirmation phrase. Read docs/DIAGNOSTIC_FUNCTIONS.md before use.

#include <LittleFS.h>

#include "Config.h"
#include "DiagEngine.h"
#include "NetManager.h"
#include "Settings.h"
#include "WebApi.h"

static void ledColor(uint8_t r, uint8_t g, uint8_t b) {
#if HFM_USE_RGB_LED && defined(RGB_BUILTIN)
  rgbLedWrite(RGB_BUILTIN, r, g, b);
#else
  (void)r; (void)g; (void)b;
#endif
}

void setup() {
  Serial.begin(HFM_SERIAL_BAUD);
  delay(200);
  Serial.printf("\n%s %s starting\n", HFM_FW_NAME, HFM_FW_VERSION);
  ledColor(0, 0, 16);

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed (recordings/reports disabled)");
  AppSettings.load();
  Engine.begin(&AppSettings);  // drives the K-Line TX idle HIGH as early as possible
  netBegin();
  webBegin();

  Serial.printf("Wi-Fi AP: %s  ->  http://192.168.4.1/  or  http://%s.local/\n", netApSsid(), HFM_MDNS_NAME);
  Serial.printf("K-Line UART%d TX=GPIO%d RX=GPIO%d\n", HFM_KLINE_UART_NUM, HFM_KLINE_TX_PIN, HFM_KLINE_RX_PIN);
}

void loop() {
  netLoop();
  webLoop();

  // Status LED: blue = idle, green = ECU session, yellow = live polling,
  // red = session error.
  static uint32_t lastLed = 0;
  if (millis() - lastLed > 250) {
    lastLed = millis();
    Engine.lock();
    SessionState s = Engine.state().session;
    bool liveOn = Engine.state().liveActive;
    Engine.unlock();
    if (s == SessionState::Error) ledColor(24, 0, 0);
    else if (s == SessionState::Ready) liveOn ? ledColor(16, 12, 0) : ledColor(0, 16, 0);
    else ledColor(0, 0, 8);
  }
  delay(2);
}
