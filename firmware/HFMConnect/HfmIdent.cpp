#include "HfmIdent.h"

#include <stdio.h>
#include <string.h>

int16_t bcdToInt(uint8_t b) {
  uint8_t hi = b >> 4, lo = b & 0x0F;
  if (hi > 9 || lo > 9) return -1;
  return (int16_t)(hi * 10 + lo);
}

static void copyAscii(const uint8_t* src, uint8_t n, char* dst) {
  for (uint8_t i = 0; i < n; i++) {
    uint8_t c = src[i];
    dst[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
  }
  dst[n] = 0;
}

bool parseIdent(const uint8_t* raw, uint8_t len, EcuIdent& o) {
  o = EcuIdent();
  uint8_t n = len > 28 ? 28 : len;
  memcpy(o.raw, raw, n);
  o.rawLen = n;
  if (len < 28) return false;
  o.present = true;

  o.vendorKnown = (raw[0] == 0x2C && raw[1] == 0x42);
  snprintf(o.vendor, sizeof o.vendor, "%s", o.vendorKnown ? "BOSCH" : "UNKNOWN");

  char id[11];
  copyAscii(raw + 2, 10, id);
  snprintf(o.identifier, sizeof o.identifier, "%.2s %.5s %.3s", id, id + 2, id + 7);

  copyAscii(raw + 12, 10, o.partNumber);
  o.partValid = true;
  for (int i = 0; i < 10; i++)
    if (o.partNumber[i] < '0' || o.partNumber[i] > '9') o.partValid = false;
  snprintf(o.partDisplay, sizeof o.partDisplay, "%.3s %.3s %.2s %.2s", o.partNumber, o.partNumber + 3,
           o.partNumber + 6, o.partNumber + 8);

  int16_t w = bcdToInt(raw[22]), y = bcdToInt(raw[23]);
  if (w >= 1 && w <= 53 && y >= 0) { o.hwWeek = (int8_t)w; o.hwYear = (int8_t)y; }
  w = bcdToInt(raw[24]);
  y = bcdToInt(raw[25]);
  if (w >= 1 && w <= 53 && y >= 0) { o.swWeek = (int8_t)w; o.swYear = (int8_t)y; }

  o.diagIndexRaw = raw[26];
  o.diagSubRaw = raw[27];
  o.diagIndexBcd = bcdToInt(raw[26]);
  snprintf(o.diagDisplay, sizeof o.diagDisplay, "%02X/%02X", raw[26], raw[27]);
  snprintf(o.fingerprint, sizeof o.fingerprint, "%s/%02X%02X", o.partValid ? o.partNumber : "??????????",
           raw[26], raw[27]);
  return true;
}
