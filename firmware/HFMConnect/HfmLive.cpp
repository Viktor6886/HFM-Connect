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

const GroupDef& group(uint8_t g) { return kGroups[g < G_COUNT ? g : 0]; }
const GroupDef& extRead(uint8_t x) { return kExt[x < X_COUNT ? x : 0]; }

LinkStatus pollNative(HfmSession& s, Snapshot& snap) {
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
    {"o2_mv", "Лямбда-зонд (верхний)", "мВ", Category::Mixture, "20:23", "4 × raw (native)", Confidence::REK, "AH", -200, 1100, 0, false},
    {"o2_alt", "Лямбда-зонд (альтернативная формула)", "мВ", Category::Mixture, "20:23", "trunc0(51102·raw − 1928300, 10000)", Confidence::GIP, "AC", -200, 1100, 0, false},
    {"lambda", "Лямбда-регулирование", "%", Category::Mixture, "20:B9", "390625·raw // 10⁶ − 50; нет данных при n=0", Confidence::GIP, "AC", -50, 50, 0, false},
    {"inj_ms", "Время впрыска", "мс", Category::Mixture, "20:54 lo + 20:55 hi", "((lo + 256·hi) // 50) / 10; нет данных при n=0", Confidence::GIP, "AC", 0, 20, 1, false},
    {"ac", "Компрессор кондиционера", "", Category::Status, "20:99 & 0x01", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"decel_cut", "Отсечка топлива при торможении", "", Category::Status, "20:68 & 0x04", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"poststart", "Обогащение после пуска", "", Category::Status, "21:69 & 0x08", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"warmup", "Прогрев", "", Category::Status, "21:69 & 0x04", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"full_load", "Контакт полной нагрузки (WOT)", "", Category::Status, "21:69 & 0x40", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"preheat", "Подогрев впуска", "", Category::Status, "21:67 & 0x08", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"air_pump", "Вторичный воздух (AIR)", "", Category::Status, "21:67 & 0x02", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"camshaft", "Регулирование распредвала Y49", "", Category::Status, "21:67 & 0x10", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"resonance", "Резонансная заслонка Y22/6", "", Category::Status, "21:67 & 0x04", "бит", Confidence::GIP, "AC", 0, 1, 0, true},
    {"speed", "Скорость автомобиля", "км/ч", Category::Extended, "20:47", "12 × raw // 10", Confidence::GIP, "AC", 0, 250, 0, false},
    {"o2_rear", "Лямбда-зонд (нижний)", "мВ", Category::Extended, "20:25", "trunc0(51102·raw − 1928300, 10000)", Confidence::GIP, "AC", -200, 1100, 0, false},
    {"idle_target", "Заданные обороты ХХ", "об/мин", Category::Extended, "20:67", "25 × raw (I6A ≥ 0x41)", Confidence::GIP, "AC", 0, 2000, 0, false},
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

void decode(const Snapshot& s, prof::NormGroup ng, bool vdo, const Decoded* prev, Decoded& d) {
  d = Decoded();
  d.seq = s.seq;
  d.tMs = s.tMs;
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
    setBit(d.v[P_POSTSTART], C[4], 0x08);
    setBit(d.v[P_WARMUP], C[4], 0x04);
    setBit(d.v[P_FULL_LOAD], C[4], 0x40);
    setBit(d.v[P_PREHEAT], C[5], 0x08);
    setBit(d.v[P_AIR_PUMP], C[5], 0x02);
    setBit(d.v[P_CAMSHAFT], C[5], 0x10);
    setBit(d.v[P_RESONANCE], C[5], 0x04);
  }
  if (maf) {
    uint8_t lo = s.g[G_MAF_LO][0], hi = s.g[G_MAF_HI][0];
    setVal(d.v[P_MAF], hfm::mafKgH(lo, hi), lo, hi, 2);
  }
  if (mx) {
    setVal(d.v[P_O2], hfm::o2NativeMv(M[0]), M[0]);
    setVal(d.v[P_O2_ALT], (float)hfm::o2AltMv(M[0]), M[0]);
    setBit(d.v[P_AC], M[1], 0x01);
    setBit(d.v[P_DECEL_CUT], M[2], 0x04);
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
    if (s.extOk[X_IDLE_TARGET]) setVal(d.v[P_IDLE_TARGET], 25.0f * s.ext[X_IDLE_TARGET], s.ext[X_IDLE_TARGET]);
    if (s.extOk[X_STATUS98]) {
      setBit(d.v[P_IDLE_CONTACT], s.ext[X_STATUS98], 0x80);
      setBit(d.v[P_STARTER], s.ext[X_STATUS98], 0x40);
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
