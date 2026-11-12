#ifdef ARDUINO
#include "Settings.h"

#include <Preferences.h>

#include "Config.h"

Settings AppSettings;

static Preferences prefs;
static SemaphoreHandle_t gMtx = nullptr;

namespace {
struct PrefsLock {
  PrefsLock() {
    if (!gMtx) gMtx = xSemaphoreCreateMutex();
    xSemaphoreTake(gMtx, portMAX_DELAY);
  }
  ~PrefsLock() { xSemaphoreGive(gMtx); }
};
}  // namespace

void Settings::load() {
  PrefsLock guard;
  prefs.begin("hfm", true);
  breakMs = prefs.getUShort("brk", 177);
  postBreakMs = prefs.getUShort("pbrk", 214);
  byteTimeoutMs = prefs.getUShort("bto", 750);
  ftdiModem = prefs.getUChar("fmod", HFM_FTDI_MODEM) & 3;
  wakeAuto = prefs.getBool("wauto", true);
  experimentalReads = prefs.getBool("exp", false);
  engineerMode = prefs.getBool("eng", false);
  requireWriteKey = prefs.getBool("wkey", false);
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
  impClearMs = prefs.getUShort("impclr", 7000);
  uiTheme = prefs.getUChar("theme", 0) % 3;
  soundLevel = prefs.getUChar("snd", 2) & 3;
  lightLevel = prefs.getUChar("light", 2) & 3;
  dtcOnConnect = prefs.getBool("dtcconn", true);
  oledOn = prefs.getBool("oledon", true);
  oledType = prefs.getUChar("oledt", HFM_OLED_CONTROLLER) ? 1 : 0;
  oledFlip = prefs.getBool("oledf", false);
  oledLevel = prefs.getUChar("oledl", 2);
  if (oledLevel < 1 || oledLevel > 3) oledLevel = 2;
  if (!prefs.getString("lsel", liveSel, sizeof liveSel)) strlcpy(liveSel, "rpm,maf,o2_mv,lambda", sizeof liveSel);
  if (!prefs.getString("lcat", liveCat, sizeof liveCat)) strlcpy(liveCat, "all", sizeof liveCat);
  prefs.getString("code9", code9, sizeof code9);
  wakeLearnedBreak = prefs.getUShort("wlbrk", 0);
  wakeLearnedModem = prefs.getUChar("wlmod", 0) & 3;
  prefs.getString("ecufp", ecuFp, sizeof ecuFp);
  ecuFlags = prefs.getUChar("ecufl", 0);
  prefs.end();
  // Previous default AP password -> new default; a user password is kept.
  bool migrate = strcmp(apPass, HFM_AP_OLD_DEFAULT_PASS) == 0;
  if (migrate || strlen(apPass) < 8) strlcpy(apPass, HFM_AP_DEFAULT_PASS, sizeof apPass);
  if (migrate) {
    prefs.begin("hfm", false);
    prefs.putString("appass", apPass);
    prefs.end();
  }
  batteryManualConfirm = false;  // never persisted: must be confirmed per session
}

void Settings::save() {
  PrefsLock guard;
  prefs.begin("hfm", false);
  prefs.putUShort("brk", breakMs);
  prefs.putUShort("pbrk", postBreakMs);
  prefs.putUShort("bto", byteTimeoutMs);
  prefs.putUChar("fmod", ftdiModem);
  prefs.putBool("wauto", wakeAuto);
  prefs.putBool("exp", experimentalReads);
  prefs.putBool("eng", engineerMode);
  prefs.putBool("wkey", requireWriteKey);
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
  prefs.putUShort("impclr", impClearMs);
  prefs.putUChar("theme", uiTheme);
  prefs.putUChar("snd", soundLevel);
  prefs.putUChar("light", lightLevel);
  prefs.putBool("dtcconn", dtcOnConnect);
  prefs.putBool("oledon", oledOn);
  prefs.putUChar("oledt", oledType);
  prefs.putBool("oledf", oledFlip);
  prefs.putUChar("oledl", oledLevel);
  prefs.putString("lsel", liveSel);
  prefs.putString("lcat", liveCat);
  prefs.putString("code9", code9);
  prefs.putUShort("wlbrk", wakeLearnedBreak);
  prefs.putUChar("wlmod", wakeLearnedModem);
  prefs.putString("ecufp", ecuFp);
  prefs.putUChar("ecufl", ecuFlags);
  prefs.end();
}

void Settings::factoryReset() {
  {
    PrefsLock guard;
    prefs.begin("hfm", false);
    prefs.clear();
    prefs.end();
  }
  *this = Settings();
  load();
}
#endif
