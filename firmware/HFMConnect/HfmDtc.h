// HfmDtc.h - HFM fault-memory bitmap decoding (REV 1.1 s.11-14).
//
// F1 index 00..0F = bank A, 10..1F = bank B. Which bank is "stored" and which
// is "current" is the open question C-01 (sources disagree). The decoder
// keeps the raw bank and names it from the profile or, better, from the
// factory intermittent-fault rule observed in the readout.
#pragma once
#include <stdint.h>

#include "DtcCatalog.h"
#include "DtcVariantMaps.h"

namespace dtc {

// ECU variant for bit-map selection. The factory tester selects one of seven
// maps by cylinder count, vendor (B = Bosch, V = VDO) and the diagnosis
// identifier byte F0:26 compared with 'I' (0x49) and 'P' (0x50).
struct Variant {
  uint8_t manufacturer = 1;  // 1 = Bosch, 0 = other (VDO)
  uint8_t cylinders = 0;     // 4 (M111) / 6 (M104) / 0 = unknown
  uint8_t diagRaw = 0x41;    // raw F0:26 byte (0x41 = reference ECU "41/00")
};

// Selected factory map, nullptr when cylinders are unknown (generic table
// with positional overrides is used instead).
const DtcVariantMap* selectMap(const Variant& v);
// Code for bit position 1..152 under the variant (0 = position not used).
uint16_t codeForPosition(uint16_t pos, const Variant& v);

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
// Generic positional overrides (REV 1.1 s.14) used when no factory map is
// selected: tables are keyed by BIT POSITION (not by code), applied in order.
uint16_t applyRemaps(uint16_t pos, uint16_t code, const Variant& v);
// Id of the last remap rule that changed the code of this position ("" = none);
// only relevant when no factory variant map applies.
const char* remapRule(uint16_t pos, const Variant& v);
// Catalog lookup (nullptr if not in the 163-entry catalog).
const DtcCatalogEntry* lookup(uint16_t code);

struct Decoded {
  uint8_t bank;        // 0 = A (00..0F), 1 = B (10..1F), 2 = extended (30..32/40..42)
  uint8_t index;       // raw F1 index
  uint8_t bit;         // 0 = LSB
  uint16_t position;   // 1..152
  uint16_t baseCode;   // generic table code for the position
  uint16_t code;       // code for this ECU variant (0 = position not used)
  bool remapped;
  const char* mapId;   // factory map id ("C8EE"...) or "GENERIC"
  const DtcCatalogEntry* entry;  // may be nullptr (unknown code)
};

// Decodes one 16-byte bank. index0 is the first F1 index of the bank (0x00 or 0x10).
// Returns the number of set bits written to out (max outMax).
uint16_t decodeBank(const uint8_t* bytes16, uint8_t index0, const Variant& v, Decoded* out, uint16_t outMax);

// Status label for a raw bank under a mapping: "stored", "current" or "bank_a"/"bank_b".
const char* bankLabel(uint8_t bank, BankMapping m);

// Factory rule (Engine 104/111 Diagnostic Manual 11/3): intermittent-contact
// faults are stored only after repeated occurrence and "can appear only as
// Stored DTC's and never as Current DTC's". If set intermittent codes are
// found in exactly one bank, that bank is the stored one.
// Returns Unknown when the readout gives no evidence (or contradicts itself).
BankMapping inferBankMapping(const Decoded* items, uint16_t n);

// Engine-specific factory wording for codes whose meaning differs on M111
// (4 cylinders, coil pairs 1/4 and 2/3). nullptr = use the catalog text.
const char* m111Text(uint16_t code);

}  // namespace dtc
