#include "HfmDtc.h"
#include "HfmIdent.h"
#include "HfmProfiles.h"
#include "minitest.h"

static const uint8_t kGolden[28] = {0x2C, 0x42, 0x4D, 0x32, 0x32, 0x38, 0x33, 0x32, 0x30, 0x56,
                                    0x52, 0x30, 0x30, 0x31, 0x33, 0x35, 0x34, 0x35, 0x31, 0x33,
                                    0x33, 0x32, 0x35, 0x92, 0x46, 0x92, 0x41, 0x00};

TEST(dtc_simulator_scenarios_decode) {
  // Bank A index 00 = 0x40 -> pos 7 -> DTC 10 (REV 1.1 s.11).
  uint8_t bank[16] = {0};
  bank[0] = 0x40;
  bank[2] = 0x08;
  dtc::Decoded out[8];
  dtc::Variant v;
  uint16_t n = dtc::decodeBank(bank, 0x00, v, out, 8);
  CHECK_EQ(n, 2);
  CHECK_EQ(out[0].position, 7);
  CHECK_EQ(out[0].code, 10);
  CHECK(out[0].entry != nullptr);
  CHECK_EQ(out[1].position, 20);
  CHECK_EQ(out[1].code, 24);
  CHECK_EQ(out[1].bank, 0);
  n = dtc::decodeBank(bank, 0x10, v, out, 8);
  CHECK_EQ(out[0].bank, 1);
  CHECK_EQ(out[0].index, 0x10);
}

TEST(dtc_base_table_edges) {
  CHECK_EQ(dtc::baseForPosition(1), 2);
  CHECK_EQ(dtc::baseForPosition(9), 118);
  CHECK_EQ(dtc::baseForPosition(121), 5);
  CHECK_EQ(dtc::baseForPosition(152), 255);
  CHECK_EQ(dtc::baseForPosition(0), 0);
  CHECK_EQ(dtc::baseForPosition(153), 0);
}

TEST(dtc_remaps_are_sequential) {
  dtc::Variant v;
  CHECK_EQ(dtc::applyRemaps(70, v), 70);  // default 41: no remap
  v.diagIndex = 49;
  CHECK_EQ(dtc::applyRemaps(35, v), 45);
  CHECK_EQ(dtc::applyRemaps(70, v), 122);
  v.diagIndex = 52;
  CHECK_EQ(dtc::applyRemaps(70, v), 122);  // 49+ applies first, 50-55 map has no 122
  CHECK_EQ(dtc::applyRemaps(68, v), 145);
  CHECK_EQ(dtc::applyRemaps(35, v), 35);   // Bosch-h map only at exactly 49
  v.diagIndex = 41;
  v.engineGroup = 'o';
  CHECK_EQ(dtc::applyRemaps(51, v), 55);
}

TEST(dtc_catalog_lookup) {
  CHECK(dtc::lookup(10) != nullptr);
  CHECK(dtc::lookup(164) != nullptr);
  CHECK(dtc::lookup(239) == nullptr);
  CHECK_EQ(DTC_CATALOG_COUNT, 163);
  for (uint16_t i = 1; i < DTC_CATALOG_COUNT; i++) CHECK(DTC_CATALOG[i - 1].code < DTC_CATALOG[i].code);
}

TEST(dtc_bank_labels) {
  CHECK_STR(dtc::bankLabel(0, dtc::BankMapping::Unknown), "bank_a");
  CHECK_STR(dtc::bankLabel(0, dtc::BankMapping::AStoredBCurrent), "stored");
  CHECK_STR(dtc::bankLabel(1, dtc::BankMapping::ACurrentBStored), "stored");
}

TEST(profile_unresolved_allows_only_ident) {
  EcuIdent none;
  prof::EcuProfile p;
  prof::resolve(none, prof::ResolveOptions(), p);
  CHECK(!p.resolved);
  CHECK(p.caps[prof::F_IDENT].allowed);
  CHECK(!p.caps[prof::F_DTC_READ].allowed);
  CHECK(!p.caps[prof::F_DTC_CLEAR].allowed);
}

TEST(profile_reference_ecu_writes_need_engineer_mode) {
  EcuIdent id;
  parseIdent(kGolden, 28, id);
  prof::EcuProfile p;
  prof::ResolveOptions o;
  prof::resolve(id, o, p);
  CHECK(p.referenceEcu);
  CHECK(p.caps[prof::F_DTC_READ].allowed);
  CHECK(p.caps[prof::F_LIVE_NATIVE].allowed);
  CHECK(!p.caps[prof::F_LIVE_EXTENDED].allowed);
  CHECK(!p.caps[prof::F_DTC_CLEAR].allowed);
  CHECK(p.bankMapping == dtc::BankMapping::AStoredBCurrent);
  o.engineerMode = true;
  prof::resolve(id, o, p);
  CHECK(p.caps[prof::F_DTC_CLEAR].allowed);
  CHECK(p.caps[prof::F_ADAPT_RESET].allowed);
  CHECK(!p.caps[prof::F_VERSION_CODING_WRITE].allowed);  // no known command
  CHECK(!p.caps[prof::F_THROTTLE_LEARN].allowed);
}

