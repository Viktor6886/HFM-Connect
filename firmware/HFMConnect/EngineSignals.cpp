#include "EngineSignals.h"
namespace scanner {
void BlinkCounter::sample(bool high) {
  if(done_)return;
  if(first_) {
    if(senseIdle_)idleHigh_=high;
    first_=false;
  }
  high=high==idleHigh_;
  if(high!=high_) {
    if(!high_) {
      if(phase_<minLow_ || phase_>maxLow_ || pulses_==64)bad_=true;
      else ++pulses_;
    } else if(!phase_)bad_=true; // Acquisition started in a low phase.
    high_=high;phase_=0;
  }
  ++phase_;
  if(bad_ || (!high && phase_>maxLow_) || (high && !pulses_ && phase_>=startTimeout_)) {
    bad_=true;done_=true;
  } else if(high && pulses_ && phase_>=endGap_)done_=true;
}
void DutyCounter::sample(bool high) {
  if(!seen_) {high_=high;seen_=true;++tick_;return;}
  if(high!=high_) {
    if(high) rise_=tick_;
    else {
      if(started_) {
        uint32_t period=tick_-fall_,low=rise_-fall_;
        // A few samples cannot resolve duty, and a lost edge changes the period.
        if(period<16 || low>=period ||
           (firstPeriod_ && (period*4<firstPeriod_*3 || period*4>firstPeriod_*5))) invalid_=true;
        if(!firstPeriod_) firstPeriod_=period;
        uint16_t duty=uint16_t((1000*low+period/2)/period);
        if(duty<shortest_)shortest_=duty;
        if(duty>longest_)longest_=duty;
        lowTotal_+=low;periodTotal_+=period;++cycles_;
      }
      fall_=tick_;started_=true;
    }
    high_=high;
  }
  ++tick_;
}
bool DutyCounter::finish(DutySample& out) const {
  out={};
  if(invalid_ || cycles_<8 || !periodTotal_ || tick_-fall_>firstPeriod_*2)return false;
  out.lowPermille=uint16_t((1000*lowTotal_+periodTotal_/2)/periodTotal_);
  out.minimum=shortest_;out.maximum=longest_;out.cycles=cycles_;
  return true;
}
}
