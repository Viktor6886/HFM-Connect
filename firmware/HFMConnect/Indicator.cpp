#ifdef ARDUINO
#include "Indicator.h"

#include <math.h>

#include "Config.h"
#include "DiagEngine.h"

// Indication pins must not collide with each other or with the diagnostic I/O.
#define HFM_PIN_CLASH(p)                                                                              \
  ((p) >= 0 && ((p) == HFM_WRITE_EN_PIN || (p) == HFM_VBAT_ADC_PIN || (p) == HFM_KLINE_TX_PIN ||     \
                (p) == HFM_KLINE_RX_PIN || (p) == HFM_ONBOARD_RGB_PIN || (p) == 19 || (p) == 20 || \
                (p) == 0 || (p) == 3 || (p) == 45 || (p) == 46))
#if HFM_PIN_CLASH(HFM_BUZZER_PIN) || HFM_PIN_CLASH(HFM_LED_R_PIN) || HFM_PIN_CLASH(HFM_LED_G_PIN) || \
    HFM_PIN_CLASH(HFM_LED_B_PIN)
#error "Индикация: вывод занят (WRITE-EN, АЦП, K-Line UART, встроенный RGB, USB 19/20 или strapping 0/3/45/46) — см. Config.h"
#endif
#if HFM_BUZZER_PIN >= 0 && \
    (HFM_BUZZER_PIN == HFM_LED_R_PIN || HFM_BUZZER_PIN == HFM_LED_G_PIN || HFM_BUZZER_PIN == HFM_LED_B_PIN)
#error "Индикация: пищалка и светодиод на одном выводе"
#endif

// Pins for the LEDC calls; a pin set to -1 is never driven (gBuzzer/gLed stay false).
#define BUZ_PIN (HFM_BUZZER_PIN >= 0 ? HFM_BUZZER_PIN : 0)
#define LED_R (HFM_LED_R_PIN >= 0 ? HFM_LED_R_PIN : 0)
#define LED_G (HFM_LED_G_PIN >= 0 ? HFM_LED_G_PIN : 0)
#define LED_B (HFM_LED_B_PIN >= 0 ? HFM_LED_B_PIN : 0)