TEST(profile_generic_bosch_blocks_writes_even_in_engineer_mode) {
  uint8_t raw[28];
  memcpy(raw, kGolden, 28);
  raw[21] = '9';  // different part number
  raw[26] = 0x45;
  EcuIdent id;
  parseIdent(raw, 28, id);
  prof::EcuProfile p;
  prof::ResolveOptions o;
  o.engineerMode = true;
  prof::resolve(id, o, p);
  CHECK(p.resolved);
  CHECK(!p.referenceEcu);
  CHECK(p.caps[prof::F_DTC_READ].allowed);
  CHECK(!p.caps[prof::F_DTC_CLEAR].allowed);
  CHECK(p.caps[prof::F_DTC_CLEAR].conf == Confidence::BLOCK);
  CHECK(p.bankMapping == dtc::BankMapping::AStoredBCurrent);
  CHECK(p.bankMappingConf == Confidence::GIP);
  CHECK_EQ(p.variant.diagIndex, 45);
}

TEST(profile_unknown_vendor_needs_experimental_optin) {
  uint8_t raw[28];
  memcpy(raw, kGolden, 28);
  raw[0] = 0x11;
  EcuIdent id;
  parseIdent(raw, 28, id);
  prof::EcuProfile p;
  prof::ResolveOptions o;
  prof::resolve(id, o, p);
  CHECK(!p.caps[prof::F_DTC_READ].allowed);
  o.experimentalReads = true;
  prof::resolve(id, o, p);
  CHECK(p.caps[prof::F_DTC_READ].allowed);
  CHECK(p.caps[prof::F_DTC_READ].conf == Confidence::GIP);
}

TEST(profile_pms_range_rejected) {
  uint8_t raw[28];
  memcpy(raw, kGolden, 28);
  raw[26] = 0x65;
  EcuIdent id;
  parseIdent(raw, 28, id);
  prof::EcuProfile p;
  prof::ResolveOptions o;
  o.experimentalReads = true;
  prof::resolve(id, o, p);
  CHECK(!p.caps[prof::F_DTC_READ].allowed);
}

TEST(vehicle_db_and_norms) {
  uint16_t n = 0;
  const prof::VehicleEntry* v = prof::vehicles(&n);
  CHECK(n >= 15);
  for (uint16_t i = 0; i < n; i++) {
    CHECK(prof::connectorById(v[i].connector) != nullptr);
    CHECK(v[i].source && v[i].source[0]);
  }
  CHECK(prof::generationFor("104.992", 199310) == prof::Generation::EarlyAuto);
  CHECK(prof::generationFor("104.992", 199402) == prof::Generation::Transition);
  CHECK(prof::generationFor("104.992", 199405) == prof::Generation::Coded);
  CHECK(prof::generationFor("104.995", 0) == prof::Generation::Hfm41);
  const prof::Norm* nr = prof::normFor("rpm", prof::NormGroup::M111_961, false);
  CHECK(nr && nr->lo == 650 && nr->hi == 850);
  const prof::Norm* nv = prof::normFor("adapt_idle", prof::NormGroup::M104_CODED, true);
  CHECK(nv && nv->hi == 15);
  CHECK(prof::normFor("rpm", prof::NormGroup::None, false) == nullptr);
  const prof::ConnectorProfile* c = prof::connectorById("W124_X11_4_16");
  CHECK(c && c->pins == 16 && c->conf == Confidence::ZAV);
}

TEST(bank_mapping_inferred_from_intermittent_rule) {
  // DTC 005 (ECT intermittent) sits at position 121 = index 0x0F bit 0.
  uint8_t a[16] = {0}, b[16] = {0};
  a[0x0F] = 0x01;
  b[0x00] = 0x40;  // DTC 010 (MAF open) - not intermittent, no evidence
  dtc::Decoded items[8];
  dtc::Variant v;
  uint16_t n = dtc::decodeBank(a, 0x00, v, items, 8);
  n += dtc::decodeBank(b, 0x10, v, items + n, (uint16_t)(8 - n));
  CHECK_EQ(items[0].code, 5);
  CHECK(dtc::inferBankMapping(items, n) == dtc::BankMapping::AStoredBCurrent);
  uint8_t a2[16] = {0}, b2[16] = {0};
  b2[0x0F] = 0x02;  // DTC 008 (IAT intermittent) in bank B -> B stored
  n = dtc::decodeBank(a2, 0x00, v, items, 8);
  n += dtc::decodeBank(b2, 0x10, v, items + n, (uint16_t)(8 - n));
  CHECK(dtc::inferBankMapping(items, n) == dtc::BankMapping::ACurrentBStored);
  b2[0x0F] = 0;
  n = dtc::decodeBank(a2, 0x00, v, items, 8);
  CHECK(dtc::inferBankMapping(items, n) == dtc::BankMapping::Unknown);
}

TEST(m111_texts_override_cylinder_mapping) {
  CHECK(dtc::m111Text(55) != nullptr);
  CHECK(strstr(dtc::m111Text(55), "цилиндре 1") != nullptr);
  CHECK(dtc::m111Text(45) != nullptr);
  CHECK(dtc::m111Text(10) == nullptr);
}
