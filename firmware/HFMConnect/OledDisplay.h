// OledDisplay.h - OLED 0.96" 128x64 (SSD1306 / SH1106, I2C) as an independent
// display subsystem. Arduino only; the calls below are no-ops when the firmware
// is built without the display (HFM_OLED_ENABLE 0).
//
// Independence from the diagnostic core (D-032):
//   * own FreeRTOS task, lowest application priority, on the core opposite to the
//     diagnostic engine; all I2C traffic, drawing and animation happen there;
//   * input is a snapshot of EngineState copied under the engine lock (a few
//     microseconds, no I/O while the lock is held); the engine never waits for
//     the display and nothing in the K-Line / USB path calls into this module;
//   * output only: the module cannot submit jobs, read or erase anything;
//   * a missing, unplugged or hung display only affects this task: the bus is
//     probed, drawing stops while the display does not answer and resumes
//     when it is back.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>

struct OledStatus {
  bool built = false;    // firmware contains the display code
  bool enabled = false;  // switched on in the settings
  bool present = false;  // display answers on the I2C bus
  uint8_t address = 0;   // 0x3C / 0x3D
  uint32_t frames = 0;   // frames sent since boot
};

// Starts the display task (does nothing without HFM_OLED_ENABLE).
void oledBegin();
// Settings: on/off, controller (0 SSD1306, 1 SH1106), rotate 180 deg, brightness 1..3.
void oledConfigure(bool on, uint8_t controller, bool flip, uint8_t level);
OledStatus oledStatus();

#endif
