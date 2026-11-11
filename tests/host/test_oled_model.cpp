// OLED display model (screen state machine, text wrapping). Software tests only:
// the display itself, I2C and the real picture are not covered here.
#include <string.h>

#include <string>

#include "OledModel.h"
#include "minitest.h"

using namespace oled;

namespace {

// UTF-8 character count.
size_t chars(const char* s) {
  size_t n = 0;
  for (; *s; s++)
    if (((uint8_t)*s & 0xC0) != 0x80) n++;
  return n;
}

Input ready() {
  Input in;
  in.link = Link::Ready;
  return in;
}

// Runs the model past the boot animation in the given state.
void boot(Model& m, const Input& in, uint32_t& t) {
  m.update(in, t);
  t += kBootMs + 10;
  m.update(in, t);
}

const char* kLong = "Регулирование распредвала: обрыв цепи или замыкание на массу электромагнита Y49, проверить жгут и разъём";

}  // namespace

TEST(oled_boot_then_link_states) {
  Model m;
  Input in;
  uint32_t t = 1000;
  m.update(in, t);
  CHECK(m.view().screen == Screen::Boot);
  m.update(in, t + kBootMs - 1);
  CHECK(m.view().screen == Screen::Boot);
  m.update(in, t + kBootMs);
  CHECK(m.view().screen == Screen::Waiting);  // leaves the logo by itself
  in.link = Link::NoAdapter;
  m.update(in, t + 5000);
  CHECK(m.view().screen == Screen::NoAdapter);
  in.link = Link::Connecting;
  m.update(in, t + 6000);
  CHECK(m.view().screen == Screen::Connecting);
  in.link = Link::Ready;
  m.update(in, t + 7000);
  CHECK(m.view().screen == Screen::Connected);  // on line, fault memory not read: no smiley
  in.reading = true;
  m.update(in, t + 7100);
  CHECK(m.view().screen == Screen::Reading);
}

TEST(oled_smiley_only_after_complete_readout_without_faults) {
  Model m;
  Input in = ready();
  uint32_t t = 0;
  boot(m, in, t);
  CHECK(m.view().screen == Screen::Connected);
  in.dtcFailed = true;  // readout failed: neither a smiley nor old data
  m.update(in, t += 100);
  CHECK(m.view().screen == Screen::ReadError);
  in.dtcFailed = false;
  in.dtcValid = true;
  in.dtcStamp = t;
  in.count = 0;
  m.update(in, t += 100);
  CHECK(m.view().screen == Screen::NoFaults);
  CHECK(!m.view().stale);
  // The picture is static between blinks: the redraw key does not change.
  uint32_t k = m.view().key;
  m.update(in, t += 1000);
  CHECK_EQ(m.view().key, k);
  bool blinked = false;
  for (int i = 0; i < 80; i++) {
    m.update(in, t += 50);
    if (m.view().key != k) blinked = true;
  }
  CHECK(blinked);
}

TEST(oled_faults_intro_then_cycle_and_wrap_around) {
  Model m;
  Input in = ready();
  uint32_t t = 0;
  boot(m, in, t);
  in.dtcValid = true;
  in.dtcStamp = t;
  in.count = 3;
  in.faults[0] = Fault{10, "Датчик массового расхода воздуха: обрыв цепи."};
  in.faults[1] = Fault{75, "Реле подогрева впуска PSV (K3/1)."};
  in.faults[2] = Fault{107, "Катушка зажигания."};
  m.update(in, t);
  CHECK(m.view().screen == Screen::FaultsIntro);
  CHECK_EQ(m.view().count, 3);
  m.update(in, t + kIntroMs - 1);
  CHECK(m.view().screen == Screen::FaultsIntro);
  t += kIntroMs;
  m.update(in, t);
  CHECK(m.view().screen == Screen::Fault);
  CHECK_EQ(m.view().index, 0);
  CHECK_EQ(m.view().code, 10);
  m.update(in, t + kFaultMs - 1);
  CHECK_EQ(m.view().index, 0);
  m.update(in, t += kFaultMs);
  CHECK_EQ(m.view().index, 1);
  CHECK_EQ(m.view().code, 75);
  m.update(in, t += kFaultMs);
  CHECK_EQ(m.view().index, 2);
  m.update(in, t += kFaultMs);
  CHECK_EQ(m.view().index, 0);  // after the last fault: the first again
  CHECK_EQ(m.view().code, 10);
  // A new readout starts with the intro and the first fault again.
  in.dtcStamp = t + 5;
  in.count = 1;
  m.update(in, t += 10);
  CHECK(m.view().screen == Screen::FaultsIntro);
  CHECK_EQ(m.view().count, 1);
  t += kIntroMs;
  m.update(in, t);
  CHECK(m.view().screen == Screen::Fault);
  m.update(in, t += kFaultMs);
  CHECK_EQ(m.view().index, 0);  // a single fault stays
}

