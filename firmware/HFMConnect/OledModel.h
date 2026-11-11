// OledModel.h - what the OLED shows: screen state machine of the display subsystem.
// Platform independent (host tests): no Arduino, no U8g2, no I/O.
//
// The display is an output only. The model gets a snapshot of the diagnostic
// state (copied by the display task) and the time; it never asks the engine for
// anything and cannot start a readout, erase faults or touch the session.
//
// Rules (docs/HARDWARE.md "OLED-дисплей"):
//   * the smiling face is shown only after a complete fault-memory readout with
//     no faults that was taken on the current connection;
//   * while the ECU link is lost nothing read earlier is shown as current; a
//     readout older than the last loss of the link is marked stale;
//   * faults are shown one by one (4 s each, longer when the description has to
//     scroll), after the last one the first follows again.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace oled {

enum class Link : uint8_t { NoAdapter, Idle, Connecting, Recovering, Ready, Error };

enum class Screen : uint8_t {
  Boot,        // logo + loading animation
  NoAdapter,   // USB K-Line adapter is not there
  Waiting,     // waiting for a connection to the ECU
  Connecting,  // wake-up / identification in progress
  Reading,     // diagnostics running (fault memory is being read)
  Connected,   // ECU on line, fault memory not read (or only a stale readout without faults)
  NoFaults,    // readout complete: no faults
  FaultsIntro, // readout complete: faults found (count), then the list
  Fault,       // one fault: code, description, i/N
  LinkLost,    // link to the ECU lost
  ReadError,   // the last readout failed
};

struct Fault {
  uint16_t code = 0;
  const char* text = "";  // UTF-8, static catalogue string
};

static const uint8_t kMaxFaults = 32;

// Snapshot of the diagnostic state (filled by the display task under the engine lock).
struct Input {
  Link link = Link::Idle;
  bool reading = false;    // a readout (fault memory / identification) is running right now
  bool dtcValid = false;   // a complete fault-memory readout exists
  bool dtcFailed = false;  // the last readout attempt failed (it is newer than the valid one)
  uint32_t dtcStamp = 0;   // time of the readout, same clock as update(nowMs)
  uint8_t count = 0;       // unique fault codes in the readout
  Fault faults[kMaxFaults];
};

// Text layout of the fault description (font 6 px wide on a 128 px display).
static const uint8_t kLineChars = 21;
static const uint8_t kVisibleLines = 3;
static const uint8_t kLinePx = 12;
static const uint8_t kMaxLines = 12;

static const uint16_t kBootMs = 2600;
static const uint16_t kIntroMs = 2500;
static const uint16_t kFaultMs = 4000;       // one fault, short description
static const uint16_t kScrollHoldMs = 1800;  // pause before and after scrolling
static const uint16_t kScrollLineMs = 900;   // time per scrolled line

struct View {
  Screen screen = Screen::Boot;
  uint32_t sinceMs = 0;   // time in this screen (animations)
  uint32_t key = 0;       // changes whenever the picture changes (redraw trigger)
  uint8_t count = 0;      // faults
  uint8_t index = 0;      // shown fault, 0-based
  uint16_t code = 0;
  const char* text = "";
  bool stale = false;     // readout is older than the last loss of the link
  bool recovering = false;// LinkLost: automatic recovery is running
  uint8_t lines = 0;      // wrapped lines of the description
  int16_t scrollPx = 0;   // vertical scroll of the description
};

// UTF-8 aware word wrap: fills start/len (bytes) of up to maxLines lines of at
// most `chars` characters; returns the number of lines.
uint8_t wrapText(const char* text, uint8_t chars, uint16_t* start, uint8_t* len, uint8_t maxLines);

class Model {
 public:
  // nowMs: monotonic milliseconds, same clock as Input::dtcStamp.
  void update(const Input& in, uint32_t nowMs);
  const View& view() const { return v_; }
  // Copies wrapped line i of the current description (zero terminated).
  void line(uint8_t i, char* out, size_t n) const;
  // Time one fault stays on screen (ms) for a description of `lines` lines.
  static uint32_t dwellMs(uint8_t lines);

 private:
  void enter(Screen s, uint32_t nowMs);
  void showFault(const Input& in, uint8_t i, uint32_t nowMs);

  View v_;
  bool started_ = false;
  uint32_t bootAt_ = 0, enteredAt_ = 0, faultAt_ = 0;
  bool wasReady_ = false, hadLoss_ = false;
  uint32_t lostAt_ = 0;
  uint32_t shownStamp_ = 0;  // readout the fault list / intro belongs to
  bool introDone_ = false;
  uint16_t lineStart_[kMaxLines] = {0};
  uint8_t lineLen_[kMaxLines] = {0};
};

}  // namespace oled
