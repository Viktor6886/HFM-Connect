#include "HfmProfiles.h"

#include <stdio.h>
#include <string.h>

namespace prof {

// Source tags (full list in docs/SOURCES.md):
//  I03 2147.pdf MBNA 07.5/1 (coding transition)    I04 659.pdf MBUSA P-SI-14.00/14
//  I05 24072001.pdf W124 ETM I                       I07 Test and Adjustment Data
//  I08 Engine Test, Adjustment.pdf (HFM p.49-56)     I09/I10 DM 8.5/8.6 manuals
//  WIS Описание по годам.docx (GF07.51-P-3017D)      E111 Engine 111 OEM 11/x
//  SNAP DTC_Connector_Pinouts_38-16-8.pdf (secondary)   R11 REV 1.1 protocol spec

const char* generationCode(Generation g) {
  switch (g) {
    case Generation::EarlyAuto: return "EARLY_AUTO";
    case Generation::Transition: return "TRANSITION";
    case Generation::Coded: return "CODED";
    case Generation::Hfm41: return "HFM41";
    default: return "UNKNOWN";
  }
}

const char* generationName(Generation g) {
  switch (g) {
    case Generation::EarlyAuto: return "HFM 3.4 ранний (до 02/1994, автораспознавание комплектации)";
    case Generation::Transition: return "Переходный период 01–02/1994 (определять по ID ЭБУ)";
    case Generation::Coded: return "HFM 3.4 кодируемый (с 03/1994, 9-значный код заводского тестера)";
    case Generation::Hfm41: return "HFM-SFI 4.1 (M104.995)";
    default: return "Не определено";
  }
}

const char* normGroupCode(NormGroup g) {
  switch (g) {
    case NormGroup::M104_EARLY: return "M104_HFM34_EARLY";
    case NormGroup::M104_CODED: return "M104_HFM34_CODED";
    case NormGroup::M111_961: return "M111_961";
    case NormGroup::M104_995: return "M104_995_HFM41";
    default: return "NONE";
  }
}

// ---- connectors -------------------------------------------------------------
static const ConnectorPin kW124_16[] = {
    {1, "Масса (цепь 31)"},
    {8, "Диагностика HFM-SFI N3/4 (K-Line)"},
    {14, "EA/CC/ISC (при наличии)"},
    {16, "Цепь 15 — ЗАЖИГАНИЕ (не постоянная 30!)"},
};
static const ConnectorPin kW202_38[] = {
    {1, "Масса"},          {3, "Питание прибора (цепь 30)"},
    {4, "HFM-SFI N3/4"},   {7, "Круиз-контроль CC"},
    {17, "TN (сигнал оборотов) — не K-Line"},
    {19, "DM N59/1 — отдельный блок, не HFM"},
};
static const ConnectorPin kGeneric38[] = {
    {1, "Масса (цепь 31)"},
    {2, "Цепь 87/15z"},
    {3, "Цепь 30"},
    {4, "HFM-SFI N3/4"},
    {19, "DM N59/1 — отдельный блок"},
};
static const ConnectorPin kX11_22[] = {
    {4, "Масса W1"},
    {5, "Масса W15/1"},
    {7, "Диагностический провод DM N59/1"},
    {16, "Цепь 30 (постоянное питание)"},
};

static const ConnectorProfile kConnectors[] = {
    {"W124_X11_4_16", "X11/4, 16-контактный (W124)", 16, kW124_16, 4, Confidence::ZAV, "I08 PDF p.53; I05 p.2",
     "Контакт 16 — цепь 15 (зажигание). Не путать с 16-контактным OBD-II X11/22."},
    {"W202_X11_4_38", "X11/4, 38-контактный (W202)", 38, kW202_38, 6, Confidence::ZAV, "E111 PDF p.8 (11/7)",
     "Профиль W202. Не переносить на W124/16 и на выводы самого ЭБУ."},
    {"X11_4_38", "X11/4, 38-контактный (R129/W140/W210)", 38, kGeneric38, 5, Confidence::GIP,
     "SNAP p.1-2 (сторонний); I08 p.51/53",
     "Назначение соседних контактов зависит от кузова и комплектации — сверить по ETM."},
    {"DM_X11_22_16", "X11/22, 16-контактный OBD-II (DM N59/1)", 16, kX11_22, 4, Confidence::ZAV, "I09/I10 PDF p.2",
     "Это НЕ разъём HFM. Протокол DM/OBD-II в проекте не реализуется."},
    {"PIN8_LEGACY", "8-контактный (старые модели)", 8, nullptr, 0, Confidence::BLOCK, "Trisco/импульсные инструкции",
     "Распиновка HFM для 8-контактного разъёма в документации не подтверждена."},
    {"ECU_BENCH_UNVERIFIED", "Стенд: разъём ЭБУ N3/4", 0, nullptr, 0, Confidence::BLOCK, "REV 1.4 s.5.2, s.49",
     "Нумерация N3/4 ≠ X11/4. Подключение 12 В на стенде только по ETM для конкретного номера ЭБУ."},
};

const ConnectorProfile* connectors(uint16_t* count) {
  *count = sizeof(kConnectors) / sizeof(kConnectors[0]);
  return kConnectors;
}

const ConnectorProfile* connectorById(const char* id) {
  uint16_t n;
  const ConnectorProfile* c = connectors(&n);
  for (uint16_t i = 0; i < n; i++)
    if (strcmp(c[i].id, id) == 0) return &c[i];
  return nullptr;
}

// ---- vehicles -----------------------------------------------------------------
// from/to are production dates stated by the source; 0 = not stated.
static const VehicleEntry kVehicles[] = {
    // W124
    {"W124", "124.028", "E280 / 300E 2.8", "104.942", 199209, 199402, Generation::EarlyAuto, NormGroup::M104_EARLY,
     "W124_X11_4_16", "", Confidence::ZAV, "I07 p.1-8"},
    {"W124", "124.028", "E280 / 300E 2.8", "104.942", 199403, 0, Generation::Coded, NormGroup::M104_CODED,
     "W124_X11_4_16", "", Confidence::ZAV, "I07 p.9-18; I03"},
    {"W124", "124.032/052/066/092", "E320", "104.992", 199209, 199402, Generation::EarlyAuto,
     NormGroup::M104_EARLY, "W124_X11_4_16", "", Confidence::ZAV, "I07 p.1-8; I04"},
    {"W124", "124.032/052/066/092", "E320", "104.992", 199403, 0, Generation::Coded, NormGroup::M104_CODED,
     "W124_X11_4_16", "", Confidence::ZAV, "I07 p.9-18; I03; I04"},
    {"W124", "124.022", "E220", "111.960", 0, 0, Generation::Unknown, NormGroup::None, "W124_X11_4_16", "",
     Confidence::GIP, "WIS GF07.51 (двигатель HFM); шасси — общие сведения, сверить"},
    // R129
    {"R129", "129.063", "SL320", "104.991", 199306, 199402, Generation::EarlyAuto, NormGroup::M104_EARLY,
     "X11_4_38", "DM 8.5", Confidence::ZAV, "I07 p.1-8; I09"},
    {"R129", "129.063", "SL320", "104.991", 199403, 0, Generation::Coded, NormGroup::M104_CODED, "X11_4_38",
     "DM 8.5 до MY1995 / DM 8.6 с MY1996", Confidence::ZAV, "I07 p.9-18; I09; I10"},
    // W140
    {"W140", "140.032/033", "S320", "104.994", 199308, 199402, Generation::EarlyAuto, NormGroup::M104_EARLY,
     "X11_4_38", "DM 8.6", Confidence::ZAV, "I07 p.1-8; I10"},
    {"W140", "140.032/033", "S320", "104.994", 199403, 0, Generation::Coded, NormGroup::M104_CODED, "X11_4_38",
     "DM 8.6", Confidence::ZAV, "I07 p.9-18; I10; I04"},
    // W202
    {"W202", "202.022", "C220", "111.961", 199302, 199402, Generation::EarlyAuto, NormGroup::M111_961,
     "W202_X11_4_38", "DM 8.5", Confidence::ZAV, "I07 p.19-23; E111 11/2"},
    {"W202", "202.022", "C220", "111.961", 199403, 0, Generation::Coded, NormGroup::M111_961, "W202_X11_4_38",
     "DM 8.5", Confidence::ZAV, "I07 p.24-30; E111 11/2-11/4"},
    {"W202", "202.028", "C280", "104.941", 199302, 199402, Generation::EarlyAuto, NormGroup::M104_EARLY,
     "W202_X11_4_38", "DM 8.5", Confidence::ZAV, "I07 p.1-8; I09"},
    {"W202", "202.028", "C280 / C36 AMG", "104.941", 199403, 199607, Generation::Coded, NormGroup::M104_CODED,
     "W202_X11_4_38", "DM 8.5", Confidence::ZAV, "I07 p.9-18; I04; WIS (104.941 до 31.7.96)"},
    {"W202", "202.020/080", "C200 (по WIS)", "111.945", 0, 199808, Generation::Unknown, NormGroup::None,
     "W202_X11_4_38", "", Confidence::ZAV, "WIS GF07.51 (111.945 до 31.8.98 в 202.020/080)"},
    {"W202", "202.023", "C230", "111.974", 0, 0, Generation::Unknown, NormGroup::None, "W202_X11_4_38", "",
     Confidence::GIP, "WIS GF07.51 (двигатель); шасси — общие сведения, сверить"},
    // W210
    {"W210", "210.055", "E320", "104.995", 0, 0, Generation::Hfm41, NormGroup::M104_995, "X11_4_38",
     "DM 8.6", Confidence::ZAV, "I07 p.31-38; I10; I04 (импульсный счётчик не для 210 — WIS)"},
};

const VehicleEntry* vehicles(uint16_t* count) {
  *count = sizeof(kVehicles) / sizeof(kVehicles[0]);
  return kVehicles;
}

const VehicleEntry* vehicleByIndex(uint16_t idx) {
  uint16_t n;
  const VehicleEntry* v = vehicles(&n);
  return idx < n ? &v[idx] : nullptr;
}

Generation generationFor(const char* engine, uint32_t yyyymm) {
  if (engine && strcmp(engine, "104.995") == 0) return Generation::Hfm41;
  if (!yyyymm) return Generation::Unknown;
  if (yyyymm < 199401) return Generation::EarlyAuto;
  if (yyyymm < 199403) return Generation::Transition;
  return Generation::Coded;
}

// ---- capabilities -----------------------------------------------------------
static const char* const kFeatureCodes[F_COUNT] = {
    "ident",        "dtc_read",     "dtc_extended", "live_native", "live_extended",
    "adapt_read",   "coding_read",  "vin_read",     "dtc_clear",   "adapt_reset",
    "purge_test",   "version_coding_write",         "throttle_learn",
    "impulse_read", "impulse_clear"};
static const char* const kFeatureNames[F_COUNT] = {
    "Идентификация ЭБУ (F0 00–1B)",
    "Чтение памяти неисправностей (F1 00–1F)",
    "Расширенные биты DTC 129–152 (F1 30–32/40–42)",
    "Live Data — нативные группы (full8)",
    "Live Data — расширенные рецепты",
    "Чтение самоадаптаций смеси (24* 90/92/93)",
    "Чтение кодировки комплектации (27* 02 24* F0)",
    "Чтение VIN (F9 00–0D)",
    "Стирание памяти неисправностей",
    "Сброс самоадаптаций смеси",
    "Тест клапана продувки адсорбера Y58/1",
    "Запись 9-значного кода комплектации",
    "Обучение упоров дросселя / ETA",
    "Импульсное чтение кодов (blink-code)",
    "Импульсное стирание кода"};

const char* featureCode(uint8_t f) { return f < F_COUNT ? kFeatureCodes[f] : "?"; }
const char* featureName(uint8_t f) { return f < F_COUNT ? kFeatureNames[f] : "?"; }
bool featureIsWrite(uint8_t f) {
  return f == F_DTC_CLEAR || f == F_ADAPT_RESET || f == F_PURGE_TEST || f == F_VERSION_CODING_WRITE ||
         f == F_THROTTLE_LEARN || f == F_IMPULSE_CLEAR;
}

static void setCap(EcuProfile& p, uint8_t f, bool allowed, Confidence c, const char* reason) {
  p.caps[f].allowed = allowed;
  p.caps[f].conf = c;
  p.caps[f].reason = reason;
}

void resolve(const EcuIdent& id, const ResolveOptions& opt, EcuProfile& p) {
  p = EcuProfile();
  p.variant = dtc::Variant();
  p.bankMapping = dtc::BankMapping::Unknown;
  p.bankMappingConf = Confidence::GIP;

  // Commands for which no transaction is known at all: always blocked.
  setCap(p, F_VERSION_CODING_WRITE, false, Confidence::BLOCK,
         "Команда «Version coding» заводского тестера не восстановлена; только ручной ввод/архив кода");
  setCap(p, F_THROTTLE_LEARN, false, Confidence::BLOCK,
         "Команда обучения не известна; доступна только визуальная проверка CTP/WOT/угла");
  setCap(p, F_IDENT, true, Confidence::REK, "");
  setCap(p, F_IMPULSE_READ, true, Confidence::ZAV,
         "Заводская процедура для diagnosis version ≤46; не для W210; требует аппаратного ключа");
  setCap(p, F_IMPULSE_CLEAR, false, Confidence::BLOCK,
         "Импульсное стирание отключено по умолчанию (REV 1.4 M14-07)");

  bool bosch = id.present && id.vendorKnown;
  int16_t di = id.diagIndexBcd;
  bool hfmRange = di >= 40 && di <= 59;
  bool pmsRange = di >= 60 && di <= 79;

  if (!id.present) {
    snprintf(p.id, sizeof p.id, "UNRESOLVED");
    snprintf(p.name, sizeof p.name, "ЭБУ не идентифицирован");
  } else if (bosch && id.partValid && strcmp(id.partNumber, "0135451332") == 0 && id.diagIndexRaw == 0x41 &&
             id.diagSubRaw == 0x00 && id.raw[2] == 'M' && id.raw[4] == '2' && id.raw[5] == '8') {
    // Reference ECU of the REV 1.1 specification (field observations exist for
    // reads). Not re-tested by this project.
    p.resolved = true;
    p.referenceEcu = true;
    snprintf(p.id, sizeof p.id, "BOSCH_0135451332_41");
    snprintf(p.name, sizeof p.name, "Bosch HFM-SFI 013 545 13 32, diag 41/00 (эталонный ЭБУ)");
    p.bankMapping = dtc::BankMapping::AStoredBCurrent;
    p.bankMappingConf = Confidence::REK;
  } else if (bosch && hfmRange) {
    p.resolved = true;
    snprintf(p.id, sizeof p.id, "GENERIC_BOSCH_HFM_%d", di);
    snprintf(p.name, sizeof p.name, "Bosch HFM-SFI, diag %s (общий профиль)", id.diagDisplay);
    // Probable orientation (reference profile + factory tester order: it reads
    // 10..1F, erases, then verifies 00..0F). Confirmed per readout by the
    // intermittent-fault rule (dtc::inferBankMapping).
    p.bankMapping = dtc::BankMapping::AStoredBCurrent;
    p.bankMappingConf = Confidence::GIP;
  } else if (pmsRange) {
    snprintf(p.id, sizeof p.id, "PMS_%d", di);
    snprintf(p.name, sizeof p.name, "Система PMS (diag %s) — вне области проекта", id.diagDisplay);
  } else {
    snprintf(p.id, sizeof p.id, "UNKNOWN_%02X%02X", id.diagIndexRaw, id.diagSubRaw);
    snprintf(p.name, sizeof p.name, "Неизвестный ЭБУ (%s, diag %s)", id.vendor, id.diagDisplay);
  }
  if (di >= 0) p.variant.diagIndex = di;
  p.variant.manufacturer = bosch ? 1 : 0;

  bool readOk = p.resolved || (id.present && !pmsRange && opt.experimentalReads);
  Confidence readConf = p.referenceEcu ? Confidence::REK : (p.resolved ? Confidence::REK : Confidence::GIP);
  const char* readReason = readOk ? "" :
      (pmsRange ? "PMS не поддерживается" :
       (id.present ? "ЭБУ не распознан: включите «Экспериментальное чтение» в настройках" : "Сначала выполните идентификацию"));
  setCap(p, F_DTC_READ, readOk, readConf, readReason);
  setCap(p, F_LIVE_NATIVE, readOk, readConf, readReason);
  setCap(p, F_ADAPT_READ, readOk, Confidence::REK, readReason);
  setCap(p, F_CODING_READ, readOk, Confidence::REK, readReason);
  bool expOk = readOk && opt.experimentalReads;
  const char* expReason = readOk ? "Включите «Экспериментальное чтение» (статус AC)" : readReason;
  setCap(p, F_LIVE_EXTENDED, expOk, Confidence::GIP, expOk ? "" : expReason);
  setCap(p, F_DTC_EXTENDED, expOk, Confidence::GIP, expOk ? "" : expReason);
  setCap(p, F_VIN_READ, expOk, Confidence::GIP, expOk ? "" : expReason);

  // Write services: REK-quarantined recipes only for the reference ECU,
  // only in engineer mode; runtime guards (interlock, preconditions,
  // confirmation) are enforced additionally by ServiceGuard.
  const char* wrReason;
  bool wr = false;
  if (!p.referenceEcu) {
    wrReason = "Заблокировано: нет проверенной трассы для этого ЭБУ (REV 1.4 s.2 БЛОК)";
  } else if (!opt.engineerMode) {
    wrReason = "Рецепт РЕК для эталонного ЭБУ: требуется «Режим инженера»";
  } else {
    wr = true;
    wrReason = "РЕК: только при ключе WRITE-EN, выполненных условиях и подтверждении";
  }
  Confidence wc = p.referenceEcu ? Confidence::REK : Confidence::BLOCK;
  setCap(p, F_DTC_CLEAR, wr, wc, wrReason);
  setCap(p, F_ADAPT_RESET, wr, wc, wrReason);
  setCap(p, F_PURGE_TEST, wr, wc, wrReason);
}

// ---- norms (REV 1.4 s.10, I07 pages 2, 10-11, 20, 25-26, 32-33) -----------------
static const Norm kNorms[] = {
    {"rpm", NormGroup::M104_EARLY, 600, 800, NormCond::WarmIdle, "ХХ, P/N, ОЖ >80 °C", "I07 p.2"},
    {"rpm", NormGroup::M104_CODED, 600, 800, NormCond::WarmIdle, "ХХ, P/N, ОЖ >80 °C", "I07 p.10"},
    {"rpm", NormGroup::M111_961, 650, 850, NormCond::WarmIdle, "ХХ, P/N, ОЖ >80 °C", "I07 p.20/25"},
    {"rpm", NormGroup::M104_995, 600, 800, NormCond::WarmIdle, "ХХ, P/N, ОЖ >80 °C", "I07 p.32"},
    {"inj_ms", NormGroup::M104_EARLY, 2.5f, 3.8f, NormCond::WarmIdle, "Время впрыска на ХХ", "I07 p.2"},
    {"inj_ms", NormGroup::M104_CODED, 2.5f, 3.8f, NormCond::WarmIdle, "Время впрыска на ХХ", "I07 p.10"},
    {"inj_ms", NormGroup::M111_961, 3.0f, 5.0f, NormCond::WarmIdle, "Время впрыска на ХХ", "I07 p.20/25"},
    {"inj_ms", NormGroup::M104_995, 2.5f, 3.8f, NormCond::WarmIdle, "Время впрыска на ХХ", "I07 p.32"},
    {"maf", NormGroup::M104_EARLY, 16, 18, NormCond::WarmIdle, "Расход воздуха на ХХ", "I07 p.2"},
    {"maf", NormGroup::M104_CODED, 16, 18, NormCond::WarmIdle, "Расход воздуха на ХХ", "I07 p.10"},
    {"maf", NormGroup::M111_961, 9, 13, NormCond::WarmIdle, "Расход воздуха на ХХ", "I07 p.20/25"},
    {"maf", NormGroup::M104_995, 15, 18, NormCond::WarmIdle, "Расход воздуха на ХХ", "I07 p.32"},
    {"o2_mv", NormGroup::M104_EARLY, -200, 1000, NormCond::WarmRunning, "Колебания ~300 мВ после ~2 мин ХХ", "I07"},
    {"o2_mv", NormGroup::M104_CODED, -200, 1000, NormCond::WarmRunning, "Колебания ~300 мВ после ~2 мин ХХ", "I07"},
    {"o2_mv", NormGroup::M111_961, -200, 1000, NormCond::WarmRunning, "Колебания ~300 мВ после ~2 мин ХХ", "I07"},
    {"o2_mv", NormGroup::M104_995, -200, 1000, NormCond::WarmRunning, "Колебания ~300 мВ после ~2 мин ХХ", "I07"},
    {"lambda", NormGroup::M104_EARLY, -10, 10, NormCond::WarmIdle, "On-off ratio 0±10 % (ХХ и 2500 об/мин)", "I07 p.2"},
    {"lambda", NormGroup::M104_CODED, -10, 10, NormCond::WarmIdle, "On-off ratio 0±10 %", "I07 p.10"},
    {"lambda", NormGroup::M111_961, -10, 10, NormCond::WarmIdle, "On-off ratio 0±10 %", "I07 p.20"},
    {"lambda", NormGroup::M104_995, -10, 10, NormCond::WarmIdle, "On-off ratio 0±10 %", "I07 p.32"},
    {"ign_deg", NormGroup::M104_EARLY, 6, 10, NormCond::WarmIdle, "Кратковременно ±10° при стабилизации ХХ", "I07 p.2"},
    {"ign_deg", NormGroup::M104_CODED, 6, 10, NormCond::WarmIdle, "Кратковременно ±10° при стабилизации ХХ", "I07 p.10"},
    {"ign_deg", NormGroup::M111_961, 5, 13, NormCond::WarmIdle, "Угол зажигания на ХХ", "I07 p.20"},
    {"ign_deg", NormGroup::M104_995, 6, 10, NormCond::WarmIdle, "Угол зажигания на ХХ", "I07 p.32"},
    {"throttle", NormGroup::M104_EARLY, 1.0f, 1.8f, NormCond::WarmIdle, "Угол заслонки на ХХ (до 02/94)", "I07 p.2"},
    {"throttle", NormGroup::M104_CODED, 0.3f, 1.0f, NormCond::WarmIdle, "Угол заслонки на ХХ (с 03/94)", "I07 p.10"},
    {"throttle", NormGroup::M111_961, 1.0f, 1.8f, NormCond::WarmIdle, "Угол заслонки на ХХ", "I07 p.20"},
    {"throttle", NormGroup::M104_995, 0.3f, 1.8f, NormCond::WarmIdle, "Угол заслонки на ХХ", "I07 p.32"},
    {"adapt_idle", NormGroup::M104_EARLY, -2, 2, NormCond::Always, "Bosch, кг/ч; 0 — базовое", "I07 p.2"},
    {"adapt_idle", NormGroup::M104_CODED, -2, 2, NormCond::Always, "Bosch, кг/ч; 0 — базовое", "I07 p.10"},
    {"adapt_idle", NormGroup::M111_961, -2, 2, NormCond::Always, "Bosch, кг/ч; 0 — базовое", "I07 p.20"},
    {"adapt_idle", NormGroup::M104_995, -2, 2, NormCond::Always, "Bosch, кг/ч; 0 — базовое", "I07 p.32"},
    {"adapt_idle_vdo", NormGroup::M104_EARLY, -15, 15, NormCond::Always, "VDO, %; 0 — базовое", "I07 p.2"},
    {"adapt_idle_vdo", NormGroup::M104_CODED, -15, 15, NormCond::Always, "VDO, %; 0 — базовое", "I07 p.10"},
    {"adapt_idle_vdo", NormGroup::M111_961, -15, 15, NormCond::Always, "VDO, %; 0 — базовое", "I07 p.20"},
    {"adapt_lower", NormGroup::M104_EARLY, 0.85f, 1.15f, NormCond::Always, "1.00 — базовое", "I07 p.2"},
    {"adapt_lower", NormGroup::M104_CODED, 0.85f, 1.15f, NormCond::Always, "1.00 — базовое", "I07 p.10"},
    {"adapt_lower", NormGroup::M111_961, 0.85f, 1.15f, NormCond::Always, "1.00 — базовое", "I07 p.20"},
    {"adapt_lower", NormGroup::M104_995, 0.85f, 1.15f, NormCond::Always, "1.00 — базовое", "I07 p.32"},
    {"adapt_upper", NormGroup::M104_EARLY, 0.85f, 1.15f, NormCond::Always, "1.00 — базовое", "I07 p.2"},
    {"adapt_upper", NormGroup::M104_CODED, 0.85f, 1.15f, NormCond::Always, "1.00 — базовое", "I07 p.10"},
    {"adapt_upper", NormGroup::M111_961, 0.85f, 1.15f, NormCond::Always, "1.00 — базовое", "I07 p.20"},
    {"adapt_upper", NormGroup::M104_995, 0.85f, 1.15f, NormCond::Always, "1.00 — базовое", "I07 p.32"},
};

const Norm* norms(uint16_t* count) {
  *count = sizeof(kNorms) / sizeof(kNorms[0]);
  return kNorms;
}

const Norm* normFor(const char* param, NormGroup g, bool vdo) {
  if (g == NormGroup::None) return nullptr;
  char key[24];
  snprintf(key, sizeof key, "%s%s", param, (vdo && strcmp(param, "adapt_idle") == 0) ? "_vdo" : "");
  uint16_t n;
  const Norm* t = norms(&n);
  for (uint16_t i = 0; i < n; i++)
    if (t[i].group == g && strcmp(t[i].param, key) == 0) return &t[i];
  return nullptr;
}

}  // namespace prof
