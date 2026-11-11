#ifdef ARDUINO
#include "OledDisplay.h"

#include "Config.h"

#if HFM_OLED_ENABLE
#include <U8g2lib.h>
#include <Wire.h>

#include "DiagEngine.h"
#include "OledModel.h"
#include "OledRender.h"
#include "Settings.h"

// The display pins must be free (see docs/HARDWARE.md for the pin map).
#define HFM_OLED_CLASH(p)                                                                              \
  ((p) == HFM_BUZZER_PIN || (p) == HFM_LED_R_PIN || (p) == HFM_LED_G_PIN || (p) == HFM_LED_B_PIN ||     \
   (p) == HFM_WRITE_EN_PIN || (p) == HFM_VBAT_ADC_PIN || (p) == HFM_KLINE_TX_PIN ||                    \
   (p) == HFM_KLINE_RX_PIN || (p) == HFM_ONBOARD_RGB_PIN || (p) == 19 || (p) == 20 || (p) == 0 ||      \
   (p) == 3 || (p) == 45 || (p) == 46)
#if HFM_OLED_CLASH(HFM_OLED_SDA_PIN) || HFM_OLED_CLASH(HFM_OLED_SCL_PIN) || HFM_OLED_SDA_PIN == HFM_OLED_SCL_PIN
#error "OLED: вывод SDA/SCL занят (пищалка, светодиод, WRITE-EN, АЦП, K-Line, USB 19/20, strapping) — см. Config.h"
#endif

