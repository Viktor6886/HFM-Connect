#ifdef ARDUINO
#include "Settings.h"

#include <Preferences.h>

#include "Config.h"

Settings AppSettings;

static Preferences prefs;

void Settings::load() {
  prefs.begin("hfm", true);
  breakMs = prefs.getUShort("brk", 177);
  postBreakMs = prefs.getUShort("pbrk", 214);
  byteTimeoutMs = prefs.getUShort("bto", 750);
  ftdiModem = prefs.getUChar("fmod", HFM_FTDI_MODEM) & 3;
  wakeAuto = prefs.getBool("wauto", true);
  experimentalReads = prefs.getBool("exp", false);
  engineerMode = prefs.getBool("eng", false);
  keepAlive = prefs.getBool("ka", true);
  autoConnect = prefs.getBool("auto", true);
  impulseEnabled = prefs.getBool("imp", false);
  traceCapture = prefs.getBool("trace", true);
  vehicleIdx = prefs.getShort("veh", -1);
  productionYm = prefs.getUInt("ym", 0);
  maskVin = prefs.getBool("maskvin", true);
  prefs.getString("vin", vin, sizeof vin);
  if (!prefs.getString("appass", apPass, sizeof apPass)) apPass[0] = 0;
  prefs.getString("stassid", staSsid, sizeof staSsid);
  prefs.getString("stapass", staPass, sizeof staPass);
  impPressMs = prefs.getUShort("imppr", 3000);
  impEndGapMs = prefs.getUShort("impgap", 2500);
  prefs.end();
  if (strlen(apPass) < 8) strlcpy(apPass, HFM_AP_DEFAULT_PASS, sizeof apPass);
  batteryManualConfirm = false;  // never persisted: must be confirmed per session
}

void Settings::save() {
  prefs.begin("hfm", false);
  prefs.putUShort("brk", breakMs);
  prefs.putUShort("pbrk", postBreakMs);
  prefs.putUShort("bto", byteTimeoutMs);
  prefs.putUChar("fmod", ftdiModem);
  prefs.putBool("wauto", wakeAuto);
  prefs.putBool("exp", experimentalReads);
  prefs.putBool("eng", engineerMode);
  prefs.putBool("ka", keepAlive);
  prefs.putBool("auto", autoConnect);
  prefs.putBool("imp", impulseEnabled);
  prefs.putBool("trace", traceCapture);
  prefs.putShort("veh", vehicleIdx);
  prefs.putUInt("ym", productionYm);
  prefs.putBool("maskvin", maskVin);
  prefs.putString("vin", vin);
  prefs.putString("appass", apPass);
  prefs.putString("stassid", staSsid);
  prefs.putString("stapass", staPass);
  prefs.putUShort("imppr", impPressMs);
  prefs.putUShort("impgap", impEndGapMs);
  prefs.end();
}

void Settings::factoryReset() {
  prefs.begin("hfm", false);
  prefs.clear();
  prefs.end();
  *this = Settings();
  load();
}
#endif
