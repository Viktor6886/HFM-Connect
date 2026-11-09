#include "HfmLive.h"

#include "HfmDecode.h"

namespace live {

static const GroupDef kGroups[G_COUNT] = {
    {"rpm_low", 0x20, 1, {0x54}},
    {"rpm_high_context", 0x20, 3, {0x55, 0xC4, 0xB9}},
    {"mixture_iat", 0x20, 3, {0x23, 0x99, 0x68}},
    {"airflow", 0x20, 6, {0x49, 0xBD, 0xB7, 0xC2, 0xC4, 0xB9}},
    {"coolant", 0x21, 6, {0xD7, 0xBF, 0xB7, 0xBE, 0x69, 0x67}},
    {"air_mass_low", 0x20, 1, {0x48}},
    {"air_mass_high", 0x20, 1, {0x49}},
};

// Extended reads with an explicit prefix (no reliance on a remembered page).
static const GroupDef kExt[X_COUNT] = {
    {"speed", 0x20, 1, {0x47}},        // km/h = 12*d//10                  AC
    {"o2_rear", 0x20, 1, {0x25}},      // alt O2 formula                   AC
    {"idle_target", 0x20, 1, {0x67}},  // 25*raw (variant I6A >= 0x41)      AC
    {"status98", 0x20, 1, {0x98}},     // status bits                      AC
};

// VDO plan (factory tester templates for vendor V, GIP; never field-tested).
static const GroupDef kVdo[V_COUNT] = {
    {"vdo_12", 0x12, 6, {0x76, 0x7E, 0x77, 0x7A, 0x7B, 0x7F}},  // ECT, rpm, IAT, lever/AC, intake, speed
    {"vdo_22", 0x22, 6, {0x78, 0x72, 0x73, 0x7F, 0x7A, 0x7B}},  // throttle, MAF lo/hi, lambda, status, AIR
    {"vdo_inj", 0x22, 2, {0x74, 0x75}},                         // injection lo/hi
    {"vdo_23", 0x23, 3, {0xA4, 0x7F, 0x7E}},                    // starter, post-start, warm-up
    {"vdo_o2", 0x20, 1, {0xF2}},                                // O2 sensor
};

// Status masks by display index q (0 idle contact, 1 starter, 2 post-start,
// 3 AIR, 4 resonance, 5 partial-load intake, 6 camshaft, 7 selector P/N,
// 8 A/C, 9 full load, 10 decel cut-off, 11 warm-up).
static const uint8_t kMaskBosch[12] = {0x80, 0x40, 0x08, 0x02, 0x04, 0x08, 0x10, 0x20, 0x01, 0x40, 0x04, 0x04};
static const uint8_t kMaskVdo[12] = {0x40, 0x80, 0x02, 0x08, 0x04, 0x02, 0x01, 0x08, 0x02, 0x20, 0x08, 0x02};
enum : uint8_t { Q_IDLE, Q_STARTER, Q_POSTSTART, Q_AIR, Q_RESON, Q_INTAKE, Q_CAM, Q_LEVER, Q_AC, Q_WOT, Q_DECEL, Q_WARMUP };

uint8_t statusMaskBosch(uint8_t q) { return q < 12 ? kMaskBosch[q] : 0; }
uint8_t statusMaskVdo(uint8_t q) { return q < 12 ? kMaskVdo[q] : 0; }

const GroupDef& group(uint8_t g) { return kGroups[g < G_COUNT ? g : 0]; }
const GroupDef& extRead(uint8_t x) { return kExt[x < X_COUNT ? x : 0]; }
const GroupDef& vdoGroup(uint8_t g) { return kVdo[g < V_COUNT ? g : 0]; }

static LinkStatus pollVdo(HfmSession& s, Snapshot& snap) {
  uint32_t t0 = s.link().nowMs();
  snap.vdoPlan = true;
  for (uint8_t g = 0; g < G_COUNT; g++) snap.groupOk[g] = false;
  for (uint8_t g = 0; g < V_COUNT; g++) snap.vdoOk[g] = false;
  LinkStatus st = LinkStatus::Ok;
  for (uint8_t g = 0; g < V_COUNT; g++) {
    const GroupDef& d = kVdo[g];
    st = s.readGroup(d.prefix, d.sel, d.n, snap.vdo[g]);
    if (st != LinkStatus::Ok) break;
    snap.vdoOk[g] = true;
  }
  snap.lastStatus = st;
  snap.tMs = s.link().nowMs();
  snap.cycleMs = (uint16_t)(snap.tMs - t0);
  snap.seq++;
  return st;
}

LinkStatus pollNative(HfmSession& s, Snapshot& snap, bool vdo) {
  if (vdo) return pollVdo(s, snap);
  snap.vdoPlan = false;
  uint32_t t0 = s.link().nowMs();
  for (uint8_t g = 0; g < G_COUNT; g++) snap.groupOk[g] = false;
  LinkStatus st = LinkStatus::Ok;
  for (uint8_t g = 0; g < G_COUNT; g++) {
    const GroupDef& d = kGroups[g];
    st = s.readGroup(d.prefix, d.sel, d.n, snap.g[g]);
    if (st != LinkStatus::Ok) break;
    snap.groupOk[g] = true;
  }
  // MAF bytes must belong to the same cycle; partial cycle invalidates both.
  if (!(snap.groupOk[G_MAF_LO] && snap.groupOk[G_MAF_HI])) snap.groupOk[G_MAF_LO] = snap.groupOk[G_MAF_HI] = false;
  snap.lastStatus = st;
  snap.tMs = s.link().nowMs();
  snap.cycleMs = (uint16_t)(snap.tMs - t0);
  snap.seq++;
  return st;
}

LinkStatus pollExtended(HfmSession& s, Snapshot& snap) {
  snap.extRead = true;
  LinkStatus st = LinkStatus::Ok;
  for (uint8_t x = 0; x < X_COUNT; x++) {
    snap.extOk[x] = false;
    const GroupDef& d = kExt[x];
    st = s.readGroup(d.prefix, d.sel, d.n, &snap.ext[x]);
    if (st != LinkStatus::Ok) break;
    snap.extOk[x] = true;
  }
  return st;
}

// ---- registry -----------------------------------------------------------------
static const ParamDef kParams[P_COUNT] = {
    {"rpm", "Обороты двигателя", "об/мин", Category::Base, "20:B7", "25 × raw", Confidence::REK, "AV", 0, 7000, 0, false},
    {"ect", "Температура ОЖ", "°C", Category::Base, "21:BF", "14-точечная интерполяция", Confidence::REK, "AV", -40, 130, 1, false},
    {"iat", "Температура всасываемого воздуха", "°C", Category::Base, "21:BE", "8-точечная интерполяция, вне 92..230 — нет значения", Confidence::REK, "AH", -40, 100, 1, false},
    {"maf", "Расход воздуха (HFM)", "кг/ч", Category::Base, "20:48 lo + 20:49 hi", "(lo + 256·hi) / 10, один цикл", Confidence::REK, "AV/AH", 0, 600, 1, false},
    {"throttle", "Угол дроссельной заслонки", "°", Category::Throttle, "20:BD", "raw × 90 / 256", Confidence::REK, "AH", 0, 90, 2, false},
    {"load", "Нагрузка двигателя", "%", Category::Base, "20:C2", "raw / 2", Confidence::REK, "AH", 0, 100, 1, false},
    {"ign_deg", "Угол опережения зажигания", "°ПКВ", Category::Ignition, "20:C4", "55 − raw × 90 / 256", Confidence::REK, "AH", -35, 55, 1, false},
    {"o2_mv", "Лямбда-зонд (верхний)", "мВ", Category::Mixture, "20:23", "trunc0(51102·raw − 1928300, 10000) — формула заводского тестера (−193…+1110 мВ)", Confidence::REK, "AC", -200, 1100, 0, false},
    {"o2_alt", "Лямбда-зонд (альтернатива 4 × raw)", "мВ", Category::Mixture, "20:23", "4 × raw (0…1020 мВ, без отрицательной области)", Confidence::GIP, "AH", -200, 1100, 0, false},
    {"lambda", "Лямбда-регулирование", "%", Category::Mixture, "20:B9", "390625·raw // 10⁶ − 50; нет данных при n=0", Confidence::GIP, "AC", -50, 50, 0, false},
    {"inj_ms", "Время впрыска", "мс", Category::Mixture, "20:54 lo + 20:55 hi", "((lo + 256·hi) // 50) / 10; нет данных при n=0", Confidence::GIP, "AC", 0, 20, 1, false},
    {"ac", "Компрессор кондиционера", "", Category::Status, "20:99 & 0x01", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"decel_cut", "Отсечка топлива при торможении", "", Category::Status, "20:68 & 0x04", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"poststart", "Обогащение после пуска", "", Category::Status, "21:69 & 0x08", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"warmup", "Прогрев", "", Category::Status, "21:69 & 0x04", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"full_load", "Контакт полной нагрузки (WOT)", "", Category::Status, "21:69 & 0x40", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"preheat", "Впускной коллектор (частичная нагрузка)", "", Category::Status, "21:67 & 0x08", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"air_pump", "Вторичный воздух (AIR)", "", Category::Status, "21:67 & 0x02", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"camshaft", "Регулирование распредвала Y49", "", Category::Status, "21:67 & 0x10", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"resonance", "Резонансная заслонка Y22/6", "", Category::Status, "21:67 & 0x04", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"speed", "Скорость автомобиля", "км/ч", Category::Extended, "20:47", "12 × raw // 10", Confidence::GIP, "AC", 0, 250, 0, false},
    {"o2_rear", "Лямбда-зонд (нижний)", "мВ", Category::Extended, "20:25", "trunc0(51102·raw − 1928300, 10000)", Confidence::GIP, "AC", -200, 1100, 0, false},
    {"idle_target", "Заданные обороты ХХ", "об/мин", Category::Extended, "20:67 / 21:D7", "25 × raw; источник 21:D7 при diag < 0x41, иначе 20:67; 0 при n = 0", Confidence::GIP, "AC", 0, 2000, 0, false},
    {"idle_contact", "Контакт ХХ (CTP)", "", Category::Extended, "20:98 & 0x80", "бит (вариант таблицы q=0); альтернатива: T50", Confidence::GIP, "AC", 0, 1, 0, true},
    {"starter", "Стартер (клемма 50)", "", Category::Extended, "20:98 & 0x40", "бит (вариант таблицы q=1); альтернатива: 0x80", Confidence::GIP, "AC", 0, 1, 0, true},
};

const ParamDef& param(uint8_t p) { return kParams[p < P_COUNT ? p : 0]; }

const char* qualityCode(Quality q) {
  switch (q) {
    case Quality::Valid: return "VALID";
    case Quality::NotRead: return "NOT_READ";
    case Quality::Partial: return "PARTIAL";
    case Quality::NoData: return "NO_DATA";
    case Quality::OutOfDomain: return "OUT_OF_DOMAIN";
    default: return "IMPLAUSIBLE";
  }
}

const char* normStateCode(NormState n) {
  switch (n) {
    case NormState::InSpec: return "IN_SPEC";
    case NormState::Below: return "BELOW";
    case NormState::Above: return "ABOVE";
    case NormState::NotApplicable: return "NOT_APPLICABLE";
    default: return "NO_NORM";
  }
}

static void setVal(Value& o, float v, uint8_t r0, uint8_t r1 = 0, uint8_t n = 1) {
  o.v = v;
  o.hasValue = true;
  o.q = Quality::Valid;
  o.raw0 = r0;
  o.raw1 = r1;
  o.rawCount = n;
}

static void setNoData(Value& o, Quality q, uint8_t r0, uint8_t r1 = 0, uint8_t n = 1) {
  o.hasValue = false;
  o.q = q;
  o.raw0 = r0;
  o.raw1 = r1;
  o.rawCount = n;
}

static void setBit(Value& o, uint8_t raw, uint8_t mask) { setVal(o, (raw & mask) ? 1.0f : 0.0f, raw); }

// VDO plan: same byte converters as Bosch (GIP - the VDO scaling is not
// documented); values that need an unknown VDO formula are not shown.
static void decodeVdo(const Snapshot& s, const Decoded* prev, Decoded& d) {
  const uint8_t* A = s.vdo[V_P12];
  const uint8_t* B = s.vdo[V_P22];
  const uint8_t* S = s.vdo[V_P23];
  uint8_t rpmByte = A[1];
  if (s.vdoOk[V_P12]) {
    float ect = hfm::coolantNativeC(A[0]);
    setVal(d.v[P_ECT], ect, A[0]);
    bool havePrev = prev && prev->v[P_ECT].hasValue;
    uint32_t dt = havePrev ? s.tMs - prev->tMs : 0;
    if (!hfm::coolantPlausible(ect, havePrev ? prev->v[P_ECT].v : 0, dt, havePrev)) d.v[P_ECT].q = Quality::Implausible;
    setVal(d.v[P_RPM], hfm::rpm(rpmByte), rpmByte);
    float iat;
    if (hfm::iatNativeC(A[2], &iat)) setVal(d.v[P_IAT], iat, A[2]);
    else setNoData(d.v[P_IAT], Quality::OutOfDomain, A[2]);
    setBit(d.v[P_AC], A[3], kMaskVdo[Q_AC]);
    setBit(d.v[P_PREHEAT], A[4], kMaskVdo[Q_INTAKE]);
    setVal(d.v[P_SPEED], (float)hfm::speedKmh(A[5]), A[5]);
  }
  if (s.vdoOk[V_P22]) {
    setVal(d.v[P_THROTTLE], hfm::throttleDeg(B[0]), B[0]);
    setVal(d.v[P_MAF], hfm::mafKgH(B[1], B[2]), B[1], B[2], 2);
    int32_t lp;
    if (s.vdoOk[V_P12] && hfm::lambdaPct(B[3], rpmByte, &lp)) setVal(d.v[P_LAMBDA], (float)lp, B[3]);
    else setNoData(d.v[P_LAMBDA], Quality::NoData, B[3]);
    setBit(d.v[P_IDLE_CONTACT], B[4], kMaskVdo[Q_IDLE]);
    setBit(d.v[P_FULL_LOAD], B[4], kMaskVdo[Q_WOT]);
    setBit(d.v[P_CAMSHAFT], B[4], kMaskVdo[Q_CAM]);
    setBit(d.v[P_RESONANCE], B[4], kMaskVdo[Q_RESON]);
    setBit(d.v[P_DECEL_CUT], B[4], kMaskVdo[Q_DECEL]);
    setBit(d.v[P_AIR_PUMP], B[5], kMaskVdo[Q_AIR]);
  }
  if (s.vdoOk[V_INJ] && s.vdoOk[V_P12]) {
    float ms;
    uint8_t lo = s.vdo[V_INJ][0], hi = s.vdo[V_INJ][1];
    if (hfm::injectionMs(lo, hi, rpmByte, &ms)) setVal(d.v[P_INJ], ms, lo, hi, 2);
    else setNoData(d.v[P_INJ], Quality::NoData, lo, hi, 2);
  }
  if (s.vdoOk[V_P23]) {
    setBit(d.v[P_STARTER], S[0], kMaskVdo[Q_STARTER]);
    setBit(d.v[P_POSTSTART], S[1], kMaskVdo[Q_POSTSTART]);
    setBit(d.v[P_WARMUP], S[2], kMaskVdo[Q_WARMUP]);
  }
  if (s.vdoOk[V_O2]) {
    uint8_t o = s.vdo[V_O2][0];
    setVal(d.v[P_O2], (float)hfm::o2AltMv(o), o);
    setVal(d.v[P_O2_ALT], hfm::o2NativeMv(o), o);
  }
}

void decode(const Snapshot& s, prof::NormGroup ng, bool vdo, const Decoded* prev, Decoded& d, uint8_t diagRaw) {
  d = Decoded();
  d.seq = s.seq;
  d.tMs = s.tMs;
  if (s.vdoPlan) {
    decodeVdo(s, prev, d);
  } else {
  const bool af = s.groupOk[G_AIRFLOW], co = s.groupOk[G_COOLANT], mx = s.groupOk[G_MIXTURE];
  const bool rl = s.groupOk[G_RPM_LOW], rh = s.groupOk[G_RPM_HIGH], maf = s.groupOk[G_MAF_LO] && s.groupOk[G_MAF_HI];
  const uint8_t* A = s.g[G_AIRFLOW];
  const uint8_t* C = s.g[G_COOLANT];
  const uint8_t* M = s.g[G_MIXTURE];
  uint8_t rpmByte = A[2];

  if (af) {
    setVal(d.v[P_RPM], hfm::rpm(rpmByte), rpmByte);
    setVal(d.v[P_THROTTLE], hfm::throttleDeg(A[1]), A[1]);
    setVal(d.v[P_LOAD], hfm::loadPct(A[3]), A[3]);
    setVal(d.v[P_IGN], hfm::ignitionDeg(A[4]), A[4]);
    int32_t lp;
    if (hfm::lambdaPct(A[5], rpmByte, &lp)) setVal(d.v[P_LAMBDA], (float)lp, A[5]);
    else setNoData(d.v[P_LAMBDA], Quality::NoData, A[5]);
  }
  if (co) {
    float ect = hfm::coolantNativeC(C[1]);
    setVal(d.v[P_ECT], ect, C[1]);
    bool havePrev = prev && prev->v[P_ECT].hasValue;
    uint32_t dt = havePrev ? s.tMs - prev->tMs : 0;
    if (!hfm::coolantPlausible(ect, havePrev ? prev->v[P_ECT].v : 0, dt, havePrev)) d.v[P_ECT].q = Quality::Implausible;
    float iat;
    if (hfm::iatNativeC(C[3], &iat)) setVal(d.v[P_IAT], iat, C[3]);
    else setNoData(d.v[P_IAT], Quality::OutOfDomain, C[3]);
    setBit(d.v[P_POSTSTART], C[4], kMaskBosch[Q_POSTSTART]);
    setBit(d.v[P_WARMUP], C[4], kMaskBosch[Q_WARMUP]);
    setBit(d.v[P_FULL_LOAD], C[4], kMaskBosch[Q_WOT]);
    setBit(d.v[P_PREHEAT], C[5], kMaskBosch[Q_INTAKE]);
    setBit(d.v[P_AIR_PUMP], C[5], kMaskBosch[Q_AIR]);
    setBit(d.v[P_CAMSHAFT], C[5], kMaskBosch[Q_CAM]);
    setBit(d.v[P_RESONANCE], C[5], kMaskBosch[Q_RESON]);
  }
  if (maf) {
    uint8_t lo = s.g[G_MAF_LO][0], hi = s.g[G_MAF_HI][0];
    setVal(d.v[P_MAF], hfm::mafKgH(lo, hi), lo, hi, 2);
  }
  if (mx) {
    // Primary O2: factory tester formula (signed, matches the factory norm
    // -200..+1000 mV); 4 x raw kept as the alternative.
    setVal(d.v[P_O2], (float)hfm::o2AltMv(M[0]), M[0]);
    setVal(d.v[P_O2_ALT], hfm::o2NativeMv(M[0]), M[0]);
    setBit(d.v[P_AC], M[1], kMaskBosch[Q_AC]);
    setBit(d.v[P_DECEL_CUT], M[2], kMaskBosch[Q_DECEL]);
  }
  if (rl && rh && af) {
    float ms;
    uint8_t lo = s.g[G_RPM_LOW][0], hi = s.g[G_RPM_HIGH][0];
    if (hfm::injectionMs(lo, hi, rpmByte, &ms)) setVal(d.v[P_INJ], ms, lo, hi, 2);
    else setNoData(d.v[P_INJ], Quality::NoData, lo, hi, 2);
  }
  if (s.extRead) {
    if (s.extOk[X_SPEED]) setVal(d.v[P_SPEED], (float)hfm::speedKmh(s.ext[X_SPEED]), s.ext[X_SPEED]);
    if (s.extOk[X_O2_REAR]) setVal(d.v[P_O2_REAR], (float)hfm::o2AltMv(s.ext[X_O2_REAR]), s.ext[X_O2_REAR]);
    if (s.extOk[X_STATUS98]) {
      setBit(d.v[P_IDLE_CONTACT], s.ext[X_STATUS98], kMaskBosch[Q_IDLE]);
      setBit(d.v[P_STARTER], s.ext[X_STATUS98], kMaskBosch[Q_STARTER]);
    }
  }
  // Idle target (factory tester): source 21:D7 (coolant group) when the
  // diagnosis byte is below 0x41, otherwise 20:67; 0 while the engine is stopped.
  {
    bool early = diagRaw < 0x41;
    bool srcOk = early ? co : (s.extRead && s.extOk[X_IDLE_TARGET]);
    if (srcOk && af) {
      uint16_t target, rpmOut;
      uint8_t src = early ? C[0] : s.ext[X_IDLE_TARGET];
      hfm::idleTarget(C[0], s.ext[X_IDLE_TARGET], rpmByte, diagRaw, &target, &rpmOut);
      setVal(d.v[P_IDLE_TARGET], (float)target, src);
    }
  }
  }

  // ---- norm layer -------------------------------------------------------------
  bool ectOk = d.v[P_ECT].hasValue && d.v[P_ECT].q == Quality::Valid;
  bool rpmOk = d.v[P_RPM].hasValue;
  float ect = ectOk ? d.v[P_ECT].v : -99;
  float rpm = rpmOk ? d.v[P_RPM].v : 0;
  d.conditionsWarmIdle = ectOk && rpmOk && ect > 80.0f && rpm >= 450 && rpm <= 1000;
  d.conditionsWarmRunning = ectOk && rpmOk && ect > 80.0f && rpm >= 450;

  for (uint8_t p = 0; p < P_COUNT; p++) {
    Value& val = d.v[p];
    const prof::Norm* n = prof::normFor(kParams[p].key, ng, vdo);
    if (!n) { val.norm = NormState::NoNorm; continue; }
    val.normRef = n;
    val.normLo = n->lo;
    val.normHi = n->hi;
    bool cond = false;
    switch (n->cond) {
      case prof::NormCond::WarmIdle: cond = d.conditionsWarmIdle; break;
      case prof::NormCond::WarmRunning: cond = d.conditionsWarmRunning; break;
      case prof::NormCond::WarmAny: cond = ectOk && ect > 80.0f; break;
      default: cond = true;
    }
    if (!val.hasValue || val.q != Quality::Valid || !cond) { val.norm = NormState::NotApplicable; continue; }
    val.norm = val.v < n->lo ? NormState::Below : (val.v > n->hi ? NormState::Above : NormState::InSpec);
  }
}

}  // namespace live
