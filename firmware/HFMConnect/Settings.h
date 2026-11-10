// Settings.h - persistent user settings (NVS via Preferences). Arduino only.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>

struct Settings {
  // K-Line timing (defaults = REV 1.1 profile; adjustable for a given adapter/ECU)
  uint16_t breakMs = 177;
  uint16_t postBreakMs = 214;
  uint16_t byteTimeoutMs = 750;
  uint8_t ftdiModem = 0;            // DTR (bit0) / RTS (bit1) asserted on the FTDI adapter
  bool wakeAuto = true;             // try other BREAK / DTR / RTS profiles while the ECU is silent
  bool experimentalReads = false;   // unknown-ECU reads, extended recipes, VIN, ext DTC
  bool engineerMode = false;        // REK-quarantined service ops (reference ECU only)
  bool keepAlive = true;            // idle F0:00 read while connected
  bool autoConnect = true;          // connect automatically when the adapter is present
  bool impulseEnabled = false;      // pulse-counter mode (grounds the line!)
  bool traceCapture = true;         // RAW TX/RX journal in RAM
  bool batteryManualConfirm = false;// operator confirmed >= 11 V (no ADC); reset on reboot, never stored
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
  // WebUI (stored on the device, not in the browser)
  uint8_t uiTheme = 0;              // 0 auto, 1 light, 2 dark
  char liveSel[72] = "rpm,maf,o2_mv,lambda";  // chart parameters, comma separated
  char liveCat[4] = "all";          // Live Data category filter
  char code9[10] = {0};             // 9-digit version code entered by the operator (archive only)
  // Learned on the vehicle (kept across reboots and firmware updates)
  uint16_t wakeLearnedBreak = 0;    // wake-up profile that answered 55 (0 = none)
  uint8_t wakeLearnedModem = 0;
  char ecuFp[24] = {0};             // ECU the flags below belong to (fingerprint)
  uint8_t ecuFlags = 0;             // ECU_F_* bits

  void load();
  void save();
  void factoryReset();
};

// ecuFlags bits: per-ECU knowledge learned by safe reads.
enum : uint8_t {
  ECU_F_NO_F0EXT = 0x01,   // F0:26/27 not answered: do not read again
  ECU_F_PLAN_KNOWN = 0x02, // live-data address plan determined
  ECU_F_PLAN_VDO = 0x04,   // ... and it is the VDO plan
};

extern Settings AppSettings;
#endif
