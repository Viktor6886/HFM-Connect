#include "HfmDtc.h"

namespace dtc {

namespace {

// HFM_BASE_DTC_NUMBERS (HFMreader dtc <module> L14, 152 entries, CS).
const uint16_t kBase[152] = {
    2,   3,   4,   6,   7,   9,   10,  11,  118, 12,  113, 14,  15,  17,  18,  20,  21,  22,  23,  24,
    25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
    45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  61,  62,  63,  55,  56,  57,  58,  59,  60,  64,
    65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  119, 77,  79,  80,  81,  82,  83,  84,
    85,  239, 240, 86,  87,  88,  123, 124, 89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  116, 243,
    99,  100, 244, 117, 101, 102, 103, 104, 125, 105, 106, 107, 114, 108, 109, 115, 126, 110, 111, 112,
    5,   8,   13,  16,  19,  78,  247, 127, 128, 129, 130, 131, 132, 156, 157, 160, 133, 134, 135, 161,
    136, 253, 137, 138, 139, 140, 141, 142, 143, 144, 254, 255};

struct Pair {
  uint16_t from, to;
};

const Pair kGroupO[] = {{51, 55}, {52, 56}, {53, 57}, {54, 58}, {55, 59}, {56, 60}};
const Pair kDiag49BoschH[] = {{35, 45}, {36, 46}, {37, 41}, {38, 42}, {39, 47}, {40, 48}, {41, 39}, {42, 40}};
const Pair kDiag49Plus[] = {{70, 122}, {71, 121}, {74, 120}};
const Pair kDiag50to55[] = {{68, 145},  {69, 146},  {70, 147},  {93, 148},  {94, 149},  {100, 150}, {101, 151},
                            {102, 159}, {103, 152}, {114, 153}, {115, 154}, {127, 155}, {134, 156}, {135, 157},
                            {136, 160}, {140, 161}, {148, 162}, {149, 158}, {151, 163}, {152, 164}};

template <size_t N>
uint16_t mapGet(const Pair (&m)[N], uint16_t v) {
  for (size_t i = 0; i < N; i++)
    if (m[i].from == v) return m[i].to;
  return v;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

}  // namespace

uint16_t baseForPosition(uint16_t pos) { return (pos >= 1 && pos <= 152) ? kBase[pos - 1] : 0; }

uint16_t applyRemaps(uint16_t value, const Variant& v) {
  char g = lower(v.engineGroup);
  if (g == 'o') value = mapGet(kGroupO, value);
  if (v.diagIndex == 49 && v.manufacturer == 1 && g == 'h') value = mapGet(kDiag49BoschH, value);
  if (v.diagIndex >= 49) value = mapGet(kDiag49Plus, value);
  if (v.diagIndex >= 50 && v.diagIndex <= 55) value = mapGet(kDiag50to55, value);
  return value;
}

const DtcCatalogEntry* lookup(uint16_t code) {
  // Catalog is sorted by code (generator guarantees it): binary search.
  int lo = 0, hi = (int)DTC_CATALOG_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t c = DTC_CATALOG[mid].code;
    if (c == code) return &DTC_CATALOG[mid];
    if (c < code) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

uint16_t decodeBank(const uint8_t* bytes16, uint8_t index0, const Variant& v, Decoded* out, uint16_t outMax) {
  uint16_t n = 0;
  uint8_t bank = index0 >= 0x10 ? 1 : 0;
  for (uint8_t i = 0; i < 16; i++) {
    uint8_t b = bytes16[i];
    if (!b) continue;
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (!(b & (1u << bit))) continue;
      if (n >= outMax) return n;
      Decoded& d = out[n++];
      d.bank = bank;
      d.index = (uint8_t)(index0 + i);
      d.bit = bit;
      d.position = (uint16_t)(i * 8 + bit + 1);
      d.baseCode = baseForPosition(d.position);
      d.code = applyRemaps(d.baseCode, v);
      d.remapped = d.code != d.baseCode;
      d.entry = lookup(d.code);
    }
  }
  return n;
}

const char* bankLabel(uint8_t bank, BankMapping m) {
  if (m == BankMapping::AStoredBCurrent) return bank == 0 ? "stored" : "current";
  if (m == BankMapping::ACurrentBStored) return bank == 0 ? "current" : "stored";
  return bank == 0 ? "bank_a" : "bank_b";
}

}  // namespace dtc
