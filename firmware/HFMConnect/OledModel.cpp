#include "OledModel.h"

#include <string.h>

namespace oled {

namespace {

// Bytes of the UTF-8 character starting at p (1..4; a stray continuation byte counts as 1).
uint8_t charBytes(const char* p) {
  uint8_t c = (uint8_t)*p;
  uint8_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
  for (uint8_t i = 1; i < n; i++)
    if (((uint8_t)p[i] & 0xC0) != 0x80) return i;  // truncated sequence
  return n;
}

}  // namespace

uint8_t wrapText(const char* text, uint8_t chars, uint16_t* start, uint8_t* len, uint8_t maxLines) {
  uint8_t lines = 0;
  if (!text || !chars) return 0;
  const char* p = text;
  while (*p && lines < maxLines) {
    while (*p == ' ') p++;  // no leading spaces
    if (!*p) break;
    const char* lineStart = p;
    const char* lastBreak = nullptr;  // end of the last complete word
    const char* q = p;
    uint8_t n = 0;
    while (*q && n < chars) {
      if (*q == ' ') lastBreak = q;
      q += charBytes(q);
      n++;
    }
    const char* end;
    if (!*q || *q == ' ') end = q;            // the rest fits / the line ends at a word end
    else if (lastBreak) end = lastBreak;      // wrap at the last space
    else end = q;                             // a word longer than the line: hard break
    while (end > lineStart && end[-1] == ' ') end--;
    start[lines] = (uint16_t)(lineStart - text);
    len[lines] = (uint8_t)(end - lineStart);
    lines++;
    p = end;
  }
  return lines;
}

uint32_t Model::dwellMs(uint8_t lines) {
  if (lines <= kVisibleLines) return kFaultMs;
  uint32_t t = 2u * kScrollHoldMs + (uint32_t)(lines - kVisibleLines) * kScrollLineMs;
  return t < kFaultMs ? kFaultMs : t;
}

void Model::enter(Screen s, uint32_t nowMs) {
  if (v_.screen != s) {
    v_.screen = s;
    enteredAt_ = nowMs;
  }
}

void Model::showFault(const Input& in, uint8_t i, uint32_t nowMs) {
  v_.index = i;
  v_.code = in.faults[i].code;
  v_.text = in.faults[i].text ? in.faults[i].text : "";
  v_.lines = wrapText(v_.text, kLineChars, lineStart_, lineLen_, kMaxLines);
  faultAt_ = nowMs;
}

void Model::line(uint8_t i, char* out, size_t n) const {
  if (!n) return;
  out[0] = 0;
  if (i >= v_.lines) return;
  size_t l = lineLen_[i] < n - 1 ? lineLen_[i] : n - 1;
  memcpy(out, v_.text + lineStart_[i], l);
  out[l] = 0;
}

void Model::update(const Input& in, uint32_t nowMs) {
  if (!started_) {
    started_ = true;
    bootAt_ = enteredAt_ = nowMs;
    v_.screen = Screen::Boot;
  }
  // Link history: a readout taken before the link was lost is not current.
  const bool ready = in.link == Link::Ready;
  if (wasReady_ && !ready) {
    hadLoss_ = true;
    lostAt_ = nowMs;
  }
  wasReady_ = ready;
  const bool stale = in.dtcValid && hadLoss_ && (int32_t)(in.dtcStamp - lostAt_) < 0;

  v_.count = in.dtcValid ? in.count : 0;
  v_.stale = false;
  v_.recovering = false;
  v_.scrollPx = 0;

  if (nowMs - bootAt_ < kBootMs) {
    enter(Screen::Boot, nowMs);
  } else if (in.link == Link::NoAdapter) {
    enter(Screen::NoAdapter, nowMs);
  } else if (in.link == Link::Idle) {
    enter(Screen::Waiting, nowMs);
  } else if (in.link == Link::Connecting) {
    enter(Screen::Connecting, nowMs);
  } else if (!ready) {  // Recovering / Error: the ECU was (or should be) there
    enter(Screen::LinkLost, nowMs);
    v_.recovering = in.link == Link::Recovering;
    v_.stale = in.dtcValid;  // whatever was read is no longer current
  } else if (in.reading) {
    enter(Screen::Reading, nowMs);
  } else if (in.dtcFailed) {
    enter(Screen::ReadError, nowMs);
  } else if (!in.dtcValid) {
    enter(Screen::Connected, nowMs);
  } else if (in.count == 0) {
    // The smiling face needs a readout of THIS connection.
    enter(stale ? Screen::Connected : Screen::NoFaults, nowMs);
    v_.stale = stale;
  } else {
    v_.stale = stale;
    const bool inList = v_.screen == Screen::Fault || v_.screen == Screen::FaultsIntro;
    if (!inList || in.dtcStamp != shownStamp_) {
      // New readout (or back from another screen): intro first, list from the start.
      if (in.dtcStamp != shownStamp_) introDone_ = false;
      shownStamp_ = in.dtcStamp;
      if (!introDone_) {
        enter(Screen::FaultsIntro, nowMs);
        enteredAt_ = nowMs;
      } else {
        enter(Screen::Fault, nowMs);
        showFault(in, 0, nowMs);
      }
    }
    if (v_.screen == Screen::FaultsIntro && nowMs - enteredAt_ >= kIntroMs) {
      introDone_ = true;
      enter(Screen::Fault, nowMs);
      showFault(in, 0, nowMs);
    }
    if (v_.screen == Screen::Fault) {
      if (v_.index >= in.count) showFault(in, 0, nowMs);
      uint32_t t = nowMs - faultAt_;
      if (t >= dwellMs(v_.lines)) {
        showFault(in, (uint8_t)((v_.index + 1) % in.count), nowMs);  // after the last one: the first again
        t = 0;
      }
      if (v_.lines > kVisibleLines) {
        // Hold, scroll smoothly line by line, hold: the whole text can be read.
        int32_t maxPx = (int32_t)(v_.lines - kVisibleLines) * kLinePx;
        int32_t px = t <= kScrollHoldMs ? 0 : (int32_t)((t - kScrollHoldMs) * kLinePx / kScrollLineMs);
        v_.scrollPx = (int16_t)(px > maxPx ? maxPx : px);
      }
    }
  }

  v_.sinceMs = nowMs - enteredAt_;
  // Redraw key: the picture only changes when this value changes.
  uint32_t k = 0;
  switch (v_.screen) {
    case Screen::Boot: k = v_.sinceMs / 60; break;
    case Screen::Connecting:
    case Screen::Reading: k = v_.sinceMs / 120; break;
    case Screen::NoAdapter:
    case Screen::Waiting: k = v_.sinceMs / 500; break;
    case Screen::LinkLost: k = (v_.sinceMs / 400) | (v_.recovering ? 0x10000u : 0) | (v_.stale ? 0x20000u : 0); break;
    case Screen::NoFaults: {
      uint32_t ph = v_.sinceMs % 3600;  // blink once per cycle
      k = ph >= 3300 && ph < 3460 ? 1 : 0;
      break;
    }
    case Screen::FaultsIntro: k = (v_.sinceMs / 300) | ((uint32_t)v_.count << 8); break;
    case Screen::Fault:
      k = ((uint32_t)v_.index << 24) | ((uint32_t)v_.count << 16) | (uint16_t)v_.scrollPx | (v_.stale ? 0x8000u : 0);
      k ^= (uint32_t)v_.code << 4;
      break;
    case Screen::Connected: k = v_.stale ? 1 : 0; break;
    default: break;
  }
  v_.key = k;
}

}  // namespace oled
