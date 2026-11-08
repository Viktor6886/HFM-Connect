// HfmDtc.h - HFM fault-memory bitmap decoding (REV 1.1 s.11-14, HFMreader dtc.py).
//
// F1 index 00..0F = bank A, 10..1F = bank B. Which bank is "stored" and which
// is "current" is the open question C-01 (HFMreader: A=stored, B=current;
// HFMScan: A=current, B=stored). The decoder keeps the raw bank and only
// names it when the selected ECU profile asserts a mapping.
#pragma once
#include <stdint.h>

#include "DtcCatalog.h"

namespace dtc {

struct Variant {
  uint8_t manufacturer = 1;  // 1 = Bosch (HFMreader default)
  char engineGroup = 'h';    // 'h' (HFM default) / 'o'
  int16_t diagIndex = 41;    // BCD-decoded diagnostic index
};

enum class BankMapping : uint8_t { Unknown, AStoredBCurrent, ACurrentBStored };

inline const char* bankMappingCode(BankMapping m) {
  switch (m) {
    case BankMapping::AStoredBCurrent: return "A_STORED_B_CURRENT";
    case BankMapping::ACurrentBStored: return "A_CURRENT_B_STORED";
    default: return "UNKNOWN";
  }
}

// Base DTC number for bit position 1..152 (0 if out of range).
uint16_t baseForPosition(uint16_t pos);
// Sequential remap exactly as HFMreader apply_variant_remaps (value = map.get(value, value)).
uint16_t applyRemaps(uint16_t base, const Variant& v);
// Catalog lookup (nullptr if not in the 163-entry catalog).
const DtcCatalogEntry* lookup(uint16_t code);

struct Decoded {
  uint8_t bank;        // 0 = A (00..0F), 1 = B (10..1F), 2 = extended (30..32/40..42)
  uint8_t index;       // raw F1 index
  uint8_t bit;         // 0 = LSB
  uint16_t position;   // 1..152
  uint16_t baseCode;   // before remap
  uint16_t code;       // after remap
  bool remapped;
  const DtcCatalogEntry* entry;  // may be nullptr (unknown code)
};

// Decodes one 16-byte bank. index0 is the first F1 index of the bank (0x00 or 0x10).
// Returns the number of set bits written to out (max outMax).
uint16_t decodeBank(const uint8_t* bytes16, uint8_t index0, const Variant& v, Decoded* out, uint16_t outMax);

// Status label for a raw bank under a mapping: "stored", "current" or "bank_a"/"bank_b".
const char* bankLabel(uint8_t bank, BankMapping m);

}  // namespace dtc
