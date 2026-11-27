#pragma once
#include <stdint.h>

namespace scanner {
struct DutySample {
  uint16_t lowPermille=0, minimum=0, maximum=0, cycles=0;
};
// Timings refer to the sampling clock measured after the button is released.
class BlinkCounter {
 public:
  explicit BlinkCounter(uint32_t hz=8000,bool senseIdle=false) : minLow_(hz*60/1000),maxLow_(hz*2),
      endGap_(hz*5/2),startTimeout_(hz*6),senseIdle_(senseIdle) {}
  void sample(bool high);
  bool done() const {return done_;}
  bool idleWindow() const {return done_ && high_ && !pulses_ && phase_>=startTimeout_;}
  uint8_t result() const {return done_ && !bad_ ? pulses_ : 0;}
 private:
  uint32_t minLow_,maxLow_,endGap_,startTimeout_,phase_=0;
  uint8_t pulses_=0;
  bool high_=true, done_=false, bad_=false, senseIdle_=false, first_=true, idleHigh_=true;
};
// Runs in sample-clock units. Only full falling-edge to falling-edge cycles count.
class DutyCounter {
 public:
  void sample(bool high);
  bool finish(DutySample& out) const;
 private:
  bool high_=true, seen_=false, started_=false;
  uint32_t tick_=0, fall_=0, rise_=0, firstPeriod_=0;
  uint32_t lowTotal_=0, periodTotal_=0;
  uint16_t shortest_=1000, longest_=0, cycles_=0;
  bool invalid_=false;
};
}
