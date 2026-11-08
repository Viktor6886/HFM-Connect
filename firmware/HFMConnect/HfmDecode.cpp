#include "HfmDecode.h"

namespace hfm {

namespace {

struct Pt {
  float x, y;
};

// Coolant 14-point table (REV 1.1 s.7).
const Pt kCoolantPts[14] = {{0, -59},   {4, -35},   {16, -17},  {32, -3},   {48, 8},
                            {128, 45},  {160, 61},  {176, 70},  {192, 80},  {208, 93},
                            {224, 112}, {240, 147}, {252, 207}, {256, 300}};
// Intake air B17 ADC 8-point candidate (REV 1.1 s.7).
const Pt kIatPts[8] = {{92.16f, 10},  {122.88f, 20}, {148.48f, 30}, {174.08f, 40},
                       {194.56f, 50}, {209.92f, 60}, {220.16f, 70}, {230.4f, 80}};

float interp(const Pt* p, int n, float raw) {
  if (raw <= p[0].x) return p[0].y;
  if (raw >= p[n - 1].x) return p[n - 1].y;
  for (int i = 1; i < n; i++)
    if (raw <= p[i].x) return p[i - 1].y + (p[i].y - p[i - 1].y) * (raw - p[i - 1].x) / (p[i].x - p[i - 1].x);
  return p[n - 1].y;
}

// 256-entry temperature tables (REV 1.1 s.7.2).
const int16_t kCoolantTable[256] = {
    -59, -52, -46, -42, -39, -36, -34, -31, -29, -27, -26, -24, -23, -21, -20, -19, -17, -16, -15, -14, -13, -12,
    -11, -10, -9,  -8,  -7,  -7,  -6,  -5,  -4,  -3,  -3,  -2,  -1,  -1,  0,   1,   2,   2,   3,   4,   4,   5,
    5,   6,   7,   7,   8,   8,   9,   9,   10,  11,  11,  12,  12,  13,  13,  14,  14,  15,  15,  16,  16,  17,
    17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  23,  23,  24,  24,  25,  25,  26,  26,  27,  27,
    28,  28,  28,  29,  29,  30,  30,  31,  31,  32,  32,  32,  33,  33,  34,  34,  35,  35,  35,  36,  36,  37,
    37,  38,  38,  39,  39,  39,  40,  40,  41,  41,  42,  42,  42,  43,  43,  44,  44,  45,  45,  46,  46,  47,
    47,  47,  48,  48,  49,  49,  50,  50,  51,  51,  52,  52,  53,  53,  54,  54,  54,  55,  55,  56,  57,  57,
    57,  58,  58,  59,  59,  60,  61,  61,  62,  62,  63,  63,  64,  64,  65,  65,  66,  67,  67,  68,  68,  69,
    69,  70,  71,  71,  72,  73,  73,  74,  74,  75,  76,  76,  77,  78,  79,  79,  80,  81,  81,  82,  83,  84,
    85,  85,  86,  87,  88,  89,  90,  91,  91,  92,  93,  94,  95,  96,  97,  98,  99,  101, 102, 103, 104, 105,
    107, 108, 109, 111, 112, 114, 115, 117, 119, 120, 122, 124, 126, 128, 131, 133, 135, 138, 141, 144, 147, 151,
    154, 159, 163, 168, 174, 181, 189, 198, 210, 232, 255, 277, 300, 300};

const int16_t kIatTable[256] = {
    -40, -40, -40, -40, -40, -40, -40, -40, -40, -39, -37, -36, -34, -33, -31, -30, -29, -28, -27, -26, -26, -25,
    -24, -23, -22, -21, -20, -19, -19, -18, -18, -17, -16, -16, -15, -14, -14, -13, -13, -12, -11, -11, -10, -10,
    -9,  -9,  -8,  -8,  -7,  -7,  -7,  -6,  -6,  -5,  -5,  -4,  -4,  -4,  -3,  -3,  -2,  -2,  -1,  -1,  0,   0,
    0,   1,   1,   2,   2,   2,   3,   3,   4,   4,   4,   5,   5,   5,   6,   6,   7,   7,   7,   8,   8,   9,
    9,   9,   10,  10,  10,  11,  11,  11,  12,  12,  12,  13,  13,  13,  14,  14,  15,  15,  15,  16,  16,  16,
    17,  17,  17,  18,  18,  18,  19,  19,  19,  20,  20,  20,  21,  21,  21,  22,  22,  23,  23,  23,  24,  24,
    24,  25,  25,  25,  26,  26,  26,  27,  27,  28,  28,  28,  29,  29,  29,  30,  30,  30,  31,  31,  32,  32,
    33,  33,  33,  34,  34,  35,  35,  35,  36,  36,  37,  37,  38,  38,  38,  39,  39,  40,  40,  41,  41,  42,
    42,  43,  43,  44,  44,  45,  45,  46,  46,  47,  47,  48,  48,  49,  49,  50,  50,  51,  51,  52,  53,  53,
    54,  54,  55,  56,  56,  57,  58,  58,  59,  59,  60,  61,  62,  63,  63,  64,  65,  66,  67,  68,  68,  69,
    70,  71,  72,  73,  74,  76,  77,  78,  79,  80,  81,  83,  84,  86,  87,  89,  90,  92,  94,  96,  98,  100,
    103, 107, 110, 113, 117, 120, 125, 130, 135, 140, 150, 150, 150, 150};

const uint8_t kBitV1Old[32] = {0x80, 0x40, 0x08, 0x02, 0x04, 0x08, 0x10, 0x20, 0x01, 0x40, 0x04,
                               0x04, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0};

}  // namespace

float coolantNativeC(uint8_t bf) { return interp(kCoolantPts, 14, bf); }

bool iatNativeC(uint8_t be, float* out) {
  if (be < kIatPts[0].x || be > kIatPts[7].x) return false;
  *out = interp(kIatPts, 8, be);
  return true;
}

int16_t coolantTableC(uint8_t raw) { return kCoolantTable[raw]; }
int16_t iatTableC(uint8_t raw) { return kIatTable[raw]; }

void idleTarget(uint8_t d2, uint8_t d6, uint8_t d8, uint8_t i6a, uint16_t* target, uint16_t* rpmOut) {
  *target = d8 ? (uint16_t)(25 * (i6a < 0x41 ? d2 : d6)) : 0;
  *rpmOut = (uint16_t)(25 * d8);
}

uint8_t statusMask(uint16_t q) { return kBitV1Old[(q & 0xFF) & 31]; }

bool statusBit(uint8_t d0, uint16_t q) {
  bool active = (d0 & statusMask(q)) != 0;
  if ((q >> 8) & 2) active = !active;
  return active;
}

bool injectionMs(uint8_t lo54, uint8_t hi55, uint8_t rpmByte, float* out) {
  if (rpmByte == 0) return false;
  *out = (float)((lo54 + 256 * hi55) / 50) / 10.0f;
  return true;
}

bool lambdaPct(uint8_t b9, uint8_t rpmByte, int32_t* out) {
  if (rpmByte == 0) return false;
  *out = lambdaPctRaw(b9);
  return true;
}

CodingInfo decodeCoding(uint8_t c, uint8_t m) {
  // REV 1.1 s.8 configuration decode (candidate interpretation).
  CodingInfo r;
  r.valid = true;
  r.coding = c;
  r.mask = m;
  if (!(m & 0x01)) r.automatic = (c & 0x01) ? 1 : 0;
  if (!(m & 0x21) && (c & 0x01)) r.gears = (c & 0x10) ? 5 : 4;
  if (!(m & 0x02)) r.catalyst = (c & 0x02) ? 0 : 1;
  r.usa = (!(m & 0x04) && (c & 0x08)) ? 1 : 0;
  if (!(m & 0x08)) {
    r.efp = (c & 0x20) ? 1 : 0;
    r.diagModule = (c & 0x80) ? 1 : 0;
  }
  r.throttleTaught = (m & 0x10) ? 0 : 1;
  if (!(m & 0x03)) {
    if (c & 0x02) r.screen = (c & 0x01) ? 107 : 108;
    else r.screen = (c & 0x01) ? ((c & 0x04) ? 109 : 103) : 104;
  }
  return r;
}

bool coolantPlausible(float v, float last, uint32_t dtMs, bool haveLast) {
  if (v < -40.0f || v > 130.0f) return false;
  if (haveLast && dtMs <= 5000) {
    float d = v - last;
    if (d < 0) d = -d;
    if (d > 15.0f) return false;
  }
  return true;
}

}  // namespace hfm
