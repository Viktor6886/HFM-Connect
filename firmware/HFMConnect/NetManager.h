// NetManager.h - Wi-Fi access point (always on) + optional station, captive
// DNS and mDNS (http://hfm.local). Works fully offline. Arduino only.
#pragma once
#ifdef ARDUINO
#include "JsonWriter.h"

void netBegin();
void netLoop();
void netApply();  // re-apply settings (password / station credentials)
void netApplyCaptive();  // captive DNS on/off (Settings::captivePortal)
const char* netApSsid();
void netStatusJson(JsonWriter& w);
#endif