namespace {

// Settings (written by the web task, read by the display task).
volatile bool gOn = true, gFlip = false, gReconfig = true;
volatile uint8_t gType = HFM_OLED_CONTROLLER, gLevel = 2;
// Status (written by the display task).
volatile bool gPresent = false;
volatile uint8_t gAddr = 0;
volatile uint32_t gFrames = 0;

const uint8_t kContrast[4] = {1, 40, 140, 255};
const uint32_t kProbeMs = 3000;        // presence check / search interval
const uint32_t kDimAfterMs = 300000;   // same picture for 5 min: dim (burn-in protection)
const uint32_t kTickMs = 40;           // state check rate; the picture is sent only when it changed

bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

U8G2* device(uint8_t type) {
  // Both drivers exist once; only the selected one is used.
  static U8G2_SSD1306_128X64_NONAME_F_HW_I2C ssd(U8G2_R0, U8X8_PIN_NONE, HFM_OLED_SCL_PIN, HFM_OLED_SDA_PIN);
  static U8G2_SH1106_128X64_NONAME_F_HW_I2C sh(U8G2_R0, U8X8_PIN_NONE, HFM_OLED_SCL_PIN, HFM_OLED_SDA_PIN);
  return type == 1 ? static_cast<U8G2*>(&sh) : static_cast<U8G2*>(&ssd);
}

oled::Link mapLink(LinkPhase p) {
  switch (p) {
    case LinkPhase::NoAdapter: return oled::Link::NoAdapter;
    case LinkPhase::Connecting: return oled::Link::Connecting;
    case LinkPhase::Recovering: return oled::Link::Recovering;
    case LinkPhase::Ready: return oled::Link::Ready;
    case LinkPhase::Error: return oled::Link::Error;
    default: return oled::Link::Idle;  // Idle, Paused
  }
}

// Copies what the display needs. Only memory is touched while the engine lock is
// held; fault texts are pointers into the firmware's DTC catalogue (the same
// texts the WebUI shows), so nothing is duplicated.
void snapshot(oled::Input& in) {
  int16_t vi = AppSettings.vehicleIdx;
  const prof::VehicleEntry* ve = vi >= 0 ? prof::vehicleByIndex((uint16_t)vi) : nullptr;
  const bool m111 = ve && strncmp(ve->engine, "111", 3) == 0;
  Engine.lock();
  const EngineState& e = Engine.state();
  in.link = mapLink(e.link);
  in.reading = (e.job.state == JobState::Running || e.job.state == JobState::Queued) &&
               (e.job.job == Job::ReadDtc || e.job.job == Job::Identify);
  in.dtcValid = e.dtc.valid;
  in.dtcStamp = e.dtc.tMs;
  const FeatRun& fr = e.feat[prof::F_DTC_READ];
  in.dtcFailed = fr.state == 2 && (!e.dtc.valid || (int32_t)(fr.tMs - e.dtc.tMs) > 0);
  in.count = 0;
  for (uint16_t i = 0; i < e.dtc.count && in.count < oled::kMaxFaults; i++) {
    const dtc::Decoded& x = e.dtc.items[i];
    bool seen = false;  // the same code may be set in both banks: show it once
    for (uint8_t k = 0; k < in.count; k++)
      if (in.faults[k].code == x.code) seen = true;
    if (seen) continue;
    const char* alt = m111 ? dtc::m111Text(x.code) : nullptr;
    in.faults[in.count].code = x.code;
    in.faults[in.count].text = alt ? alt : (x.entry ? x.entry->ru : "Бит вне каталога кодов — подробности в WebUI.");
    in.count++;
  }
  Engine.unlock();
}

void task(void*) {
  Wire.begin(HFM_OLED_SDA_PIN, HFM_OLED_SCL_PIN, (uint32_t)HFM_OLED_I2C_HZ);
  Wire.setTimeOut(20);  // a stuck bus costs this task milliseconds, nobody else anything
  oled::Model model;
  U8G2* u = nullptr;
  bool present = false, dimmed = false, force = true;
  uint32_t lastProbe = 0, lastKey = 0, lastChange = millis();
  oled::Screen lastScreen = oled::Screen::Boot;
  uint8_t level = 2;
  for (;;) {
    uint32_t now = millis();
    if (!gOn) {
      if (present && u) u->setPowerSave(1);
      present = false;
      gPresent = false;
      gReconfig = true;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    // (Re)attach: at start, after a settings change, or when the display vanished.
    if (!present || gReconfig) {
      if (!gReconfig && now - lastProbe < kProbeMs) {  // not found last time: look again later
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      lastProbe = now;
      uint8_t addr = probe(0x3C) ? 0x3C : (probe(0x3D) ? 0x3D : 0);
      gReconfig = false;
      if (!addr) {
        present = false;
        gPresent = false;
        gAddr = 0;
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      u = device(gType);
      u->setI2CAddress(addr * 2);
      u->setBusClock(HFM_OLED_I2C_HZ);
      u->begin();
      u->setDisplayRotation(gFlip ? U8G2_R2 : U8G2_R0);
      level = gLevel;
      u->setContrast(kContrast[level & 3]);
      u->setPowerSave(0);
      present = true;
      dimmed = false;
      force = true;
      gAddr = addr;
      gPresent = true;
      lastChange = now;
    }
    // Presence check: an unplugged display stops the drawing, nothing else.
    if (now - lastProbe >= kProbeMs) {
      lastProbe = now;
      if (!probe(gAddr)) {
        present = false;
        gPresent = false;
        continue;
      }
    }
    oled::Input in;
    snapshot(in);
    model.update(in, now);
    const oled::View& v = model.view();
    if (v.screen != lastScreen) lastChange = now;
    if (gLevel != level) {
      level = gLevel;
      dimmed = false;
      u->setContrast(kContrast[level & 3]);
    }
    // The same picture for a long time: dim it; any new screen restores the brightness.
    bool wantDim = now - lastChange > kDimAfterMs;
    if (wantDim != dimmed) {
      dimmed = wantDim;
      u->setContrast(dimmed ? kContrast[1] / 2 : kContrast[level & 3]);
    }
    if (force || v.key != lastKey || v.screen != lastScreen) {
      oled::render(u->getU8g2(), model);
      u->sendBuffer();  // ~25 ms at 400 kHz; the task sleeps in the I2C driver meanwhile
      gFrames = gFrames + 1;
      lastKey = v.key;
      lastScreen = v.screen;
      force = false;
    }
    vTaskDelay(pdMS_TO_TICKS(kTickMs));
  }
}

}  // namespace

void oledBegin() {
  // Core 1 (the diagnostic engine runs on core 0), priority 1 = lowest application priority.
  xTaskCreatePinnedToCore(task, "hfm-oled", 4608, nullptr, 1, nullptr, 1);
}

void oledConfigure(bool on, uint8_t controller, bool flip, uint8_t level) {
  const uint8_t type = controller ? 1 : 0;
  if (type != gType || flip != gFlip || on != gOn) gReconfig = true;
  gType = type;
  gFlip = flip;
  gLevel = level < 1 ? 1 : (level > 3 ? 3 : level);
  gOn = on;
}

OledStatus oledStatus() {
  OledStatus s;
  s.built = true;
  s.enabled = gOn;
  s.present = gPresent;
  s.address = gAddr;
  s.frames = gFrames;
  return s;
}

#else  // built without the display

void oledBegin() {}
void oledConfigure(bool, uint8_t, bool, uint8_t) {}
OledStatus oledStatus() { return OledStatus(); }

#endif
#endif
