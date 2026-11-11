// PC preview of the OLED screens: runs the real model and renderer against a
// U8g2 frame buffer (no display, dummy I2C) and writes the frames to a file.
// Built and run by tools/oled_preview.py.
#include <stdio.h>
#include <string.h>

#include "DtcCatalog.h"
#include "HfmDtc.h"
#include "OledModel.h"
#include "OledRender.h"
#include "u8g2.h"

using namespace oled;

static uint8_t gBuf[1024];
static u8g2_t gU;
static FILE* gOut;

static uint8_t dummyCb(u8x8_t*, uint8_t, uint8_t, void*) { return 1; }

static void frame(const char* name, const Model& m) {
  render(&gU, m);
  char n[32] = {0};
  strncpy(n, name, sizeof n - 1);
  fwrite(n, 1, sizeof n, gOut);
  fwrite(gBuf, 1, sizeof gBuf, gOut);
}

static const char* text(uint16_t code) {
  const DtcCatalogEntry* e = dtc::lookup(code);
  return e ? e->ru : "?";
}

int main(int argc, char** argv) {
  gOut = fopen(argc > 1 ? argv[1] : "frames.bin", "wb");
  if (!gOut) return 1;
  u8g2_SetupDisplay(&gU, u8x8_d_ssd1306_128x64_noname, u8x8_cad_001, dummyCb, dummyCb);
  u8g2_SetupBuffer(&gU, gBuf, 8, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  u8g2_InitDisplay(&gU);

  Input in;
  uint32_t t = 0;
  {
    Model m;
    m.update(in, t);
    m.update(in, t + 250);
    frame("boot_250ms", m);
    m.update(in, t + 1500);
    frame("boot_1500ms", m);
    t += kBootMs;
    in.link = Link::NoAdapter;
    m.update(in, t);
    frame("no_adapter", m);
    in.link = Link::Idle;
    m.update(in, t += 100);
    frame("waiting", m);
    in.link = Link::Connecting;
    m.update(in, t += 100);
    m.update(in, t += 500);
    frame("connecting_a", m);
    m.update(in, t += 480);
    frame("connecting_b", m);
    in.link = Link::Ready;
    m.update(in, t += 100);
    frame("connected", m);
    in.reading = true;
    m.update(in, t += 100);
    m.update(in, t += 360);
    frame("reading", m);
    in.reading = false;
    in.dtcFailed = true;
    m.update(in, t += 100);
    frame("read_error", m);
    in.dtcFailed = false;
    in.dtcValid = true;
    in.dtcStamp = t;
    in.count = 0;
    m.update(in, t += 100);
    frame("no_faults", m);
    m.update(in, t += 3350);
    frame("no_faults_blink", m);
    in.link = Link::Recovering;
    m.update(in, t += 100);
    m.update(in, t += 900);
    frame("link_lost_recovering", m);
    in.link = Link::Error;
    m.update(in, t += 100);
    frame("link_lost", m);
    in.link = Link::Ready;
    m.update(in, t += 100);
    frame("connected_stale", m);
  }
  {
    // Faults from the real catalogue (texts as shown by the WebUI).
    Model m;
    Input f;
    f.link = Link::Ready;
    uint32_t x = 0;
    m.update(f, x);
    x += kBootMs;
    f.dtcValid = true;
    f.dtcStamp = x;
    static const uint16_t codes[] = {10, 3, 107, 117, 75};
    f.count = 5;
    for (int i = 0; i < 5; i++) f.faults[i] = Fault{codes[i], text(codes[i])};
    m.update(f, x);
    frame("faults_intro", m);
    m.update(f, x + 300);
    frame("faults_intro_tear", m);
    x += kIntroMs;
    m.update(f, x);
    frame("fault_1_of_5", m);
    x += Model::dwellMs(m.view().lines);
    m.update(f, x);
    frame("fault_2_of_5", m);
    x += Model::dwellMs(m.view().lines);
    m.update(f, x);
    frame("fault_3_long_top", m);
    uint32_t d = Model::dwellMs(m.view().lines);
    m.update(f, x + d / 2);
    frame("fault_3_long_scrolling", m);
    m.update(f, x + d - 100);
    frame("fault_3_long_end", m);
    // Link lost and restored without a new readout: the list is marked stale.
    f.link = Link::Recovering;
    m.update(f, x + d + 100);
    f.link = Link::Ready;
    m.update(f, x + d + 200);
    m.update(f, x + d + 200 + kIntroMs);
    frame("fault_stale", m);
  }
  fclose(gOut);
  return 0;
}