// ---- melodies ----------------------------------------------------------------
// Short chimes in the 0.7-2.1 kHz range (soft for a piezo, well below its
// shrill resonance). Each note: start/end frequency (glide), length, pause
// after it and loudness. Loudness follows a bell envelope: 6 ms attack, then
// an exponential decay, so notes ring out instead of beeping.
namespace {

struct Note {
  uint16_t f0, f1;   // Hz; f1 = 0 -> no glide
  uint16_t ms;       // note length
  uint16_t gapMs;    // silence after the note
  uint8_t level;     // 0..100
};

// Note frequencies (equal temperament).
enum : uint16_t { F5 = 698, G5 = 784, A5 = 880, C6 = 1047, D6 = 1175, E6 = 1319, G6 = 1568, A6 = 1760, C7 = 2093 };

const Note kBoot[] = {{G5, 0, 90, 10, 70}, {C6, 0, 90, 10, 80}, {G6, 0, 260, 0, 85}};
const Note kConnect[] = {{C6, 0, 85, 15, 80}, {E6, 0, 85, 15, 85}, {G6, 0, 300, 0, 95}};
const Note kRecovered[] = {{E6, 0, 70, 10, 75}, {G6, A6, 220, 0, 85}};
const Note kDisconnect[] = {{G6, 0, 110, 20, 80}, {C6, 0, 300, 0, 75}};
const Note kLinkLost[] = {{A5, 0, 170, 40, 85}, {F5, 0, 340, 0, 80}};
const Note kAdapterIn[] = {{C7, 0, 45, 0, 55}};
const Note kAdapterOut[] = {{G5, F5, 70, 0, 55}};
const Note kDtcNone[] = {{C6, 0, 60, 10, 70}, {E6, 0, 60, 10, 75}, {G6, 0, 60, 10, 80}, {C7, 0, 320, 0, 85}};
const Note kDtcPresent[] = {{E6, 0, 150, 30, 90}, {C6, 0, 280, 160, 85}, {E6, 0, 150, 30, 90}, {C6, 0, 340, 0, 85}};
const Note kFault[] = {{A5, 0, 120, 90, 100}, {A5, 0, 120, 90, 100}, {A5, 0, 120, 0, 100}};
const Note kTest[] = {{C6, 0, 85, 15, 80}, {E6, 0, 85, 15, 85}, {G6, 0, 85, 15, 90}, {C7, 0, 320, 0, 95}};

struct Melody {
  const Note* n;
  uint8_t count;
};

Melody melodyFor(Sound s) {
#define M(a) Melody{a, (uint8_t)(sizeof(a) / sizeof(a[0]))}
  switch (s) {
    case Sound::Boot: return M(kBoot);
    case Sound::Connect: return M(kConnect);
    case Sound::Recovered: return M(kRecovered);
    case Sound::Disconnect: return M(kDisconnect);
    case Sound::LinkLost: return M(kLinkLost);
    case Sound::AdapterIn: return M(kAdapterIn);
    case Sound::AdapterOut: return M(kAdapterOut);
    case Sound::DtcNone: return M(kDtcNone);
    case Sound::DtcPresent: return M(kDtcPresent);
    case Sound::Fault: return M(kFault);
    case Sound::Test: return M(kTest);
    default: return Melody{nullptr, 0};
  }
#undef M
}

// Peak duty (10-bit, 512 = 50 % = loudest for a piezo) per sound level.
const uint16_t kSoundPeak[4] = {0, 70, 200, 512};
// Light scale per level (applied after gamma).
const float kLightScale[4] = {0.0f, 0.18f, 0.5f, 1.0f};

const uint8_t kBuzzRes = 10;  // own LEDC timer: tone changes do not touch the LED timer
const uint8_t kLedRes = 12;
const uint32_t kLedFreq = 5000;

QueueHandle_t gQueue = nullptr;
volatile uint8_t gSoundLevel = 2, gLightLevel = 2;
volatile uint32_t gTestLightUntil = 0;
bool gBuzzer = false, gLed = false;

// ---- player state (indicator task only) ----
Melody gMel{nullptr, 0};
uint8_t gNote = 0;
uint32_t gNoteStart = 0;
bool gInGap = false;
uint16_t gLastFreq = 0;
Sound gLastSound = Sound::None;
uint32_t gLastSoundMs = 0;

void buzzerOff() {
  if (gBuzzer) ledcWrite(BUZ_PIN, 0);
}

void startMelody(Sound s) {
  gMel = melodyFor(s);
  gNote = 0;
  gInGap = false;
  gNoteStart = millis();
  gLastFreq = 0;
}

// Advances the melody; returns true while it is playing.
bool playerTick() {
  if (!gMel.n) return false;
  uint32_t now = millis();
  const Note& n = gMel.n[gNote];
  uint32_t t = now - gNoteStart;
  if (!gInGap && t >= n.ms) {
    buzzerOff();
    gInGap = true;
  }
  if (gInGap) {
    if (t < (uint32_t)n.ms + n.gapMs) return true;
    if (++gNote >= gMel.count) {
      gMel = Melody{nullptr, 0};
      buzzerOff();
      return false;
    }
    gInGap = false;
    gNoteStart = now;
    return true;
  }
  float x = (float)t / n.ms;
  uint16_t f = n.f1 ? (uint16_t)(n.f0 + (n.f1 - (float)n.f0) * (x < 0.4f ? x / 0.4f : 1.0f)) : n.f0;
  float env = t < 6 ? t / 6.0f : expf(-3.2f * x);  // bell: quick attack, exponential ring-out
  uint32_t duty = (uint32_t)(kSoundPeak[gSoundLevel & 3] * env * n.level / 100.0f);
  if (f != gLastFreq) {
    ledcChangeFrequency(BUZ_PIN, f, kBuzzRes);
    gLastFreq = f;
  }
  ledcWrite(BUZ_PIN, duty);
  return true;
}

// ---- light ----------------------------------------------------------------------
struct Rgb {
  float r, g, b;
};
const Rgb kRed{1, 0, 0}, kGreen{0, 1, 0.12f}, kBlue{0, 0.15f, 1}, kAmber{1, 0.38f, 0}, kCyan{0, 0.75f, 1},
    kMagenta{1, 0, 0.8f}, kWhite{1, 1, 1}, kOff{0, 0, 0};

Rgb scale(Rgb c, float k) { return Rgb{c.r * k, c.g * k, c.b * k}; }

float breathe(uint32_t now, uint32_t period, float lo) {
  float ph = (float)(now % period) / period;
  return lo + (1.0f - lo) * (0.5f - 0.5f * cosf(6.2831853f * ph));
}

uint16_t ledDuty(float v, uint16_t gainPct) {
  if (v < 0) v = 0;
  if (v > 1) v = 1;
  v = v * v * kLightScale[gLightLevel & 3] * gainPct / 100.0f;  // gamma 2 for even fades
  return (uint16_t)(v * ((1u << kLedRes) - 1) + 0.5f);
}

uint8_t lastOnboard[3] = {1, 1, 1};

void writeLight(Rgb c) {
  if (gLed) {
    ledcWrite(LED_R, ledDuty(c.r, HFM_LED_GAIN_R));
    ledcWrite(LED_G, ledDuty(c.g, HFM_LED_GAIN_G));
    ledcWrite(LED_B, ledDuty(c.b, HFM_LED_GAIN_B));
  }
#if HFM_ONBOARD_RGB_PIN >= 0
  // On-board RGB LED: same scene, much dimmer (it is very bright); only on change.
  float k = kLightScale[gLightLevel & 3] * 24.0f;
  uint8_t v[3] = {(uint8_t)(c.r * c.r * k), (uint8_t)(c.g * c.g * k), (uint8_t)(c.b * c.b * k)};
  if (memcmp(v, lastOnboard, 3) != 0) {
    memcpy(lastOnboard, v, 3);
#if HFM_ONBOARD_RGB_SWAP_RG
    rgbLedWrite(HFM_ONBOARD_RGB_PIN, v[1], v[0], v[2]);
#else
    rgbLedWrite(HFM_ONBOARD_RGB_PIN, v[0], v[1], v[2]);
#endif
  }
#endif
}

struct Snap {
  LinkPhase link = LinkPhase::Idle;
  bool live = false;
  uint32_t cycles = 0;
  bool dtcValid = false;
  uint16_t dtcCount = 0;
  uint32_t dtcT = 0;
  bool faultStop = false;
  bool service = false;
};

Snap readSnap() {
  Snap s;
  Engine.lock();
  const EngineState& e = Engine.state();
  s.link = e.link;
  s.live = e.liveActive;
  s.cycles = e.liveCycles;
  s.dtcValid = e.dtc.valid;
  s.dtcCount = e.dtc.count;
  s.dtcT = e.dtc.tMs;
  s.faultStop = e.faultStop;
  s.service = e.serviceActive;
  Engine.unlock();
  return s;
}

Rgb scene(const Snap& s, uint32_t now, uint32_t bootMs, uint32_t flashUntil) {
  if (now - bootMs < 1200) {  // start-up sweep: red, green, blue
    uint32_t t = now - bootMs;
    return t < 400 ? scale(kRed, 0.7f) : (t < 800 ? scale(kGreen, 0.7f) : scale(kBlue, 0.7f));
  }
  uint32_t testUntil = gTestLightUntil;
  if (testUntil && (int32_t)(testUntil - now) > 0) {
    static const Rgb seq[] = {kRed, kGreen, kBlue, kWhite, kAmber, kCyan, kMagenta};
    uint32_t i = (3150 - (testUntil - now)) / 450;
    return seq[i < 7 ? i : 6];
  }
  if (s.faultStop) return (now / 250) % 2 ? kRed : kOff;
  if (s.service) return scale(kMagenta, breathe(now, 1200, 0.15f));
  switch (s.link) {
    case LinkPhase::NoAdapter: return scale(kRed, breathe(now, 3000, 0.06f));
    case LinkPhase::Connecting: return scale(kBlue, breathe(now, 2400, 0.08f));
    case LinkPhase::Recovering: return scale(kAmber, breathe(now, 1400, 0.08f));
    case LinkPhase::Error: return scale(kRed, 0.45f);
    case LinkPhase::Paused:
    case LinkPhase::Idle: return scale(kBlue, 0.25f);
    case LinkPhase::Ready: {
      // Stored faults: two amber flashes every 4 s over the base colour.
      if (s.dtcValid && s.dtcCount) {
        uint32_t t = now % 4000;
        if (t < 120 || (t >= 240 && t < 360)) return kAmber;
      }
      if (s.live) return (int32_t)(flashUntil - now) > 0 ? scale(kCyan, 1.0f) : scale(kCyan, 0.45f);
      return scale(kGreen, 0.6f);
    }
  }
  return kOff;
}

void task(void*) {
  Snap prev = readSnap();
  bool first = true, adapterSeen = false, wasRecovering = false;
  uint32_t readySince = 0, bootMs = millis(), lastLight = 0, flashUntil = 0, lastFlash = 0;
  uint32_t lastCycles = prev.cycles, lastDtcT = prev.dtcT;
  for (;;) {
    uint32_t now = millis();
    // 1. Sounds queued by events.
    if (!gMel.n) {
      Sound s;
      while (xQueueReceive(gQueue, &s, 0) == pdTRUE) {
        if (s == gLastSound && s != Sound::Test && now - gLastSoundMs < 1500) continue;  // no repeated chimes
        gLastSound = s;
        gLastSoundMs = now;
        if (gBuzzer && gSoundLevel) {
          startMelody(s);
          break;
        }
      }
    }
    bool playing = gBuzzer && playerTick();
    // 2. Engine state -> events and light (every 20 ms).
    if (now - lastLight >= 20) {
      lastLight = now;
      Snap s = readSnap();
      if (first) {
        // Engine still starting: only remember the state (no chimes for it).
        if (now - bootMs >= 300) {
          first = false;
          adapterSeen = s.link != LinkPhase::NoAdapter;
        }
      } else {
        if (s.link != prev.link) {
          if (s.link == LinkPhase::Ready) {
            indicatorPlay(wasRecovering ? Sound::Recovered : Sound::Connect);
            readySince = now;
            wasRecovering = false;
          } else if (prev.link == LinkPhase::Ready) {
            if (s.link == LinkPhase::Paused) indicatorPlay(Sound::Disconnect);
            else if (now - readySince > 1000) indicatorPlay(Sound::LinkLost);
            wasRecovering = s.link == LinkPhase::Recovering;
          }
          if (s.link == LinkPhase::NoAdapter && adapterSeen) indicatorPlay(Sound::AdapterOut);
          if (prev.link == LinkPhase::NoAdapter && s.link != LinkPhase::NoAdapter) {
            if (adapterSeen || now - bootMs > 1500) indicatorPlay(Sound::AdapterIn);
            adapterSeen = true;
          }
          if (s.link == LinkPhase::Paused || s.link == LinkPhase::Idle) wasRecovering = false;
        }
        if (s.dtcValid && s.dtcT != lastDtcT) indicatorPlay(s.dtcCount ? Sound::DtcPresent : Sound::DtcNone);
        if (s.faultStop && !prev.faultStop) indicatorPlay(Sound::Fault);
      }
      lastDtcT = s.dtcT;
      if (s.cycles != lastCycles) {
        lastCycles = s.cycles;
        if (now - lastFlash > 250) {  // activity flash, at most 4 per second
          lastFlash = now;
          flashUntil = now + 70;
        }
      }
      prev = s;
      writeLight(scene(s, now, bootMs, flashUntil));
    }
    vTaskDelay(pdMS_TO_TICKS(playing ? 2 : 10));
  }
}

}  // namespace

