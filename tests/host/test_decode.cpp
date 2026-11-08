// Decoder tests. Expected values come from data/protocol/test_vectors.json
// (hand-computed from the recovered formulas; SYNTH_SIM / FORMULA provenance).
#include "HfmDecode.h"
#include "HfmIdent.h"
#include "minitest.h"

using namespace hfm;

TEST(native_formulas_match_test_vectors) {
  CHECK_EQ(rpm(30), 750);
  CHECK_EQ(rpm(255), 6375);
  CHECK_NEAR(coolantNativeC(190), 78.75, 1e-6);
  CHECK_NEAR(coolantNativeC(0), -59.0, 1e-9);
  CHECK_NEAR(coolantNativeC(128), 45.0, 1e-9);
  CHECK_NEAR(coolantNativeC(255), 276.75, 1e-6);
  float iat = 0;
  CHECK(iatNativeC(173, &iat));
  CHECK_NEAR(iat, 39.578125, 1e-4);
  CHECK(!iatNativeC(92, &iat));
  CHECK(!iatNativeC(231, &iat));
  CHECK_EQ(o2NativeMv(128), 512);
  CHECK_NEAR(throttleDeg(6), 2.109375, 1e-6);
  CHECK_NEAR(loadPct(40), 20.0, 1e-9);
  CHECK_NEAR(ignitionDeg(0), 55.0, 1e-9);
  CHECK_NEAR(ignitionDeg(128), 10.0, 1e-9);
  CHECK_NEAR(mafKgH(18, 52), 1333.0, 1e-6);
  CHECK_NEAR(mafKgH(200, 0), 20.0, 1e-6);
}

TEST(alt_formulas_match_test_vectors) {
  CHECK_EQ(o2AltMv(128), 461);
  CHECK_EQ(o2AltMv(0), -192);  // trunc0 toward zero, not floor (-193)
  CHECK_EQ(throttleTenths(255), 896);
  CHECK_EQ(throttleTenths(6), 21);
  CHECK_EQ(mafIntKgH(200, 0), 20);
  CHECK_EQ(ignitionGatedDeg(100, 3), 0);
  CHECK_EQ(ignitionGatedDeg(100, 30), 19);
  CHECK_EQ(speedKmh(50), 60);
  uint16_t tgt, r;
  idleTarget(0, 30, 32, 0x41, &tgt, &r);
  CHECK_EQ(tgt, 750);
  CHECK_EQ(r, 800);
  idleTarget(0, 30, 0, 0x41, &tgt, &r);
  CHECK_EQ(tgt, 0);
  CHECK_EQ(r, 0);
  CHECK(statusBit(0x01, 8));     // R021 A/C
  CHECK(statusBit(0x08, 258));   // R016 post-start
  CHECK(!statusBit(0x7F, 0));    // R015 q=0 mask 0x80
  CHECK_EQ(coolantTableC(190), 79);
}

TEST(fuel_and_adaptation_formulas) {
  float ms = 0;
  CHECK(injectionMs(100, 10, 30, &ms));
  CHECK_NEAR(ms, 5.3, 1e-6);
  CHECK(!injectionMs(100, 10, 0, &ms));
  int32_t l = 0;
  CHECK(lambdaPct(128, 30, &l));
  CHECK_EQ(l, 0);
  CHECK(lambdaPct(0, 30, &l));
  CHECK_EQ(l, -50);
  CHECK_NEAR(adaptIdleKgH(200), 2.0, 1e-6);
  CHECK_NEAR(adaptIdleKgH(127), 0.0, 1e-6);
  CHECK_NEAR(adaptIdleKgH(0), -3.5, 1e-6);
  CHECK_NEAR(adaptFactor(128), 1.0, 1e-6);
  CHECK_NEAR(adaptFactor(0), 0.5, 1e-6);
  CHECK_NEAR(adaptFactor(255), 1.49, 1e-6);
}

TEST(trunc0_boundaries) {
  CHECK_EQ(trunc0(-1, 10), 0);
  CHECK_EQ(trunc0(-19, 10), -1);
  CHECK_EQ(trunc0(19, 10), 1);
  CHECK_EQ(trunc0(0, 7), 0);
}

TEST(coding_byte_decode) {
  CodingInfo c = decodeCoding(0x11, 0x00);
  CHECK_EQ(c.automatic, 1);
  CHECK_EQ(c.gears, 5);
  CHECK_EQ(c.catalyst, 1);
  CHECK_EQ(c.throttleTaught, 1);
  CHECK_EQ(c.screen, 103);
  CodingInfo m = decodeCoding(0x13, 0x13);  // all masked
  CHECK_EQ(m.automatic, -1);
  CHECK_EQ(m.gears, -1);
  CHECK_EQ(m.catalyst, -1);
  CHECK_EQ(m.throttleTaught, 0);
  CHECK_EQ(m.screen, -1);
  CodingInfo n = decodeCoding(0x03, 0x00);
  CHECK_EQ(n.catalyst, 0);
  CHECK_EQ(n.screen, 107);
}

TEST(coolant_plausibility) {
  CHECK(coolantPlausible(80, 79, 1000, true));
  CHECK(!coolantPlausible(131, 0, 0, false));
  CHECK(!coolantPlausible(-41, 0, 0, false));
  CHECK(!coolantPlausible(80, 60, 4000, true));
  CHECK(coolantPlausible(80, 60, 6000, true));
}

TEST(ident_parse_golden_simulator_block) {
  const uint8_t raw[28] = {0x2C, 0x42, 0x4D, 0x32, 0x32, 0x38, 0x33, 0x32, 0x30, 0x56, 0x52, 0x30, 0x30, 0x31,
                           0x33, 0x35, 0x34, 0x35, 0x31, 0x33, 0x33, 0x32, 0x35, 0x92, 0x46, 0x92, 0x41, 0x00};
  EcuIdent id;
  CHECK(parseIdent(raw, 28, id));
  CHECK(id.vendorKnown);
  CHECK_STR(id.vendor, "BOSCH");
  CHECK_STR(id.identifier, "M2 28320 VR0");
  CHECK_STR(id.partNumber, "0135451332");
  CHECK_STR(id.partDisplay, "013 545 13 32");
  CHECK_EQ(id.hwWeek, 35);
  CHECK_EQ(id.hwYear, 92);
  CHECK_EQ(id.swWeek, 46);
  CHECK_EQ(id.swYear, 92);
  CHECK_STR(id.diagDisplay, "41/00");
  CHECK_EQ(id.diagIndexBcd, 41);
}

TEST(ident_short_block_rejected) {
  uint8_t raw[10] = {0};
  EcuIdent id;
  CHECK(!parseIdent(raw, 10, id));
  CHECK(!id.present);
  CHECK_EQ(id.rawLen, 10);
}

TEST(ident_invalid_bcd_and_vendor) {
  uint8_t raw[28];
  memset(raw, 0xFF, 28);
  EcuIdent id;
  CHECK(parseIdent(raw, 28, id));
  CHECK(!id.vendorKnown);
  CHECK(!id.partValid);
  CHECK_EQ(id.hwWeek, -1);
  CHECK_EQ(id.diagIndexBcd, -1);
}
