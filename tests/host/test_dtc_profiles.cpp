#include <string>
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

TEST(dtc_remaps_are_positional) {
  dtc::Variant v;  // cylinders unknown -> generic table + positional overrides
  CHECK_EQ(dtc::codeForPosition(70, v), dtc::baseForPosition(70));  // 0x41: no override
  v.diagRaw = 0x49;  // 'I', Bosch, not 4-cyl
  CHECK_EQ(dtc::codeForPosition(35, v), 45);
  CHECK_EQ(dtc::codeForPosition(41, v), 39);
  CHECK_EQ(dtc::codeForPosition(70, v), 122);
  v.diagRaw = 0x52;
  CHECK_EQ(dtc::codeForPosition(70, v), 147);  // 50..55 table applied after 49+
  CHECK_EQ(dtc::codeForPosition(68, v), 145);
  CHECK_EQ(dtc::codeForPosition(35, v), dtc::baseForPosition(35));  // Bosch-h only at exactly 'I'
  // Keys are positions: position 35 is not remapped by a code-keyed lookup of its base code.
  v.diagRaw = 0x41;
  v.cylinders = 0;
  CHECK_EQ(dtc::applyRemaps(51, dtc::baseForPosition(51), v), dtc::baseForPosition(51));
}

TEST(dtc_remap_rule_and_reference_texts) {
  dtc::Variant v;
  v.cylinders = 4;
  CHECK_STR(dtc::remapRule(51, v), "GROUP_O");      // 4-cyl: position 51 -> 055
  CHECK_EQ(dtc::applyRemaps(51, dtc::baseForPosition(51), v), 55);
  v.cylinders = 6;
  CHECK_STR(dtc::remapRule(51, v), "");
  v.diagRaw = 0x52;
  v.cylinders = 0;
  bool any = false;
  for (uint16_t p = 1; p <= 152; p++)
    if (dtc::remapRule(p, v)[0]) any = true;
  CHECK(any);  // diag 50..55 rules apply without a factory map
  // Reference package 2026-11-11: factory meanings for 107/117, HHT candidates.
  const DtcCatalogEntry* e = dtc::lookup(107);
  CHECK(e && e->source == DTC_SRC_OEM);
  e = dtc::lookup(75);
  CHECK(e && e->source == DTC_SRC_CANDIDATE);
  CHECK(std::string(e->ru).find("K3/1") != std::string::npos);
}

TEST(dtc_factory_map_selection) {
  dtc::Variant v;
  CHECK(dtc::selectMap(v) == nullptr);  // cylinders unknown
  v.cylinders = 6; v.manufacturer = 1; v.diagRaw = 0x41;
  CHECK(std::string(dtc::selectMap(v)->id) == "C8EE");
  v.manufacturer = 0;
  CHECK(std::string(dtc::selectMap(v)->id) == "C8EE");
  v.cylinders = 4;
  CHECK(std::string(dtc::selectMap(v)->id) == "C86E");
  v.diagRaw = 0x49;
  CHECK(std::string(dtc::selectMap(v)->id) == "C96E");  // 4-cyl VDO 'I'..'O'
  v.diagRaw = 0x50;
  CHECK(std::string(dtc::selectMap(v)->id) == "CB9E");  // 4-cyl VDO >= 'P'
  v.manufacturer = 1;
  CHECK(std::string(dtc::selectMap(v)->id) == "CA9E");  // 4-cyl Bosch >= 'I'
  v.cylinders = 6;
  CHECK(std::string(dtc::selectMap(v)->id) == "CB1E");  // 6-cyl Bosch >= 'I'
  v.manufacturer = 0;
  CHECK(std::string(dtc::selectMap(v)->id) == "CA06");  // 6-cyl VDO >= 'I'
  // 6-cyl Bosch >= 'I': positions 35/39/41 -> 045/047/039 (factory map)
  v.manufacturer = 1; v.diagRaw = 0x4C;
  CHECK_EQ(dtc::codeForPosition(35, v), 45);
  CHECK_EQ(dtc::codeForPosition(39, v), 47);
  CHECK_EQ(dtc::codeForPosition(41, v), 39);
  // Unused position in a map decodes as code 0 (RAW, no guess).
  v.cylinders = 4; v.diagRaw = 0x41;
  CHECK_EQ(dtc::codeForPosition(9, v), 0);
  uint8_t bank[16] = {0};
  bank[1] = 0x01;  // position 9
  dtc::Decoded out[4];
  CHECK_EQ(dtc::decodeBank(bank, 0x00, v, out, 4), 1);
  CHECK_EQ(out[0].code, 0);
  CHECK(out[0].entry == nullptr);
  CHECK(std::string(out[0].mapId) == "C86E");
}

