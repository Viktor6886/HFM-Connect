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
    {"trim_plug", 0x20, 1, {0x39}},    // trimming plug voltage (PC tester, Bosch 40/41)
    {"altitude", 0x24, 1, {0x7C}},     // altitude pressure      (PC tester, Bosch 40/41)
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

bool pageNotKept(const uint8_t* sel, const uint8_t* reply, uint8_t n) {
  if (n < 3 || reply[0] == sel[0]) return false;
  return repliesEchoRequest(sel + 1, reply + 1, (uint8_t)(n - 1));
}

uint8_t echoMask(const uint8_t* sel, const uint8_t* reply, uint8_t n) {
  uint8_t m = 0, c = 0;
  for (uint8_t i = 0; i < n && i < 8; i++)
    if (reply[i] == sel[i]) {
      m |= (uint8_t)(1u << i);
      c++;
    }
  return c >= 2 ? m : 0;
}

static uint8_t fullMask(uint8_t n) { return (uint8_t)((1u << n) - 1u); }

// One group: on one prefix (the ECU keeps the page, Bosch templates) or one
// prefix per selector (VDO templates, or an ECU found to keep no page).
static LinkStatus readG(HfmSession& s, const GroupDef& d, uint8_t* out, Snapshot& snap) {
  if (snap.perSelector || d.n == 1) return s.readEach(d.prefix, d.sel, d.n, out);
  LinkStatus st = s.readGroup(d.prefix, d.sel, d.n, out);
  if (st == LinkStatus::Ok && pageNotKept(d.sel, out, d.n)) {
    snap.perSelector = true;  // from now on for this ECU (sticky in the snapshot)
    st = s.readEach(d.prefix, d.sel, d.n, out);
  }
  return st;
}

static LinkStatus pollVdo(HfmSession& s, Snapshot& snap) {
  uint32_t t0 = s.link().nowMs();
  snap.vdoPlan = true;
  for (uint8_t g = 0; g < G_COUNT; g++) snap.groupOk[g] = false;
  for (uint8_t g = 0; g < V_COUNT; g++) snap.vdoOk[g] = false;
  LinkStatus st = LinkStatus::Ok;
  for (uint8_t g = 0; g < V_COUNT; g++) {
    const GroupDef& d = kVdo[g];
    // Factory tester templates of vendor V: every read is "prefix 01 selector"
    // (e.g. 22 01 72 / 22 01 73 for MAF lo/hi) - never a bare selector (D-037).
    st = s.readEach(d.prefix, d.sel, d.n, snap.vdo[g]);
    if (st != LinkStatus::Ok) break;
    snap.vdoOk[g] = true;
    snap.vdoEchoMask[g] = echoMask(d.sel, snap.vdo[g], d.n);
    snap.vdoEcho[g] = snap.vdoEchoMask[g] == fullMask(d.n);
  }
  snap.planEcho = snap.vdoOk[V_P12] && snap.vdoOk[V_P22] && snap.vdoEcho[V_P12] && snap.vdoEcho[V_P22];
  // A single O2 byte cannot be judged alone: it follows the plan.
  if (snap.planEcho && snap.vdoOk[V_O2]) {
    snap.vdoEcho[V_O2] = true;
    snap.vdoEchoMask[V_O2] = 1;
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
    st = readG(s, d, snap.g[g], snap);
    if (st != LinkStatus::Ok) break;
    snap.groupOk[g] = true;
    snap.echoMask[g] = echoMask(d.sel, snap.g[g], d.n);
    snap.groupEcho[g] = snap.echoMask[g] == fullMask(d.n);
  }
  // Single-byte groups are judged in pairs: MAF 48/49 and injection 54/55.
  {
    static const uint8_t mafReq[2] = {0x48, 0x49};
    const uint8_t mafGot[2] = {snap.g[G_MAF_LO][0], snap.g[G_MAF_HI][0]};
    bool e = snap.groupOk[G_MAF_LO] && snap.groupOk[G_MAF_HI] && repliesEchoRequest(mafReq, mafGot, 2);
    snap.groupEcho[G_MAF_LO] = snap.groupEcho[G_MAF_HI] = e;
    snap.echoMask[G_MAF_LO] = snap.echoMask[G_MAF_HI] = e ? 1 : 0;
    static const uint8_t injReq[2] = {0x54, 0x55};
    const uint8_t injGot[2] = {snap.g[G_RPM_LOW][0], snap.g[G_RPM_HIGH][0]};
    if (snap.groupOk[G_RPM_LOW] && snap.groupOk[G_RPM_HIGH] && repliesEchoRequest(injReq, injGot, 2)) {
      snap.groupEcho[G_RPM_LOW] = true;
      snap.echoMask[G_RPM_LOW] = 1;
      snap.echoMask[G_RPM_HIGH] |= 1;
    }
  }
  snap.planEcho = snap.groupOk[G_AIRFLOW] && snap.groupOk[G_COOLANT] && snap.groupEcho[G_AIRFLOW] &&
                  snap.groupEcho[G_COOLANT];
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
  // Single reads are judged together: all of them equal to their selectors.
  uint8_t req[X_COUNT];
  bool all = true;
  for (uint8_t x = 0; x < X_COUNT; x++) {
    req[x] = kExt[x].sel[0];
    all = all && snap.extOk[x];
  }
  snap.extEcho = all && repliesEchoRequest(req, snap.ext, X_COUNT);
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
    {"trim_v", "Корректирующий штекер (CO), напряжение", "В", Category::Extended, "20:39", "raw × 5 / 255 (таблица ПК-тестера для Bosch diag 40/41)", Confidence::GIP, "AC", 0, 5, 2, false},
    {"alt_p", "Высотная коррекция (давление)", "мбар", Category::Extended, "24:7C", "raw × 5 (таблица ПК-тестера для Bosch diag 40/41; единица — кандидат по величине)", Confidence::GIP, "AC", 500, 1100, 0, false},
};