TEST(oled_long_description_scrolls_fully_before_next_fault) {
  Model m;
  Input in = ready();
  uint32_t t = 0;
  boot(m, in, t);
  in.dtcValid = true;
  in.dtcStamp = t;
  in.count = 2;
  in.faults[0] = Fault{33, kLong};
  in.faults[1] = Fault{2, "Датчик температуры ОЖ."};
  m.update(in, t);
  t += kIntroMs;
  m.update(in, t);
  const uint8_t lines = m.view().lines;
  CHECK(lines > kVisibleLines);
  CHECK_EQ(m.view().scrollPx, 0);
  const uint32_t dwell = Model::dwellMs(lines);
  CHECK(dwell > kFaultMs);  // more than the 4 s of a short text
  m.update(in, t + kScrollHoldMs);
  CHECK_EQ(m.view().scrollPx, 0);  // held first
  int16_t prev = 0, maxPx = (int16_t)((lines - kVisibleLines) * kLinePx);
  bool monotonic = true;
  for (uint32_t dt = kScrollHoldMs; dt < dwell; dt += 40) {
    m.update(in, t + dt);
    if (m.view().scrollPx < prev) monotonic = false;
    prev = m.view().scrollPx;
    CHECK_EQ(m.view().index, 0);
  }
  CHECK(monotonic);
  CHECK_EQ(prev, maxPx);  // the last line became visible before switching
  m.update(in, t + dwell);
  CHECK_EQ(m.view().index, 1);
  CHECK_EQ(m.view().scrollPx, 0);
}

TEST(oled_link_loss_never_shows_old_data_as_current) {
  Model m;
  Input in = ready();
  uint32_t t = 0;
  boot(m, in, t);
  in.dtcValid = true;
  in.dtcStamp = t;
  in.count = 0;
  m.update(in, t += 100);
  CHECK(m.view().screen == Screen::NoFaults);
  in.link = Link::Recovering;
  m.update(in, t += 1000);
  CHECK(m.view().screen == Screen::LinkLost);
  CHECK(m.view().recovering);
  CHECK(m.view().stale);
  // Link is back, fault memory not read again: no smiley for the old readout.
  in.link = Link::Ready;
  m.update(in, t += 5000);
  CHECK(m.view().screen == Screen::Connected);
  CHECK(m.view().stale);
  // New readout on this connection: current again.
  in.dtcStamp = t;
  m.update(in, t += 100);
  CHECK(m.view().screen == Screen::NoFaults);
  CHECK(!m.view().stale);
  // With faults: after a loss the old list is shown only with the stale mark.
  in.count = 1;
  in.faults[0] = Fault{10, "Датчик массового расхода воздуха."};
  in.dtcStamp = t;
  m.update(in, t += 100);
  in.link = Link::Error;
  m.update(in, t += 1000);
  CHECK(m.view().screen == Screen::LinkLost);
  CHECK(!m.view().recovering);
  in.link = Link::Ready;
  m.update(in, t += 1000);
  m.update(in, t += kIntroMs + 10);
  CHECK(m.view().screen == Screen::Fault);
  CHECK(m.view().stale);
}

TEST(oled_wrap_cyrillic_words) {
  uint16_t st[kMaxLines];
  uint8_t ln[kMaxLines];
  const char* s = "Ёлка и ёж: проверка переноса длинных русских слов по строкам экрана";
  uint8_t n = wrapText(s, kLineChars, st, ln, kMaxLines);
  CHECK(n >= 3);
  std::string joined;
  for (uint8_t i = 0; i < n; i++) {
    std::string line(s + st[i], ln[i]);
    CHECK(chars(line.c_str()) <= kLineChars);
    CHECK(line.size() > 0 && line.front() != ' ' && line.back() != ' ');
    CHECK(((uint8_t)line[0] & 0xC0) != 0x80);  // never starts inside a UTF-8 character
    joined += (i ? " " : "") + line;
  }
  CHECK(joined == s);  // nothing lost, words not split
  // A word longer than the line is broken hard, at a character boundary.
  const char* w = "Электрогидравлическийпереключательклапан";
  n = wrapText(w, 10, st, ln, kMaxLines);
  CHECK_EQ(n, 4);
  CHECK_EQ(chars(std::string(w + st[0], ln[0]).c_str()), 10u);
  CHECK_EQ(wrapText("", kLineChars, st, ln, kMaxLines), 0);
  CHECK_EQ(wrapText("   ", kLineChars, st, ln, kMaxLines), 0);
  // Model::line copies whole lines and terminates them.
  Model m;
  Input in = ready();
  uint32_t t = 0;
  boot(m, in, t);
  in.dtcValid = true;
  in.dtcStamp = t;
  in.count = 1;
  in.faults[0] = Fault{33, kLong};
  m.update(in, t);
  m.update(in, t += kIntroMs);
  char buf[64];
  m.line(0, buf, sizeof buf);
  CHECK(strlen(buf) > 0 && chars(buf) <= kLineChars);
  m.line(200, buf, sizeof buf);
  CHECK_EQ(strlen(buf), 0u);
}