TEST(profile_cylinders_from_engine) {
  CHECK_EQ(prof::cylindersForEngine("104.992"), 6);
  CHECK_EQ(prof::cylindersForEngine("111.961"), 4);
  CHECK_EQ(prof::cylindersForEngine("119.970"), 0);
  CHECK_EQ(prof::cylindersForEngine(nullptr), 0);
}

TEST(dtc_catalog_lookup) {
  CHECK(dtc::lookup(10) != nullptr);
  CHECK(dtc::lookup(164) != nullptr);
  CHECK(dtc::lookup(239) == nullptr);
  CHECK_EQ(DTC_CATALOG_COUNT, 165);
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
  CHECK_EQ(p.variant.diagRaw, 0x45);
  CHECK(!p.vdoCandidate);
}

TEST(profile_unknown_vendor_needs_experimental_optin) {
  uint8_t raw[28];
  memcpy(raw, kGolden, 28);
  raw[1] = 0x11;  // vendor letter not B/V/S
  EcuIdent id;
  parseIdent(raw, 28, id);
  CHECK(!id.vendorKnown);
  CHECK_STR(id.vendorCode, "2C 11");
  prof::EcuProfile p;
  prof::ResolveOptions o;
  prof::resolve(id, o, p);
  CHECK(!p.caps[prof::F_DTC_READ].allowed);
  o.experimentalReads = true;
  prof::resolve(id, o, p);
  CHECK(p.caps[prof::F_DTC_READ].allowed);
  CHECK(p.caps[prof::F_DTC_READ].conf == Confidence::GIP);
  CHECK(p.vdoCandidate);  // unknown vendor in the HFM range: live plan probed
  CHECK(!p.vdoUnits);     // Bosch plan first
  CHECK_EQ(p.variant.manufacturer, 0);
}

TEST(profile_vendor_letter_as_pc_tester) {
  // Byte 0 other than 2C with vendor letter 'B' is still Bosch (PC tester rule).
  uint8_t raw[28];
  memcpy(raw, kGolden, 28);
  raw[0] = 0x27;
  raw[26] = 0x40;
  EcuIdent id;
  parseIdent(raw, 28, id);
  CHECK(id.vendorKnown);
  CHECK_EQ(id.vendorId, 1);
  CHECK_EQ(id.systemGroup, 1);
  CHECK_STR(id.vendor, "BOSCH");
  prof::EcuProfile p;
  prof::ResolveOptions o;
  prof::resolve(id, o, p);
  CHECK(p.resolved && !p.referenceEcu && !p.vdoCandidate && !p.vdoUnits);
  CHECK(p.caps[prof::F_DTC_READ].allowed && p.caps[prof::F_LIVE_NATIVE].allowed);
  CHECK_EQ(p.variant.manufacturer, 1);
  // 'V' = VDO: HFM reads allowed, VDO live addresses only with the opt-in.
  raw[1] = 'V';
  parseIdent(raw, 28, id);
  CHECK_STR(id.vendor, "VDO");
  prof::resolve(id, o, p);
  CHECK(p.resolved && p.vdoUnits && !p.vdoCandidate);
  CHECK(p.caps[prof::F_DTC_READ].allowed);
  CHECK(!p.caps[prof::F_LIVE_NATIVE].allowed);
  o.experimentalReads = true;
  prof::resolve(id, o, p);
  CHECK(p.caps[prof::F_LIVE_NATIVE].allowed);
  CHECK_EQ(p.variant.manufacturer, 0);
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
