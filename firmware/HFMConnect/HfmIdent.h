// HfmIdent.h - parsing of the 28-byte F0 identification block.
// Layout per REV 1.1 s.15:
//   0-1   manufacturer code: byte 1 = vendor letter, as the PC tester decodes
//         it ('B' BOSCH, 'V' VDO, 'S' SIEMENS); byte 0 = system group
//         (0x27/0x2C -> 1, 0x21/0x23 -> 2). 2C 42 = BOSCH (reference ECU).
//   2-11  ASCII identifier, shown as "XX XXXXX XXX"
//   12-21 ASCII ECU part number (10 digits), shown as "013 545 13 32"
//   22-23 hardware date week/year (BCD)
//   24-25 software date week/year (BCD)
//   26-27 diagnostic index "%02X/%02X" (e.g. 41/00)
// The raw bytes are always kept; interpretation is a separate layer.
#pragma once
#include <stdint.h>

#include "HfmTypes.h"

struct EcuIdent {
  bool present = false;      // 28 bytes were read without transport error
  uint8_t raw[28] = {0};
  uint8_t rawLen = 0;

  bool vendorKnown = false;
  uint8_t vendorId = 0;      // 0 unknown, 1 BOSCH, 2 VDO, 3 SIEMENS (byte 1 letter)
  uint8_t systemGroup = 0;   // byte 0: 1 (0x27/0x2C), 2 (0x21/0x23), 0 other
  char vendor[12] = {0};     // "BOSCH" / "VDO" / "SIEMENS" / "UNKNOWN"
  char vendorCode[6] = {0};  // raw bytes 0-1, "2C 42"
  char identifier[16] = {0}; // "M2 28320 VR0"
  char partNumber[16] = {0}; // "0135451332"
  char partDisplay[20] = {0};// "013 545 13 32"
  bool partValid = false;    // 10 ASCII digits
  int8_t hwWeek = -1, hwYear = -1;
  int8_t swWeek = -1, swYear = -1;
  uint8_t diagIndexRaw = 0;  // byte 26
  uint8_t diagSubRaw = 0;    // byte 27
  int16_t diagIndexBcd = -1; // byte 26 read as BCD (0x41 -> 41); -1 if not BCD
  char diagDisplay[8] = {0}; // "41/00"
  // Fingerprint used as stable profile key: part number + diag index.
  char fingerprint[24] = {0};
  // F0:26 / F0:27 - ROM page and start address of the live-data descriptor
  // table (read by the factory tester together with 00..1B). Optional.
  bool extPresent = false;
  uint8_t romPage = 0;
  uint8_t descAddr = 0;
};

// Parses raw identification bytes. Returns false if the block is too short.
bool parseIdent(const uint8_t* raw, uint8_t len, EcuIdent& out);

// BCD helpers (exposed for tests)
int16_t bcdToInt(uint8_t b);