void indicatorBegin() {
  if (!gQueue) gQueue = xQueueCreate(8, sizeof(Sound));
#if HFM_LED_R_PIN >= 0 && HFM_LED_G_PIN >= 0 && HFM_LED_B_PIN >= 0
  // LEDs first: they share one timer (same frequency and resolution).
  gLed = ledcAttach(HFM_LED_R_PIN, kLedFreq, kLedRes) && ledcAttach(HFM_LED_G_PIN, kLedFreq, kLedRes) &&
         ledcAttach(HFM_LED_B_PIN, kLedFreq, kLedRes);
#if HFM_LED_COMMON_ANODE
  if (gLed) {
    ledcOutputInvert(HFM_LED_R_PIN, true);
    ledcOutputInvert(HFM_LED_G_PIN, true);
    ledcOutputInvert(HFM_LED_B_PIN, true);
  }
#endif
#endif
#if HFM_BUZZER_PIN >= 0
  // Different resolution -> its own timer; tone changes never disturb the LEDs.
  gBuzzer = ledcAttach(HFM_BUZZER_PIN, 2000, kBuzzRes);
  if (gBuzzer) ledcWrite(BUZ_PIN, 0);
#endif
  xTaskCreatePinnedToCore(task, "hfm-ind", 3072, nullptr, 1, nullptr, 1);
  indicatorPlay(Sound::Boot);
}

void indicatorPlay(Sound s) {
  if (gQueue && s != Sound::None) xQueueSend(gQueue, &s, 0);
}

void indicatorSetLevels(uint8_t sound, uint8_t light) {
  gSoundLevel = sound > 3 ? 3 : sound;
  gLightLevel = light > 3 ? 3 : light;
}

void indicatorTestLight() { gTestLightUntil = millis() + 3150; }
#endif
