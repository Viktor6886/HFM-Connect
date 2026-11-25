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
Words smoothness(const uint8_t (&raw)[64],bool bosch,unsigned cylinders);
Words convert(uint16_t entry,const uint8_t (&r)[64],bool bosch,uint8_t diag,
              uint8_t revision,uint8_t cylinders,uint16_t parameter);
}
