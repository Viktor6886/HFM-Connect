#ifdef ARDUINO
#include "Settings.h"

#include <Preferences.h>

#include "Config.h"

Settings AppSettings;

static Preferences prefs;

void Settings::load() {
  prefs.begin("hfm", true);
  source = prefs.getUChar("src", 0);
  simScenario = prefs.getUChar("simsc", 0);
  experimentalReads = prefs.getBool("exp", false);
  engineerMode = prefs.getBool("eng", false);
  keepAlive = prefs.getBool("ka", true);
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
  prefs.putUChar("src", source);
  prefs.putUChar("simsc", simScenario);
  prefs.putBool("exp", experimentalReads);
  prefs.putBool("eng", engineerMode);
  prefs.putBool("ka", keepAlive);
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
