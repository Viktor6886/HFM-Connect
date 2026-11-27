#pragma once
#include <stdint.h>
namespace hht {
struct Words {
  int16_t values[8]{};
  uint16_t valid = 0;
  void put(unsigned i, int64_t value) {
    if (i < 8) { values[i] = static_cast<int16_t>(value); valid |= 1u << i; }
  }
};
enum class CrankPhase { Wait, Settle, Sample, Done, LowVoltage, NoStarter, Released };
class CrankingWindow {
 public:
  explicit CrankingWindow(uint32_t now) : waiting_(now) {}
  CrankPhase step(uint32_t now,bool starter,int batteryDecivolts);
  bool withinSampleWindow(uint32_t now) const {return started_ && now-start_>=2010 && now-start_<10010;}
 private:
  uint32_t waiting_,start_=0;
  bool started_=false;
};
struct CrankingResult {
  uint16_t rpm[6]{},samples=0,mask=0;
  uint8_t cylinders=0;
  bool complete=false;
  void accumulate(const Words& sample);
};
Words crankingSpeeds(const uint8_t (&raw)[64],bool bosch,unsigned cylinders);
Words smoothness(const uint8_t (&raw)[64],bool bosch,unsigned cylinders);
Words convert(uint16_t entry,const uint8_t (&r)[64],bool bosch,uint8_t diag,
              uint8_t revision,uint8_t cylinders,uint16_t parameter);
}