// Physically possible ranges (engine and sensors), not norms. A value outside
// is a wrong address or a wrong formula for this ECU, never a measurement.
bool physRange(uint8_t p, float* lo, float* hi) {
  switch (p) {
    case P_RPM: *lo = 0; *hi = 8000; return true;
    case P_ECT: *lo = -40; *hi = 150; return true;
    case P_IAT: *lo = -40; *hi = 100; return true;
    case P_MAF: *lo = 0; *hi = 900; return true;    // M104 3.2 l at 6500 rpm ~ 710 kg/h
    case P_INJ: *lo = 0; *hi = 30; return true;     // engine cycle at 6500 rpm ~ 18 ms
    case P_SPEED: *lo = 0; *hi = 260; return true;
    case P_IDLE_TARGET: *lo = 0; *hi = 2000; return true;
    case P_ALT: *lo = 500; *hi = 1100; return true;
    default: return false;
  }
}

const ParamDef& param(uint8_t p) { return kParams[p < P_COUNT ? p : 0]; }

const char* qualityCode(Quality q) {
  switch (q) {
    case Quality::Valid: return "VALID";
    case Quality::NotRead: return "NOT_READ";
    case Quality::Partial: return "PARTIAL";
    case Quality::NoData: return "NO_DATA";
    case Quality::OutOfDomain: return "OUT_OF_DOMAIN";
    case Quality::NotSupported: return "NOT_SUPPORTED";
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

// A byte that repeated its own address (D-034) is not a value: the parameter
// is shown as "the ECU does not give this parameter", never as a number.
static bool echoed(uint8_t mask, uint8_t i) { return ((mask >> i) & 1u) != 0; }
static void unsupported(Value& o, uint8_t raw) { setNoData(o, Quality::NotSupported, raw); }

static void setEct(Decoded& d, const Snapshot& s, const Decoded* prev, uint8_t raw) {
  float ect = hfm::coolantNativeC(raw);
  setVal(d.v[P_ECT], ect, raw);
  bool havePrev = prev && prev->v[P_ECT].hasValue;
  uint32_t dt = havePrev ? s.tMs - prev->tMs : 0;
  if (!hfm::coolantPlausible(ect, havePrev ? prev->v[P_ECT].v : 0, dt, havePrev)) d.v[P_ECT].q = Quality::Implausible;
}

static void setIat(Decoded& d, uint8_t raw) {
  float iat;
  if (hfm::iatNativeC(raw, &iat)) setVal(d.v[P_IAT], iat, raw);
  else setNoData(d.v[P_IAT], Quality::OutOfDomain, raw);
}

// VDO plan: same byte converters as Bosch (GIP - the VDO scaling is not
// documented); the addresses are those of the factory tester templates.
static void decodeVdo(const Snapshot& s, const Decoded* prev, Decoded& d) {
  const uint8_t* A = s.vdo[V_P12];
  const uint8_t* B = s.vdo[V_P22];
  const uint8_t* S = s.vdo[V_P23];
  const uint8_t* J = s.vdo[V_INJ];
  const bool p12 = s.vdoOk[V_P12], p22 = s.vdoOk[V_P22], pinj = s.vdoOk[V_INJ], p23 = s.vdoOk[V_P23],
             po2 = s.vdoOk[V_O2];
  const uint8_t eA = s.vdoEchoMask[V_P12], eB = s.vdoEchoMask[V_P22], eJ = s.vdoEchoMask[V_INJ],
                eS = s.vdoEchoMask[V_P23], eO = s.vdoEchoMask[V_O2];
  const uint8_t rpmByte = A[1];
  const bool rpmOk = p12 && !echoed(eA, 1);
  if (p12) {
    if (echoed(eA, 0)) unsupported(d.v[P_ECT], A[0]); else setEct(d, s, prev, A[0]);
    if (rpmOk) setVal(d.v[P_RPM], hfm::rpm(rpmByte), rpmByte); else unsupported(d.v[P_RPM], rpmByte);
    if (echoed(eA, 2)) unsupported(d.v[P_IAT], A[2]); else setIat(d, A[2]);
    if (echoed(eA, 3)) unsupported(d.v[P_AC], A[3]); else setBit(d.v[P_AC], A[3], kMaskVdo[Q_AC]);
    if (echoed(eA, 4)) unsupported(d.v[P_PREHEAT], A[4]); else setBit(d.v[P_PREHEAT], A[4], kMaskVdo[Q_INTAKE]);
    if (echoed(eA, 5)) unsupported(d.v[P_SPEED], A[5]); else setVal(d.v[P_SPEED], (float)hfm::speedKmh(A[5]), A[5]);
  }
  if (p22) {
    if (echoed(eB, 0)) unsupported(d.v[P_THROTTLE], B[0]); else setVal(d.v[P_THROTTLE], hfm::throttleDeg(B[0]), B[0]);
    if (echoed(eB, 1) || echoed(eB, 2)) setNoData(d.v[P_MAF], Quality::NotSupported, B[1], B[2], 2);
    else setVal(d.v[P_MAF], hfm::mafKgH(B[1], B[2]), B[1], B[2], 2);
    int32_t lp;
    if (echoed(eB, 3)) unsupported(d.v[P_LAMBDA], B[3]);
    else if (rpmOk && hfm::lambdaPct(B[3], rpmByte, &lp)) setVal(d.v[P_LAMBDA], (float)lp, B[3]);
    else setNoData(d.v[P_LAMBDA], Quality::NoData, B[3]);
    static const uint8_t kBits[] = {P_IDLE_CONTACT, P_FULL_LOAD, P_CAMSHAFT, P_RESONANCE, P_DECEL_CUT};
    static const uint8_t kQ[] = {Q_IDLE, Q_WOT, Q_CAM, Q_RESON, Q_DECEL};
    for (uint8_t i = 0; i < sizeof kBits; i++) {
      if (echoed(eB, 4)) unsupported(d.v[kBits[i]], B[4]);
      else setBit(d.v[kBits[i]], B[4], kMaskVdo[kQ[i]]);
    }
    if (echoed(eB, 5)) unsupported(d.v[P_AIR_PUMP], B[5]); else setBit(d.v[P_AIR_PUMP], B[5], kMaskVdo[Q_AIR]);
  }
  if (pinj) {
    float ms;
    if (echoed(eJ, 0) || echoed(eJ, 1)) setNoData(d.v[P_INJ], Quality::NotSupported, J[0], J[1], 2);
    else if (rpmOk && hfm::injectionMs(J[0], J[1], rpmByte, &ms)) setVal(d.v[P_INJ], ms, J[0], J[1], 2);
    else setNoData(d.v[P_INJ], Quality::NoData, J[0], J[1], 2);
  }
  if (p23) {
    if (echoed(eS, 0)) unsupported(d.v[P_STARTER], S[0]); else setBit(d.v[P_STARTER], S[0], kMaskVdo[Q_STARTER]);
    if (echoed(eS, 1)) unsupported(d.v[P_POSTSTART], S[1]); else setBit(d.v[P_POSTSTART], S[1], kMaskVdo[Q_POSTSTART]);
    if (echoed(eS, 2)) unsupported(d.v[P_WARMUP], S[2]); else setBit(d.v[P_WARMUP], S[2], kMaskVdo[Q_WARMUP]);
  }
  if (po2) {
    uint8_t o = s.vdo[V_O2][0];
    if (echoed(eO, 0)) {
      unsupported(d.v[P_O2], o);
      unsupported(d.v[P_O2_ALT], o);
    } else {
      setVal(d.v[P_O2], (float)hfm::o2AltMv(o), o);
      setVal(d.v[P_O2_ALT], hfm::o2NativeMv(o), o);
    }
  }
}

void decode(const Snapshot& s, prof::NormGroup ng, bool vdo, const Decoded* prev, Decoded& d, uint8_t diagRaw) {
  d = Decoded();
  d.seq = s.seq;
  d.tMs = s.tMs;
  if (s.vdoPlan) {
    decodeVdo(s, prev, d);
  } else {
  // Bytes answered with their own addresses carry no data (D-034).
  const bool af = s.groupOk[G_AIRFLOW];
  const bool co = s.groupOk[G_COOLANT];
  const bool mx = s.groupOk[G_MIXTURE];
  const bool rl = s.groupOk[G_RPM_LOW];
  const bool rh = s.groupOk[G_RPM_HIGH];
  const bool maf = s.groupOk[G_MAF_LO] && s.groupOk[G_MAF_HI];
  const uint8_t* A = s.g[G_AIRFLOW];
  const uint8_t* C = s.g[G_COOLANT];
  const uint8_t* M = s.g[G_MIXTURE];
  const uint8_t eA = s.echoMask[G_AIRFLOW], eC = s.echoMask[G_COOLANT], eM = s.echoMask[G_MIXTURE];
  const uint8_t rpmByte = A[2];
  const bool rpmOk = af && !echoed(eA, 2);

  if (af) {
    if (rpmOk) setVal(d.v[P_RPM], hfm::rpm(rpmByte), rpmByte); else unsupported(d.v[P_RPM], rpmByte);
    if (echoed(eA, 1)) unsupported(d.v[P_THROTTLE], A[1]); else setVal(d.v[P_THROTTLE], hfm::throttleDeg(A[1]), A[1]);
    if (echoed(eA, 3)) unsupported(d.v[P_LOAD], A[3]); else setVal(d.v[P_LOAD], hfm::loadPct(A[3]), A[3]);
    if (echoed(eA, 4)) unsupported(d.v[P_IGN], A[4]); else setVal(d.v[P_IGN], hfm::ignitionDeg(A[4]), A[4]);
    int32_t lp;
    if (echoed(eA, 5)) unsupported(d.v[P_LAMBDA], A[5]);
    else if (rpmOk && hfm::lambdaPct(A[5], rpmByte, &lp)) setVal(d.v[P_LAMBDA], (float)lp, A[5]);
    else setNoData(d.v[P_LAMBDA], Quality::NoData, A[5]);
  }
  if (co) {
    if (echoed(eC, 1)) unsupported(d.v[P_ECT], C[1]); else setEct(d, s, prev, C[1]);
    if (echoed(eC, 3)) unsupported(d.v[P_IAT], C[3]); else setIat(d, C[3]);
    static const uint8_t k4[] = {P_POSTSTART, P_WARMUP, P_FULL_LOAD};
    static const uint8_t q4[] = {Q_POSTSTART, Q_WARMUP, Q_WOT};
    for (uint8_t i = 0; i < sizeof k4; i++) {
      if (echoed(eC, 4)) unsupported(d.v[k4[i]], C[4]); else setBit(d.v[k4[i]], C[4], kMaskBosch[q4[i]]);
    }
    static const uint8_t k5[] = {P_PREHEAT, P_AIR_PUMP, P_CAMSHAFT, P_RESONANCE};
    static const uint8_t q5[] = {Q_INTAKE, Q_AIR, Q_CAM, Q_RESON};
    for (uint8_t i = 0; i < sizeof k5; i++) {
      if (echoed(eC, 5)) unsupported(d.v[k5[i]], C[5]); else setBit(d.v[k5[i]], C[5], kMaskBosch[q5[i]]);
    }
  }
  if (maf) {
    uint8_t lo = s.g[G_MAF_LO][0], hi = s.g[G_MAF_HI][0];
    if (echoed(s.echoMask[G_MAF_LO], 0)) setNoData(d.v[P_MAF], Quality::NotSupported, lo, hi, 2);
    else setVal(d.v[P_MAF], hfm::mafKgH(lo, hi), lo, hi, 2);
  }
  if (mx) {
    // Primary O2: factory tester formula (signed, matches the factory norm
    // -200..+1000 mV); 4 x raw kept as the alternative.
    if (echoed(eM, 0)) {
      unsupported(d.v[P_O2], M[0]);
      unsupported(d.v[P_O2_ALT], M[0]);
    } else {
      setVal(d.v[P_O2], (float)hfm::o2AltMv(M[0]), M[0]);
      setVal(d.v[P_O2_ALT], hfm::o2NativeMv(M[0]), M[0]);
    }
    if (echoed(eM, 1)) unsupported(d.v[P_AC], M[1]); else setBit(d.v[P_AC], M[1], kMaskBosch[Q_AC]);
    if (echoed(eM, 2)) unsupported(d.v[P_DECEL_CUT], M[2]); else setBit(d.v[P_DECEL_CUT], M[2], kMaskBosch[Q_DECEL]);
  }
  if (rl && rh && af) {
    float ms;
    uint8_t lo = s.g[G_RPM_LOW][0], hi = s.g[G_RPM_HIGH][0];
    if (echoed(s.echoMask[G_RPM_LOW], 0) || echoed(s.echoMask[G_RPM_HIGH], 0))
      setNoData(d.v[P_INJ], Quality::NotSupported, lo, hi, 2);
    else if (rpmOk && hfm::injectionMs(lo, hi, rpmByte, &ms)) setVal(d.v[P_INJ], ms, lo, hi, 2);
    else setNoData(d.v[P_INJ], Quality::NoData, lo, hi, 2);
  }
  if (s.extRead && s.extEcho) {
    static const uint8_t kExtP[] = {P_SPEED, P_O2_REAR, P_IDLE_CONTACT, P_STARTER, P_TRIM, P_ALT};
    for (uint8_t i = 0; i < sizeof kExtP; i++) unsupported(d.v[kExtP[i]], s.ext[0]);
  } else if (s.extRead) {
    if (s.extOk[X_TRIM]) setVal(d.v[P_TRIM], s.ext[X_TRIM] * 5.0f / 255.0f, s.ext[X_TRIM]);
    if (s.extOk[X_ALT]) setVal(d.v[P_ALT], s.ext[X_ALT] * 5.0f, s.ext[X_ALT]);
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
    bool srcOk = early ? (co && !echoed(eC, 0)) : (s.extRead && s.extOk[X_IDLE_TARGET] && !s.extEcho);
    if (srcOk && rpmOk) {
      uint16_t target, rpmOut;
      uint8_t src = early ? C[0] : s.ext[X_IDLE_TARGET];
      hfm::idleTarget(C[0], s.ext[X_IDLE_TARGET], rpmByte, diagRaw, &target, &rpmOut);
      setVal(d.v[P_IDLE_TARGET], (float)target, src);
    }
  }
  }

  // ---- physical plausibility ---------------------------------------------------
  // A number outside what the engine can do comes from a wrong address or
  // formula for this ECU: it is not shown (raw bytes stay for the report).
  for (uint8_t p = 0; p < P_COUNT; p++) {
    Value& val = d.v[p];
    float lo, hi;
    if (val.hasValue && physRange(p, &lo, &hi) && (val.v < lo || val.v > hi)) {
      val.hasValue = false;
      val.q = Quality::Implausible;
    }
  }

  // ---- engine mode ------------------------------------------------------------
  // The factory norms (I07) are stated for the warm engine at idle (ECT > 80 C,
  // P/N); a norm is compared only in the mode it is given for.
  bool ectOk = d.v[P_ECT].hasValue && d.v[P_ECT].q == Quality::Valid;
  bool rpmValid = d.v[P_RPM].hasValue && d.v[P_RPM].q == Quality::Valid;
  float ect = ectOk ? d.v[P_ECT].v : -99;
  float rpm = rpmValid ? d.v[P_RPM].v : 0;
  d.warm = ectOk && ect > 80.0f;
  if (!rpmValid) d.mode = EngineMode::Unknown;
  else if (rpm < 1.0f) d.mode = EngineMode::Off;
  else if (rpm < 450.0f) d.mode = EngineMode::Cranking;
  else if (rpm <= 1000.0f) d.mode = EngineMode::Idle;
  else d.mode = EngineMode::Raised;
  const bool running = d.mode == EngineMode::Idle || d.mode == EngineMode::Raised;
  d.conditionsWarmIdle = d.warm && d.mode == EngineMode::Idle;
  d.conditionsWarmRunning = d.warm && running;

  // ---- norm layer -------------------------------------------------------------
  for (uint8_t p = 0; p < P_COUNT; p++) {
    Value& val = d.v[p];
    const prof::Norm* n = prof::normFor(kParams[p].key, ng, vdo);
    if (!n) {
      val.norm = NormState::NoNorm;
      d.why[p] = NormWhy::None;
      continue;
    }
    val.normRef = n;
    val.normLo = n->lo;
    val.normHi = n->hi;
    NormWhy w = NormWhy::Applied;
    if (!val.hasValue || val.q != Quality::Valid) w = NormWhy::NoValue;
    else if (n->cond != prof::NormCond::Always) {
      if (!ectOk) w = NormWhy::EctUnknown;
      else if (!d.warm) w = NormWhy::Cold;
      else if (n->cond == prof::NormCond::WarmIdle && d.mode != EngineMode::Idle) w = NormWhy::NotIdle;
      else if (n->cond == prof::NormCond::WarmRunning && !running) w = NormWhy::NotRunning;
    }
    d.why[p] = w;
    if (w != NormWhy::Applied) { val.norm = NormState::NotApplicable; continue; }
    val.norm = val.v < n->lo ? NormState::Below : (val.v > n->hi ? NormState::Above : NormState::InSpec);
  }
}

const char* engineModeCode(EngineMode m) {
  switch (m) {
    case EngineMode::Off: return "OFF";
    case EngineMode::Cranking: return "CRANKING";
    case EngineMode::Idle: return "IDLE";
    case EngineMode::Raised: return "RAISED";
    default: return "UNKNOWN";
  }
}

const char* normWhyCode(NormWhy w) {
  switch (w) {
    case NormWhy::Applied: return "APPLIED";
    case NormWhy::NoValue: return "NO_VALUE";
    case NormWhy::Cold: return "COLD";
    case NormWhy::NotIdle: return "NOT_IDLE";
    case NormWhy::NotRunning: return "NOT_RUNNING";
    case NormWhy::EctUnknown: return "ECT_UNKNOWN";
    default: return "NONE";
  }
}

}  // namespace live
