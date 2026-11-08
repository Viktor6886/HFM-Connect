// Settings.h - persistent user settings (NVS via Preferences). Arduino only.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>

struct Settings {
  // diagnostic source: 0 = K-Line hardware, 1 = built-in simulator (synthetic)
  uint8_t source = 0;
  uint8_t simScenario = 0;          // SimEcu::Scenario
  bool experimentalReads = false;   // unknown-ECU reads, HHT recipes, VIN, ext DTC
  bool engineerMode = false;        // REK-quarantined service ops (reference ECU only)
  bool keepAlive = true;            // idle F0:00 read while connected
  bool impulseEnabled = false;      // pulse-counter mode (grounds the line!)
  bool traceCapture = true;         // RAW TX/RX journal in RAM
  bool batteryManualConfirm = false;// operator confirmed >= 11 V (no ADC); reset on reboot
  int16_t vehicleIdx = -1;          // prof::vehicles() index, -1 = not selected
  uint32_t productionYm = 0;        // YYYYMM, 0 = unknown
  char vin[18] = {0};               // operator-entered (masked in exports by default)
  bool maskVin = true;
  // Wi-Fi
  char apPass[33] = {0};
  char staSsid[33] = {0};
  char staPass[65] = {0};
  // impulse thresholds
  uint16_t impPressMs = 3000;
  uint16_t impEndGapMs = 2500;

  void load();
  void save();
  void factoryReset();
};

extern Settings AppSettings;
#endif
