// WebApi.h - embedded HTTP server: WebUI (gzip, PROGMEM) + JSON REST API.
// Uses only the WebServer library bundled with the ESP32 Arduino core.
#pragma once
#ifdef ARDUINO
void webBegin();
void webLoop();
#endif
