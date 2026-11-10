#ifdef ARDUINO
#include "NetManager.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "Config.h"
#include "Settings.h"

static DNSServer dns;
static char apSsid[33];
static uint32_t lastStaTry = 0;

const char* netApSsid() { return apSsid; }

void netApply() {
  WiFi.mode(AppSettings.staSsid[0] ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(apSsid, AppSettings.apPass, 6, 0, 4);
  if (AppSettings.staSsid[0]) {
    WiFi.begin(AppSettings.staSsid, AppSettings.staPass);
    lastStaTry = millis();
  }
}

void netBegin() {
  strlcpy(apSsid, HFM_AP_SSID, sizeof apSsid);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  netApply();
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  if (MDNS.begin(HFM_MDNS_NAME)) MDNS.addService("http", "tcp", HFM_HTTP_PORT);
}

void netLoop() {
  dns.processNextRequest();
  if (AppSettings.staSsid[0] && WiFi.status() != WL_CONNECTED && millis() - lastStaTry > 30000) {
    lastStaTry = millis();
    WiFi.begin(AppSettings.staSsid, AppSettings.staPass);
  }
}

void netStatusJson(JsonWriter& w) {
  w.kv("ap_ssid", apSsid).kv("ap_ip", WiFi.softAPIP().toString().c_str());
  w.kv("ap_clients", (int)WiFi.softAPgetStationNum());
  w.kv("sta_ssid", AppSettings.staSsid).kv("sta_connected", WiFi.status() == WL_CONNECTED);
  w.kv("sta_ip", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "");
  w.kv("rssi", WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0);
  w.kv("mdns", HFM_MDNS_NAME ".local");
}
#endif
