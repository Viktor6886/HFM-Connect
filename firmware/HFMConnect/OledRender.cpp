#include "Config.h"
#if !defined(ARDUINO) || HFM_OLED_ENABLE
#include "OledRender.h"

#include <stdio.h>

#ifdef ARDUINO
#include <U8g2lib.h>
#else
#include "u8g2.h"
#endif

#include "OledBitmaps.h"

namespace oled {

namespace {

// Fonts: Cyrillic incl. Ё/ё (U8g2 "_t_cyrillic" sets), digits for the fault code.
const uint8_t* const kTitle = u8g2_font_10x20_t_cyrillic;  // up to 12 characters per line
const uint8_t* const kBody = u8g2_font_6x12_t_cyrillic;    // 21 characters per line
const uint8_t* const kDigits = u8g2_font_logisoso22_tn;

const int kW = 128;

void bmp(u8g2_t* u, int x, int y, const OledBitmap& b) { u8g2_DrawXBMP(u, x, y, b.w, b.h, b.bits); }

int textW(u8g2_t* u, const uint8_t* font, const char* s) {
  u8g2_SetFont(u, font);
  return u8g2_GetUTF8Width(u, s);
}

void centered(u8g2_t* u, const uint8_t* font, int y, const char* s, int x0 = 0, int x1 = kW) {
  int w = textW(u, font, s);
  u8g2_DrawUTF8(u, x0 + (x1 - x0 - w) / 2, y, s);
}

// Subtitle with up to three animated dots after it (fixed layout: no jitter).
void subtitle(u8g2_t* u, const char* s, int dots = -1) {
  int w = textW(u, kBody, s);
  int total = dots >= 0 ? w + 14 : w;
  int x = (kW - total) / 2;
  u8g2_DrawUTF8(u, x, 61, s);
  for (int i = 0; i < dots; i++) u8g2_DrawBox(u, x + w + 3 + i * 4, 59, 2, 2);
}

// Icon, large title, small subtitle: the common layout of the state screens.
void status(u8g2_t* u, const OledBitmap* icon, const char* title, const char* sub, int dots = -1, int iconDx = 0) {
  if (icon) bmp(u, (kW - icon->w) / 2 + iconDx, 1, *icon);
  centered(u, kTitle, 47, title);
  if (sub) subtitle(u, sub, dots);
}

void drawBoot(u8g2_t* u, const View& v) {
  // The car rolls in from the right, then the name and a loading bar.
  uint32_t t = v.sinceMs;
  int x = t < 500 ? 32 + (int)((500 - t) * 96 / 500) : 32;
  bmp(u, x, 2, kOled_car);
  if (t >= 350) centered(u, kTitle, 45, "HFM Connect");
  u8g2_DrawRFrame(u, 22, 52, 84, 8, 3);
  int fill = (int)(t >= kBootMs ? 80 : (uint32_t)80 * t / kBootMs);
  if (fill > 3) u8g2_DrawRBox(u, 24, 54, fill, 4, 1);
}

void drawConnecting(u8g2_t* u, const View& v) {
  // Signal bars filling one after another.
  int phase = (int)(v.sinceMs / 240) % 5;  // 0..4 bars lit
  for (int i = 0; i < 4; i++) {
    int h = 7 + i * 6, x = 45 + i * 10, y = 28 - h;
    if (i < phase) u8g2_DrawBox(u, x, y, 7, h);
    else u8g2_DrawFrame(u, x, y, 7, h);
  }
  centered(u, kTitle, 47, "Подключение");
  subtitle(u, "к ЭБУ", (int)(v.sinceMs / 480) % 4);
}

void drawReading(u8g2_t* u, const View& v) {
  // The magnifier sweeps left and right over a row of "memory cells".
  static const int8_t kSweep[12] = {0, 4, 8, 12, 14, 12, 8, 4, 0, -4, -8, -12};
  int dx = kSweep[(v.sinceMs / 120) % 12];
  for (int i = 0; i < 7; i++) u8g2_DrawFrame(u, 30 + i * 10, 20, 7, 7);
  u8g2_SetDrawColor(u, 0);
  u8g2_DrawDisc(u, 59 + dx, 11, 10, U8G2_DRAW_ALL);
  u8g2_SetDrawColor(u, 1);
  bmp(u, 50 + dx, 1, kOled_icon_magnifier);
  centered(u, kTitle, 47, "Диагностика");
  subtitle(u, "чтение памяти ошибок");
}

void drawNoFaults(u8g2_t* u, const View& v) {
  bmp(u, 42, 0, v.key ? kOled_face_happy_blink : kOled_face_happy);
  centered(u, kTitle, 62, "Ошибок нет");
}

void drawIntro(u8g2_t* u, const View& v) {
  bmp(u, 8, 0, (v.sinceMs / 300) % 2 ? kOled_face_sad_tear : kOled_face_sad);
  char n[8];
  snprintf(n, sizeof n, "%u", (unsigned)v.count);
  centered(u, kBody, 12, "найдено", 60, kW);
  centered(u, kDigits, 40, n, 60, kW);
  centered(u, kTitle, 62, "Есть ошибки!");
}

void drawFault(u8g2_t* u, const Model& m) {
  const View& v = m.view();
  char s[16];
  snprintf(s, sizeof s, "%03u", (unsigned)v.code);
  u8g2_SetFont(u, kDigits);
  u8g2_DrawStr(u, 0, 23, s);
  snprintf(s, sizeof s, "%u/%u", (unsigned)v.index + 1, (unsigned)v.count);
  int w = textW(u, kTitle, s);
  u8g2_DrawUTF8(u, kW - w, v.stale ? 13 : 17, s);
  if (v.stale) {
    // Readout is older than the last loss of the link: an inverted tag on every fault.
    const char* st = "устарело";
    int sw = textW(u, kBody, st);
    u8g2_DrawBox(u, kW - sw - 4, 15, sw + 4, 10);
    u8g2_SetDrawColor(u, 0);
    u8g2_DrawUTF8(u, kW - sw - 2, 23, st);
    u8g2_SetDrawColor(u, 1);
  }
  u8g2_DrawHLine(u, 0, 26, kW);
  // Description: wrapped lines in a clipped window, scrolled when longer.
  const int top = 28, lineH = kLinePx;
  u8g2_SetClipWindow(u, 0, top, kW, 64);
  u8g2_SetFont(u, kBody);
  char line[2 * kLineChars + 4];
  for (uint8_t i = 0; i < v.lines; i++) {
    int base = top + 9 + i * lineH - v.scrollPx;
    if (base < top - 2 || base > 64 + lineH) continue;
    m.line(i, line, sizeof line);
    u8g2_DrawUTF8(u, 0, base, line);
  }
  u8g2_SetMaxClipWindow(u);
  if (v.lines > kVisibleLines) {
    // Thin scroll mark on the right edge.
    int track = 64 - top, thumb = track * kVisibleLines / v.lines;
    int maxPx = (v.lines - kVisibleLines) * lineH;
    int y = top + (track - thumb) * v.scrollPx / (maxPx ? maxPx : 1);
    u8g2_DrawVLine(u, kW - 1, y, thumb);
  }
}

}  // namespace

void render(u8g2_struct* u, const Model& m) {
  const View& v = m.view();
  u8g2_ClearBuffer(u);
  u8g2_SetDrawColor(u, 1);
  u8g2_SetFontMode(u, 1);
  u8g2_SetBitmapMode(u, 1);
  u8g2_SetFontPosBaseline(u);
  switch (v.screen) {
    case Screen::Boot: drawBoot(u, v); break;
    case Screen::NoAdapter: status(u, &kOled_icon_plug, "Нет адаптера", "подключите K-Line"); break;
    case Screen::Waiting: status(u, &kOled_icon_ecu, "Ожидание", "подключения к ЭБУ"); break;
    case Screen::Connecting: drawConnecting(u, v); break;
    case Screen::Reading: drawReading(u, v); break;
    case Screen::Connected:
      status(u, &kOled_icon_check, "ЭБУ на связи", v.stale ? "ошибки не перечитаны" : "ошибки не прочитаны");
      break;
    case Screen::NoFaults: drawNoFaults(u, v); break;
    case Screen::FaultsIntro: drawIntro(u, v); break;
    case Screen::Fault: drawFault(u, m); break;
    case Screen::LinkLost:
      if (v.recovering) status(u, &kOled_icon_lost, "Нет связи", "восстановление", (int)(v.sinceMs / 400) % 4);
      else status(u, &kOled_icon_lost, "Нет связи", "связь с ЭБУ потеряна");
      break;
    case Screen::ReadError: status(u, &kOled_icon_warning, "Ошибка", "чтения данных ЭБУ"); break;
  }
}

}  // namespace oled
#endif
