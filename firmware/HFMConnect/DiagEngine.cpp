#ifdef ARDUINO
#include "DiagEngine.h"

#include <LittleFS.h>
#include <esp_random.h>

#include "Config.h"
#include "FtdiLink.h"
#include "KLineUart.h"

DiagEngine Engine;

const char* jobCode(Job j) {
  static const char* const names[] = {"none", "connect", "disconnect", "identify", "read_dtc", "read_adapt",
                                      "read_coding", "read_vin", "live_start", "live_stop", "svc_prepare",
                                      "svc_execute", "purge_on", "purge_off", "stop", "impulse_read",
                                      "ack_fault"};
  return names[(uint8_t)j < sizeof(names) / sizeof(names[0]) ? (uint8_t)j : 0];
}

const char* jobStateCode(JobState s) {
  switch (s) {
    case JobState::Queued: return "queued";
    case JobState::Running: return "running";
    case JobState::Done: return "done";
    case JobState::Failed: return "failed";
    default: return "idle";
  }
}

const char* linkPhaseCode(LinkPhase p) {
  switch (p) {
    case LinkPhase::NoAdapter: return "no_adapter";
    case LinkPhase::Paused: return "paused";
    case LinkPhase::Connecting: return "connecting";
    case LinkPhase::Recovering: return "recovering";
    case LinkPhase::Ready: return "ready";
    case LinkPhase::Error: return "error";
    default: return "idle";
  }
}

static void traceLock(void* m) { xSemaphoreTakeRecursive((SemaphoreHandle_t)m, portMAX_DELAY); }
static void traceUnlock(void* m) { xSemaphoreGiveRecursive((SemaphoreHandle_t)m); }
static SemaphoreHandle_t gTraceMtx = nullptr;

void DiagEngine::begin(Settings* s) {
  settings_ = s;
  mtx_ = xSemaphoreCreateRecursiveMutex();
  gTraceMtx = xSemaphoreCreateRecursiveMutex();
  trace_.setLock(traceLock, traceUnlock, gTraceMtx);
  trace_.setEnabled(s->traceCapture);
#if HFM_TRANSPORT_FTDI
  // FTDI K-Line adapter on the ESP32-S3 USB OTG port (USB host mode).
  ftdi_ = new FtdiLink();
  if (!ftdi_->begin()) Serial.println("USB host init failed (FTDI adapter unavailable)");
  link_ = ftdi_;
#else
  kline_ = new KLineUart(HFM_KLINE_UART_NUM, HFM_KLINE_TX_PIN, HFM_KLINE_RX_PIN, HFM_KLINE_TX_INVERTED,
                         HFM_KLINE_RX_INVERTED);
  // Install the UART immediately so the transceiver TX input is driven idle
  // HIGH from boot (no floating input / K-Line pull-down glitch).
  if (!kline_->open()) Serial.println("K-Line UART init failed");
  kline_->close();
  link_ = kline_;
#endif
  snprintf(sessionId_, sizeof sessionId_, "S%08lX", (unsigned long)esp_random());
  pinMode(HFM_WRITE_EN_PIN, INPUT_PULLUP);
  session_ = new HfmSession(*link_, trace_);
  applySettings();
  EcuIdent none;
  prof::ResolveOptions o;
  prof::resolve(none, o, st_.profile);
  xTaskCreatePinnedToCore(taskEntry, "hfm-engine", HFM_ENGINE_TASK_STACK, this, 3, nullptr, HFM_ENGINE_TASK_CORE);
}

void DiagEngine::applySettings() {
  LinkTiming t;
  t.breakMs = settings_->breakMs;
  t.postBreakMs = settings_->postBreakMs;
  t.byteDeadlineMs = settings_->byteTimeoutMs;
  t.resyncSilenceMs = HFM_RESYNC_SILENCE_MS;
  session_->setTiming(t);
  session_->setExperimentalReads(settings_->experimentalReads);
  if (ftdi_)
    ftdi_->setWakeConfig(settings_->breakMs, settings_->ftdiModem, settings_->wakeAuto, settings_->wakeLearnedBreak,
                         settings_->wakeLearnedModem);
}

const char* DiagEngine::transportName() { return link_ ? link_->name() : "none"; }

void DiagEngine::lock() { xSemaphoreTakeRecursive(mtx_, portMAX_DELAY); }
void DiagEngine::unlock() { xSemaphoreGiveRecursive(mtx_); }

JobInfo* DiagEngine::ringFind(uint32_t id) {
  for (uint8_t i = 0; i < kRing; i++)
    if (ring_[i].id == id && id) return &ring_[i];
  return nullptr;
}

bool DiagEngine::jobInfo(uint32_t id, JobInfo& out) {
  JobInfo* r = ringFind(id);
  if (!r) return false;
  out = *r;
  return true;
}

uint32_t DiagEngine::submit(Job j, uint32_t arg, const char* text) {
  lock();
  const bool urgent = j == Job::Stop || j == Job::LiveStop;
  const bool hasText = text && text[0];
  // Merge with an identical waiting job; a Connect also with the running one.
  for (uint8_t i = 0; i < qCount_ && !hasText; i++)
    if (q_[i].job == j && q_[i].arg == arg) {
      uint32_t id = q_[i].id;
      unlock();
      return id;
    }
  if (j == Job::Connect && st_.job.job == Job::Connect && st_.job.state == JobState::Running) {
    uint32_t id = st_.job.id;
    unlock();
    return id;
  }
  if (qCount_ >= kQueue && !urgent) {
    unlock();
    return 0;
  }
  if (qCount_ >= kQueue) {  // urgent job: drop the last waiting one
    JobInfo* r = ringFind(q_[kQueue - 1].id);
    if (r) {
      r->state = JobState::Failed;
      strlcpy(r->message, "Отменено: выполняется STOP", sizeof r->message);
    }
    qCount_ = kQueue - 1;
  }
  uint32_t id = nextJobId_++;
  QJob qj;
  qj.job = j;
  qj.arg = arg;
  qj.id = id;
  strlcpy(qj.text, text ? text : "", sizeof qj.text);
  if (urgent) {
    memmove(&q_[1], &q_[0], qCount_ * sizeof(QJob));
    q_[0] = qj;
  } else {
    q_[qCount_] = qj;
  }
  qCount_++;
  JobInfo& r = ring_[ringNext_];
  ringNext_ = (uint8_t)((ringNext_ + 1) % kRing);
  r = JobInfo();
  r.id = id;
  r.job = j;
  r.state = JobState::Queued;
  unlock();
  return id;
}

void DiagEngine::taskEntry(void* self) { static_cast<DiagEngine*>(self)->run(); }

void DiagEngine::finishJob(bool ok, const char* msg) {
  lock();
  st_.job.state = ok ? JobState::Done : JobState::Failed;
  strlcpy(st_.job.message, msg ? msg : "", sizeof st_.job.message);
  st_.job.finishedMs = millis();
  JobInfo* r = ringFind(st_.job.id);
  if (r) *r = st_.job;
  st_.session = session_->state();
  st_.lastError = session_->lastError();
  st_.txCount = session_->txCount();
  st_.errCount = session_->errorCount();
  unlock();
}

void DiagEngine::featMark(uint8_t f, bool ok) {
  if (f >= prof::F_COUNT) return;
  lock();
  st_.feat[f].state = ok ? 1 : 2;
  st_.feat[f].tMs = millis();
  unlock();
}

void DiagEngine::readHardware() {
  bool il = digitalRead(HFM_WRITE_EN_PIN) == (HFM_WRITE_EN_ACTIVE_LOW ? LOW : HIGH);
  float v = -1;
#if HFM_VBAT_ADC_PIN >= 0
  v = analogReadMilliVolts(HFM_VBAT_ADC_PIN) / 1000.0f * HFM_VBAT_DIVIDER * HFM_VBAT_CAL;
#endif
  lock();
  st_.interlock = il;
  st_.batteryV = v;
  if (ftdi_) {
    st_.parityErrors = ftdi_->parityErrors();
    st_.frameErrors = ftdi_->frameErrors();
    const bool present = ftdi_->attached();
    const uint32_t count = ftdi_->attachCount();
    if (count != st_.adapter.attachCount || (st_.adapter.present && !present)) {
      // Adapter re-attached / removed: the session is gone. Live Data stays
      // wanted and resumes after the automatic reconnection.
      session_->disconnect();
      guard_.cancel();
      if (guard_.state() == guard::State::Active) guard_.finish(false);
      st_.plan = guard_.plan();
      st_.session = session_->state();
      autoBackoffMs_ = HFM_AUTOCONNECT_MIN_MS;
      lastAutoMs_ = millis() - autoBackoffMs_;
    }
    st_.adapter.present = present;
    st_.adapter.configured = ftdi_->configured();
    st_.adapter.vid = ftdi_->vid();
    st_.adapter.pid = ftdi_->pid();
    st_.adapter.chip = ftdi_->chipName();
    st_.adapter.attachCount = count;
    st_.adapter.usbErrors = ftdi_->usbErrors();
    st_.adapter.breaks = ftdi_->breaksSeen();
    st_.adapter.overruns = ftdi_->overruns();
    st_.adapter.hostReady = ftdi_->hostReady();
    st_.adapter.usbDevices = ftdi_->usbDevices();
    st_.adapter.deviceEvents = ftdi_->deviceEvents();
    st_.adapter.seenVid = ftdi_->seenVid();
    st_.adapter.seenPid = ftdi_->seenPid();
    ftdi_->note(st_.adapter.note, sizeof st_.adapter.note);
    st_.adapter.haveWake = ftdi_->lastWake(st_.adapter.wake);
    st_.adapter.wakeNext = ftdi_->wakeProfile();
    st_.adapter.wakeLocked = ftdi_->wakeLocked();
    st_.adapter.wakeAttempts = ftdi_->wakeAttempts();
    FtdiLink::PortInfo pi = ftdi_->portInfo();
    st_.adapter.phyOtg = pi.phyOtg;
    st_.adapter.portPowered = pi.powered;
    st_.adapter.portConnected = pi.connected;
    st_.adapter.lineState = pi.lineState;
  } else if (kline_) {
    st_.parityErrors = kline_->parityErrors();
    st_.frameErrors = kline_->frameErrors();
    st_.adapter.present = true;
    st_.adapter.configured = true;
    st_.adapter.chip = "UART";
    st_.adapter.hostReady = true;
  }
  unlock();
}

uint8_t DiagEngine::vehicleCylinders() {
  int16_t vi = settings_->vehicleIdx;
  const prof::VehicleEntry* ve = vi >= 0 ? prof::vehicleByIndex((uint16_t)vi) : nullptr;
  return ve ? prof::cylindersForEngine(ve->engine) : 0;
}

void DiagEngine::reresolveProfile() {
  lock();
  prof::ResolveOptions o;
  o.experimentalReads = settings_->experimentalReads;
  o.engineerMode = settings_->engineerMode;
  o.cylinders = vehicleCylinders();
  prof::resolve(st_.ident, o, st_.profile);
  afterResolve();
  if (session_) applySettings();
  trace_.setEnabled(settings_->traceCapture);
  unlock();
}

bool DiagEngine::can(uint8_t f, char* msg, size_t n) {
  lock();
  const prof::Capability c = st_.profile.caps[f];
  unlock();
  if (!c.allowed) snprintf(msg, n, "%s", c.reason ? c.reason : "Недоступно для профиля");
  return c.allowed;
}

void DiagEngine::doConnect(char* msg, size_t n, bool& ok, uint8_t attempts) {
  connecting_ = true;
  updateLink();
  LinkStatus s = session_->connect(attempts);
  connecting_ = false;
  ok = s == LinkStatus::Ok;
  lock();
  st_.session = session_->state();
  st_.connectedAtMs = session_->connectedAtMs();
  st_.lastError = s;
  unlock();
  if (ok) {
    everConnected_ = true;
    return;
  }
  if (s == LinkStatus::LinkDown) {
    adapterMissing(msg, n);
    return;
  }
  // Local echo alone cannot verify the vehicle wiring or ECU power.
  uint8_t echo = session_->lastSyncEcho();
  kwake::Diag wd;
  bool haveWd = ftdi_ && ftdi_->lastWake(wd);
  bool sawEcho = haveWd ? wd.echo : echo == 0xAA;
  bool sawBreak = haveWd && wd.breakOnRx;
  if (!sawEcho && !sawBreak && haveWd)
    snprintf(msg, n, "Линия K-Line молчит: адаптер не принял свой BREAK и эхо AA — проверьте +12 В адаптера и провод K-Line");
  else if (!sawEcho)
    snprintf(msg, n, "Нет эха AA от адаптера — проверьте провод K-Line и массу");
  else
    snprintf(msg, n, "ЭБУ не ответил на пробуждение (эхо адаптера есть, ответа 55 нет) — проверьте зажигание");
}

static void rememberEcu(Settings* st, const char* fp, uint8_t set, uint8_t clear) {
  if (strcmp(st->ecuFp, fp) != 0) {
    strlcpy(st->ecuFp, fp, sizeof st->ecuFp);
    st->ecuFlags = 0;
  }
  uint8_t f = (uint8_t)((st->ecuFlags | set) & ~clear);
  if (f == st->ecuFlags) return;  // already stored
  st->ecuFlags = f;
  st->save();
}

void DiagEngine::doIdentify(char* msg, size_t n, bool& ok) {
  uint8_t raw[28];
  LinkStatus s = session_->readIdent(raw, 28);
  ok = s == LinkStatus::Ok;
  EcuIdent id;
  parseIdent(raw, ok ? 28 : 0, id);
  if (ok) {
    // F0:26 / F0:27 (ROM page, descriptor address) - read by the factory tester
    // in the same telegram. Optional; an ECU that does not answer them is
    // remembered (NVS) and the read session is resumed without a new BREAK.
    bool skip = strcmp(settings_->ecuFp, id.fingerprint) == 0 && (settings_->ecuFlags & ECU_F_NO_F0EXT);
    if (!skip) {
      uint8_t b26 = 0, b27 = 0;
      LinkStatus e = session_->prefix(0xF0);
      if (e == LinkStatus::Ok) e = session_->selector(0x26, b26);
      if (e == LinkStatus::Ok) e = session_->prefix(0xF0);
      if (e == LinkStatus::Ok) e = session_->selector(0x27, b27);
      if (e == LinkStatus::Ok) {
        id.extPresent = true;
        id.romPage = b26;
        id.descAddr = b27;
      } else {
        rememberEcu(settings_, id.fingerprint, ECU_F_NO_F0EXT, 0);
        if (!recoverRead()) {
          char tmp[200];
          bool ok2 = false;
          doConnect(tmp, sizeof tmp, ok2, 2);
        }
      }
    }
  }
  lock();
  if (ok) {
    if (strcmp(st_.ident.fingerprint, id.fingerprint) != 0) {
      // Another ECU: results of the previous one are not valid for it.
      st_.dtc = DtcView();
      st_.adapt = svc::Adaptations();
      st_.coding = svc::Coding();
      st_.vin = svc::Vin();
      st_.live = live::Decoded();
      st_.snap = live::Snapshot();
      for (uint8_t f = 0; f < prof::F_COUNT; f++) st_.feat[f] = FeatRun();
      extDisabled_ = false;
    }
    st_.ident = id;
  }
  prof::ResolveOptions o;
  o.experimentalReads = settings_->experimentalReads;
  o.engineerMode = settings_->engineerMode;
  o.cylinders = vehicleCylinders();
  prof::resolve(st_.ident, o, st_.profile);
  afterResolve();
  unlock();
  featMark(prof::F_IDENT, ok);
  if (!ok) snprintf(msg, n, "Ошибка чтения идентификации F0 (%s)", linkStatusCode(s));
  else snprintf(msg, n, "ЭБУ %s · %s", id.partDisplay, id.identifier);
}

void DiagEngine::afterResolve() {
  // Live-data address plan: by vendor; unknown vendor -> remembered probe
  // result, otherwise the documented Bosch plan first.
  st_.livePlanVdo = st_.profile.vdoUnits;
  if (st_.profile.vdoCandidate && strcmp(settings_->ecuFp, st_.ident.fingerprint) == 0 &&
      (settings_->ecuFlags & ECU_F_PLAN_KNOWN))
    st_.livePlanVdo = (settings_->ecuFlags & ECU_F_PLAN_VDO) != 0 && settings_->experimentalReads;
}

static uint16_t decodeAll(const svc::FaultMemory& fm, const prof::EcuProfile& p, dtc::Decoded* out, uint16_t max);

// Fault memory F1 (read only). A partial readout is never shown as "no faults".
bool DiagEngine::readDtc(char* msg, size_t n) {
  bool ext = st_.profile.caps[prof::F_DTC_EXTENDED].allowed;
  svc::FaultMemory fm;
  LinkStatus s = svc::readFaultMemory(*session_, fm, ext);
  bool ok = fm.valid;
  if (ok) {
    lock();
    st_.dtc.fm = fm;
    st_.dtc.valid = true;
    st_.dtc.tMs = millis();
    st_.dtc.count = decodeAll(fm, st_.profile, st_.dtc.items, 48);
    st_.dtc.inferred = dtc::inferBankMapping(st_.dtc.items, st_.dtc.count);
    if (st_.dtc.count) snprintf(msg, n, "Память неисправностей прочитана: записей %u", st_.dtc.count);
    else snprintf(msg, n, "Память неисправностей прочитана: записей нет");
    unlock();
    if (fm.extValid) featMark(prof::F_DTC_EXTENDED, true);
  } else {
    snprintf(msg, n, "Ошибка чтения памяти неисправностей (%s)", linkStatusCode(s));
    recoverRead();
  }
  featMark(prof::F_DTC_READ, ok);
  return ok;
}

// Optional read of the fault memory right after a connection (setting), so the
// result is known (and signalled) without another tap. Not after an automatic
// recovery when a readout of this ECU already exists.
void DiagEngine::dtcAfterConnect(bool recovering) {
  if (!settings_->dtcOnConnect || !session_->ready()) return;
  lock();
  bool allowed = st_.profile.caps[prof::F_DTC_READ].allowed;
  bool have = st_.dtc.valid;
  unlock();
  if (!allowed || (recovering && have)) return;
  char m[160];
  readDtc(m, sizeof m);
}

bool DiagEngine::recoverRead() {
  if (!session_->resyncAllowed()) return false;
  bool ok = session_->resync() == LinkStatus::Ok;
  lock();
  if (ok) st_.resyncs++;
  st_.session = session_->state();
  st_.lastError = session_->lastError();
  unlock();
  return ok;
}

bool DiagEngine::ensureConnected(char* msg, size_t n) {
  if (session_->ready()) return true;
  if (!adapterPresent()) {
    adapterMissing(msg, n);
    return false;
  }
  if (recoverRead()) return true;
  bool ok;
  doConnect(msg, n, ok, 2);
  if (ok) doIdentify(msg, n, ok);
  return ok && session_->ready();
}

void DiagEngine::fillFacts(guard::Facts& f) {
  uint32_t now = millis();
  f.sessionReady = session_->ready();
  f.interlockClosed = st_.interlock;
  const live::Value& rpm = st_.live.v[live::P_RPM];
  const live::Value& ect = st_.live.v[live::P_ECT];
  f.liveAgeMs = st_.live.tMs ? now - st_.live.tMs : 0xFFFFFFFF;
  if (rpm.hasValue) {
    f.rpm = rpm.v;
    f.engineStopped = rpm.raw0 == 0 ? 1 : 0;
  }
  if (ect.hasValue && ect.q == live::Quality::Valid) f.ect = ect.v;
  f.batteryV = st_.batteryV;
  f.batteryManualConfirm = settings_->batteryManualConfirm;
  if (st_.dtc.valid) {
    f.faultMemAgeMs = now - st_.dtc.tMs;
    f.faultMemNonZero = false;
    for (int i = 0; i < 16; i++)
      if (st_.dtc.fm.bankA[i] || st_.dtc.fm.bankB[i]) f.faultMemNonZero = true;
  }
  if (st_.adapt.valid) f.adaptAgeMs = now - st_.adapt.tMs;
}

static uint16_t decodeAll(const svc::FaultMemory& fm, const prof::EcuProfile& p, dtc::Decoded* out, uint16_t max) {
  uint16_t n = dtc::decodeBank(fm.bankA, 0x00, p.variant, out, max);
  n += dtc::decodeBank(fm.bankB, 0x10, p.variant, out + n, (uint16_t)(max - n));
  return n;
}

void DiagEngine::execute(Job j, uint32_t arg, const char* text) {
  char msg[256] = "OK";
  bool ok = true;
  lock();
  st_.job.state = JobState::Running;
  st_.job.startedMs = millis();
  JobInfo* rj = ringFind(st_.job.id);
  if (rj) *rj = st_.job;
  unlock();

  switch (j) {
    case Job::Connect:
      userDisconnected_ = false;
      autoBackoffMs_ = HFM_AUTOCONNECT_MIN_MS;
      if (!adapterPresent()) {
        ok = false;
        adapterMissing(msg, sizeof msg);
        break;
      }
      // A running session is kept: a new BREAK into it would not be answered.
      if (session_->ready()) {
        snprintf(msg, sizeof msg, "ЭБУ уже на связи");
        break;
      }
      if (recoverRead()) {
        snprintf(msg, sizeof msg, "Связь с ЭБУ восстановлена");
        break;
      }
      doConnect(msg, sizeof msg, ok, 2);
      if (ok) doIdentify(msg, sizeof msg, ok);
      if (ok && !session_->ready()) {
        ok = false;
        snprintf(msg, sizeof msg, "ЭБУ ответил, но связь прервалась после идентификации — восстанавливается автоматически");
      }
      if (ok) {
        char keep[256];
        strlcpy(keep, msg, sizeof keep);
        dtcAfterConnect(false);
        strlcpy(msg, keep, sizeof msg);
      }
      break;
    case Job::Disconnect:
      userDisconnected_ = true;  // no auto-reconnect until "Connect" is pressed
      everConnected_ = false;
      lock();
      st_.liveActive = false;
      unlock();
      session_->disconnect();
      snprintf(msg, sizeof msg, "Отключено (автоподключение приостановлено до нажатия «Подключить»)");
      break;
    case Job::Identify:
      if (!ensureConnected(msg, sizeof msg)) { ok = false; break; }
      doIdentify(msg, sizeof msg, ok);
      if (ok && !session_->ready()) ensureConnected(msg, sizeof msg);
      break;
    case Job::ReadDtc: {
      if (!ensureConnected(msg, sizeof msg)) { ok = false; break; }
      if (!can(prof::F_DTC_READ, msg, sizeof msg)) { ok = false; break; }
      ok = readDtc(msg, sizeof msg);
      break;
    }
    case Job::ReadAdapt: {
      if (!ensureConnected(msg, sizeof msg) || !can(prof::F_ADAPT_READ, msg, sizeof msg)) { ok = false; break; }
      svc::Adaptations a;
      LinkStatus s = svc::readAdaptations(*session_, a);
      ok = a.valid;
      lock();
      if (ok) st_.adapt = a;
      unlock();
      if (!ok) snprintf(msg, sizeof msg, "Ошибка чтения адаптаций 24* 90/92/93 (%s)", linkStatusCode(s));
      else snprintf(msg, sizeof msg, "Самоадаптации прочитаны");
      featMark(prof::F_ADAPT_READ, ok);
      break;
    }
    case Job::ReadCoding: {
      if (!ensureConnected(msg, sizeof msg) || !can(prof::F_CODING_READ, msg, sizeof msg)) { ok = false; break; }
      svc::Coding c;
      LinkStatus s = svc::readCoding(*session_, c);
      ok = c.valid;
      lock();
      if (ok) st_.coding = c;
      unlock();
      if (!ok) snprintf(msg, sizeof msg, "Ошибка чтения кодировки 27* 02 24* F0 (%s)", linkStatusCode(s));
      else snprintf(msg, sizeof msg, "Кодировка прочитана");
      featMark(prof::F_CODING_READ, ok);
      break;
    }
    case Job::ReadVin: {
      if (!ensureConnected(msg, sizeof msg) || !can(prof::F_VIN_READ, msg, sizeof msg)) { ok = false; break; }
      svc::Vin v;
      LinkStatus s = svc::readVin(*session_, v);
      ok = s == LinkStatus::Ok;
      lock();
      st_.vin = v;
      unlock();
      snprintf(msg, sizeof msg, ok ? (v.valid ? "VIN прочитан" : "VIN в ЭБУ не записан (00/FF)") : "Ошибка F9 (%s)",
               linkStatusCode(s));
      featMark(prof::F_VIN_READ, ok);
      break;
    }
    case Job::LiveStart:
      // While the link is recovering the request is kept: polling starts by
      // itself after the automatic reconnection.
      if (!session_->ready() && settings_->autoConnect && !userDisconnected_ && adapterPresent()) {
        bool identified;
        lock();
        identified = st_.ident.present;
        unlock();
        if (identified && can(prof::F_LIVE_NATIVE, msg, sizeof msg)) {
          lock();
          st_.liveActive = true;
          unlock();
          snprintf(msg, sizeof msg, "Опрос начнётся после восстановления связи с ЭБУ");
          break;
        }
      }
      if (!ensureConnected(msg, sizeof msg) || !can(prof::F_LIVE_NATIVE, msg, sizeof msg)) { ok = false; break; }
      lock();
      st_.liveActive = true;
      st_.liveCycles = 0;
      unlock();
      snprintf(msg, sizeof msg, "Опрос Live Data запущен");
      break;
    case Job::LiveStop:
      lock();
      st_.liveActive = false;
      unlock();
      snprintf(msg, sizeof msg, "Опрос остановлен");
      break;
    case Job::SvcPrepare: {
      guard::Op op = (guard::Op)arg;
      if (!ensureConnected(msg, sizeof msg)) { ok = false; break; }
      // Fresh read of engine state immediately before planning.
      if (st_.profile.caps[prof::F_LIVE_NATIVE].allowed) {
        live::Snapshot snap = st_.snap;
        bool vdo = st_.livePlanVdo;
        if (live::pollNative(*session_, snap, vdo) == LinkStatus::Ok) {
          int16_t vi = settings_->vehicleIdx;
          const prof::VehicleEntry* ve = vi >= 0 ? prof::vehicleByIndex((uint16_t)vi) : nullptr;
          lock();
          st_.snap = snap;
          prevLive_ = st_.live;
          live::decode(snap, ve ? ve->norms : prof::NormGroup::None, vdo, &prevLive_, st_.live,
                       st_.profile.variant.diagRaw);
          unlock();
        } else {
          recoverRead();
        }
      }
      readHardware();
      lock();
      guard::Facts f;
      fillFacts(f);
      guard::Plan plan;
      ok = guard_.prepare(op, st_.profile, st_.ident, f, millis(), esp_random() | 1, plan);
      st_.plan = plan;
      unlock();
      snprintf(msg, sizeof msg, ok ? "План готов: подтвердите в течение 20 с" : "Условия не выполнены");
      break;
    }
    case Job::SvcExecute: {
      guard::Op op = (guard::Op)(arg & 0xFF);
      readHardware();
      lock();
      guard::Facts f;
      fillFacts(f);
      uint32_t token = st_.plan.token;  // token is echoed by the UI in text
      (void)token;
      uint32_t given = strtoul(text, nullptr, 16);
      const char* phrase = strchr(text, '|');
      guard::AuthResult ar = guard_.authorize(op, given, phrase ? phrase + 1 : "", st_.profile, st_.ident, f, millis());
      unlock();
      if (ar != guard::AuthResult::Ok) {
        ok = false;
        snprintf(msg, sizeof msg, "Отказано: %s", guard::authCode(ar));
        break;
      }
      lock();
      st_.liveActive = false;
      guard_.begin(op);
      unlock();
      session_->armWrite(esp_random() | 1);
      LinkStatus s = LinkStatus::Ok;
      char res[160];
      if (op == guard::Op::DtcClear) {
        bool verified = false;
        svc::FaultMemory after;
        s = svc::dtcClear(*session_, verified, after);
        ok = s == LinkStatus::Ok;
        lock();
        if (after.valid) {
          st_.dtc.fm = after;
          st_.dtc.valid = true;
          st_.dtc.tMs = millis();
          st_.dtc.count = decodeAll(after, st_.profile, st_.dtc.items, 48);
          st_.dtc.inferred = dtc::inferBankMapping(st_.dtc.items, st_.dtc.count);
        }
        unlock();
        snprintf(res, sizeof res, ok ? (verified ? "Стирание выполнено, повторное чтение: все 32 байта = 00"
                                                 : "Команды приняты, но память не пуста — неисправность активна?")
                                     : "ОШИБКА при стирании (%s). Состояние неизвестно — запись заблокирована.",
                 linkStatusCode(s));
        featMark(prof::F_DTC_CLEAR, ok && verified);
        guard_.finish(ok);
      } else if (op == guard::Op::AdaptReset) {
        uint8_t r[10];
        s = svc::adaptationReset(*session_, r);
        ok = s == LinkStatus::Ok;
        snprintf(res, sizeof res, ok ? "Команды сброса приняты. Обязателен цикл зажигания ВЫКЛ/ВКЛ; результат не "
                                       "подтверждён ЭБУ — прочитайте адаптации повторно."
                                     : "ОШИБКА сброса (%s). Состояние неизвестно — запись заблокирована.",
                 linkStatusCode(s));
        if (!ok) featMark(prof::F_ADAPT_RESET, false);  // success is confirmed only by a new readout
        guard_.finish(ok);
      } else {
        bool sonde = false;
        s = svc::purgeEnter(*session_, sonde);
        ok = s == LinkStatus::Ok;
        lock();
        st_.purgeSonde = sonde;
        st_.purgeOn = false;
        st_.purgeActiveSinceMs = millis();
        unlock();
        featMark(prof::F_PURGE_TEST, ok);
        if (!ok) {
          svc::purgeRestore(*session_, sonde);
          guard_.finish(false);
          snprintf(res, sizeof res, "ОШИБКА входа в режим управления (%s). Выключите зажигание.", linkStatusCode(s));
        } else {
          snprintf(res, sizeof res, "Режим управления Y58/1 активен. Лимит ВКЛ — 30 с, затем восстановление.");
        }
      }
      session_->disarmWrite();
      lock();
      strlcpy(st_.lastServiceResult, res, sizeof st_.lastServiceResult);
      st_.plan = guard::Plan();
      unlock();
      snprintf(msg, sizeof msg, "%s", ok ? "Выполнено" : "Ошибка");
      break;
    }
    case Job::PurgeOn:
    case Job::PurgeOff: {
      if (guard_.state() != guard::State::Active) { ok = false; snprintf(msg, sizeof msg, "Тест не активен"); break; }
      session_->armWrite(1);
      LinkStatus s = svc::purgeSet(*session_, j == Job::PurgeOn);
      session_->disarmWrite();
      ok = s == LinkStatus::Ok;
      lock();
      if (ok) {
        st_.purgeOn = j == Job::PurgeOn;
        st_.purgeOnSinceMs = millis();
      }
      unlock();
      if (!ok) {
        guard_.finish(false);
        snprintf(msg, sizeof msg, "Ошибка управления (%s) — выключите зажигание", linkStatusCode(s));
      }
      break;
    }
    case Job::Stop:
      lock();
      st_.liveActive = false;
      unlock();
      if (guard_.state() == guard::State::Active) {
        guard_.restoring();
        session_->armWrite(1);
        LinkStatus s = svc::purgeRestore(*session_, st_.purgeSonde);
        ok = s == LinkStatus::Ok;
        guard_.finish(ok);
        lock();
        st_.purgeOn = false;
        snprintf(st_.lastServiceResult, sizeof st_.lastServiceResult,
                 ok ? "Восстановление выполнено (D0 0B, D1 00, D2 00, D3 40, D4 00)"
                    : "ВОССТАНОВЛЕНИЕ НЕ ПОДТВЕРЖДЕНО — выключите зажигание!");
        unlock();
      }
      snprintf(msg, sizeof msg, "STOP");
      break;
    case Job::AckFault:
      guard_.acknowledgeFault();
      snprintf(msg, sizeof msg, "Ошибка квитирована: переподключитесь и идентифицируйте ЭБУ");
      break;
    case Job::ImpulseRead: {
      if (!settings_->impulseEnabled) { ok = false; snprintf(msg, sizeof msg, "Включите импульсный режим в настройках"); break; }
      if (!kline_) { ok = false; snprintf(msg, sizeof msg, "Импульсный режим требует UART-трансивера; с FTDI-адаптером недоступен"); break; }
      lock();
      st_.liveActive = false;
      unlock();
      session_->disconnect();
      if (!kline_->open()) { ok = false; snprintf(msg, sizeof msg, "UART недоступен"); break; }
      ImpulseConfig cfg;
      cfg.pressMs = settings_->impPressMs;
      cfg.endGapMs = settings_->impEndGapMs;
      ImpulseView v;
      v.edgeCount = kline_->impulseCapture(cfg, v.edges, 96);
      v.result = decodeImpulses(v.edges, v.edgeCount, cfg);
      v.valid = true;
      v.tMs = millis();
      lock();
      ImpulseView& cur = st_.impulse;
      uint8_t codes[16];
      uint8_t nc = cur.codeCount;
      memcpy(codes, cur.codes, sizeof codes);
      if (v.result.complete && nc < 16) codes[nc++] = v.result.pulses;
      cur = v;
      memcpy(cur.codes, codes, sizeof codes);
      cur.codeCount = nc;
      unlock();
      kline_->close();
      snprintf(msg, sizeof msg, v.result.complete ? "Код N = %u" : "Код не распознан (импульсов: %u)", v.result.pulses);
      ok = v.result.complete;
      featMark(prof::F_IMPULSE_READ, ok);
      break;
    }
    default:
      break;
  }
  finishJob(ok, msg);
}

void DiagEngine::livePoll() {
  uint32_t now = millis();
  if (now - lastLiveMs_ < HFM_LIVE_MIN_CYCLE_MS) return;
  lastLiveMs_ = now;
  live::Snapshot snap;
  lock();
  snap = st_.snap;
  bool vdo = st_.livePlanVdo;
  bool ext = st_.profile.caps[prof::F_LIVE_EXTENDED].allowed && !vdo && !extDisabled_;
  bool probe = st_.profile.vdoCandidate && !(strcmp(settings_->ecuFp, st_.ident.fingerprint) == 0 &&
                                             (settings_->ecuFlags & ECU_F_PLAN_KNOWN));
  int16_t vi = settings_->vehicleIdx;
  uint8_t diagRaw = st_.profile.variant.diagRaw;
  char fp[24];
  strlcpy(fp, st_.ident.fingerprint, sizeof fp);
  unlock();
  // VDO plan: VDO address plan (experimental prefixes); Bosch extended
  // addresses do not apply to it.
  LinkStatus s = live::pollNative(*session_, snap, vdo);
  if (s == LinkStatus::Ok && ext) {
    // Extended reads are optional (opt-in): two failures in a row switch them
    // off for this ECU session instead of dropping the link every cycle.
    if (live::pollExtended(*session_, snap) != LinkStatus::Ok) {
      if (++extFails_ >= 2) extDisabled_ = true;
      recoverRead();
    } else {
      extFails_ = 0;
    }
  }
  prof::NormGroup ng = prof::NormGroup::None;
  const prof::VehicleEntry* ve = vi >= 0 ? prof::vehicleByIndex((uint16_t)vi) : nullptr;
  if (ve) ng = ve->norms;
  lock();
  st_.snap = snap;
  if (s == LinkStatus::Ok) {
    // Only complete cycles replace the shown values; a failed cycle keeps the
    // last ones (their age tells the UI they are not live).
    prevLive_ = st_.live;
    live::decode(snap, ng, vdo, &prevLive_, st_.live, diagRaw);
    st_.liveCycles++;
    st_.feat[prof::F_LIVE_NATIVE].state = 1;
    st_.feat[prof::F_LIVE_NATIVE].tMs = millis();
  }
  st_.session = session_->state();
  st_.txCount = session_->txCount();
  st_.errCount = session_->errorCount();
  unlock();
  if (s == LinkStatus::Ok) {
    if (probe) rememberEcu(settings_, fp, (uint8_t)(ECU_F_PLAN_KNOWN | (vdo ? ECU_F_PLAN_VDO : 0)), vdo ? 0 : ECU_F_PLAN_VDO);
    if (st_.recording) recordRow();
    return;
  }
  if (s == LinkStatus::NotAllowed) {
    // The policy refuses this plan now (e.g. experimental reads switched
    // off): nothing was sent, so stop polling instead of looping silently.
    lock();
    st_.liveActive = false;
    snprintf(st_.autoMsg, sizeof st_.autoMsg, "Опрос Live Data остановлен: адреса этого ЭБУ требуют «Экспериментальное чтение»");
    unlock();
    return;
  }
  const bool firstGroupFailed = vdo ? !snap.vdoOk[0] : !snap.groupOk[0];
  if (probe && firstGroupFailed) {
    // Unknown vendor: the first group of this plan is not answered - try the
    // other documented plan (VDO plan only with the experimental opt-in).
    lock();
    if (!vdo && settings_->experimentalReads) st_.livePlanVdo = true;
    else if (vdo) st_.livePlanVdo = false;
    unlock();
  }
  // Read-only: resume the session without a new BREAK; otherwise
  // maintainLink() reconnects and polling continues afterwards.
  recoverRead();
}

void DiagEngine::keepAlive() {
  if (!settings_->keepAlive || !session_->ready()) return;
  uint32_t now = millis();
  if (now - session_->lastActivityMs() < HFM_KEEPALIVE_MS) return;
  uint8_t b;
  // Harmless read-only command (F0:00) instead of "10 00": 0x10 is
  // also an element of the DTC clear sequence (docs/DECISIONS.md D-006).
  LinkStatus s = session_->prefix(0xF0);
  if (s == LinkStatus::Ok) s = session_->selector(0x00, b);
  if (s != LinkStatus::Ok) recoverRead();
  lock();
  st_.session = session_->state();
  st_.lastError = session_->lastError();
  unlock();
}

void DiagEngine::purgeWatchdog() {
  if (guard_.state() == guard::State::Prepared && st_.plan.ok && (int32_t)(millis() - st_.plan.expiresMs) > 0) {
    guard_.cancel();  // confirmation window (20 s) passed without execution
    lock();
    st_.plan = guard_.plan();
    unlock();
    return;
  }
  if (guard_.state() != guard::State::Active) return;
  uint32_t now = millis();
  bool timeoutOn = st_.purgeOn && now - st_.purgeOnSinceMs > 30000;
  bool timeoutTest = now - st_.purgeActiveSinceMs > 180000;
  if (timeoutOn || timeoutTest) execute(Job::Stop, 0, nullptr);
}

bool DiagEngine::startRecording() {
  lock();
  if (st_.recording) { unlock(); return true; }
  LittleFS.mkdir("/rec");
  snprintf(st_.recordFile, sizeof st_.recordFile, "/rec/%s_%lu.csv", sessionId_, (unsigned long)(millis() / 1000));
  File f = LittleFS.open(st_.recordFile, "w");
  bool ok = (bool)f;
  if (ok) {
    f.print("t_ms");
    for (uint8_t p = 0; p < live::P_COUNT; p++) {
      f.print(',');
      f.print(live::param(p).key);
    }
    f.print('\n');
    f.close();
    st_.recording = true;
    st_.recordRows = 0;
    recBuf_.clear();
  }
  unlock();
  return ok;
}

void DiagEngine::stopRecording() {
  lock();
  if (st_.recording && !recBuf_.empty()) {
    File f = LittleFS.open(st_.recordFile, "a");
    if (f) { f.write((const uint8_t*)recBuf_.data(), recBuf_.size()); f.close(); }
    recBuf_.clear();
  }
  st_.recording = false;
  unlock();
}

void DiagEngine::recordRow() {
  lock();
  char t[24];
  snprintf(t, sizeof t, "%lu", (unsigned long)st_.live.tMs);
  recBuf_ += t;
  for (uint8_t p = 0; p < live::P_COUNT; p++) {
    const live::Value& v = st_.live.v[p];
    recBuf_ += ',';
    if (v.hasValue) {
      snprintf(t, sizeof t, "%.*f", live::param(p).decimals, v.v);
      recBuf_ += t;
    }
  }
  recBuf_ += '\n';
  st_.recordRows++;
  unlock();
}

void DiagEngine::adapterMissing(char* msg, size_t n) {
  char why[160] = {0};
  if (ftdi_) ftdi_->note(why, sizeof why);
  if (why[0])
    snprintf(msg, n, "Адаптер K-Line не обнаружен: %s", why);
  else
    strlcpy(msg, "Адаптер K-Line не обнаружен (USB): ожидание определения устройства", n);
}

bool DiagEngine::adapterPresent() {
  if (ftdi_) return ftdi_->attached();
  return kline_ != nullptr;
}

void DiagEngine::maintainLink() {
  if (session_->ready() || !settings_->autoConnect || userDisconnected_) return;
  if (guard_.state() == guard::State::Prepared) {
    // A service plan prepared on the lost session is void (it would also block
    // the recovery forever: the plan only expires when it is executed).
    guard_.cancel();
    lock();
    st_.plan = guard_.plan();
    unlock();
  }
  if (guard_.state() != guard::State::Idle && guard_.state() != guard::State::FaultStop) return;
  if (!adapterPresent()) {
    char why[256];
    adapterMissing(why, sizeof why);
    lock();
    strlcpy(st_.autoMsg, why, sizeof st_.autoMsg);
    unlock();
    return;
  }
  // 1. A read session lost by a byte error: resume without a new BREAK.
  if (recoverRead()) {
    lock();
    st_.recoveries++;
    st_.recoverAttempts = 0;
    snprintf(st_.autoMsg, sizeof st_.autoMsg, "Связь восстановлена автоматически");
    unlock();
    return;
  }
  // 2. Line silence until the ECU dropped its session (not blocking: jobs
  //    submitted meanwhile are executed).
  uint32_t silence = session_->silenceNeededMs();
  if (silence) return;
  uint32_t now = millis();
  if (now - lastAutoMs_ < autoBackoffMs_) return;
  lastAutoMs_ = now;
  const bool recovering = everConnected_;
  lock();
  if (recovering) st_.recoverAttempts++;
  unlock();
  char msg[256];
  bool ok = false;
  doConnect(msg, sizeof msg, ok, 1);
  if (ok) doIdentify(msg, sizeof msg, ok);
  if (ok && session_->ready()) dtcAfterConnect(recovering);
  lock();
  st_.autoAttempts++;
  if (ok && session_->ready()) {
    autoBackoffMs_ = HFM_AUTOCONNECT_MIN_MS;
    if (recovering) st_.recoveries++;
    st_.recoverAttempts = 0;
    snprintf(st_.autoMsg, sizeof st_.autoMsg, recovering ? "Связь восстановлена автоматически" : "Подключено автоматически");
  } else {
    autoBackoffMs_ = autoBackoffMs_ * 2 > HFM_AUTOCONNECT_MAX_MS ? HFM_AUTOCONNECT_MAX_MS : autoBackoffMs_ * 2;
    strlcpy(st_.autoMsg, msg, sizeof st_.autoMsg);
  }
  unlock();
}

void DiagEngine::updateLink() {
  lock();
  SessionState ss = session_->state();
  st_.session = ss;
  st_.lastError = session_->lastError();
  st_.resyncs = session_->resyncCount();
  LinkPhase ph;
  char m[200] = {0};
  if (ftdi_ && !st_.adapter.present) {
    ph = LinkPhase::NoAdapter;
    snprintf(m, sizeof m, "Адаптер K-Line не обнаружен%s%s", st_.adapter.note[0] ? ": " : "", st_.adapter.note);
  } else if (ss == SessionState::Ready) {
    ph = LinkPhase::Ready;
  } else if (guard_.state() == guard::State::FaultStop) {
    ph = LinkPhase::Error;
    snprintf(m, sizeof m, "FAULT_STOP: сервисная операция завершилась ошибкой — квитируйте и проверьте автомобиль");
  } else if (connecting_) {
    ph = everConnected_ ? LinkPhase::Recovering : LinkPhase::Connecting;
    snprintf(m, sizeof m, everConnected_ ? "Восстановление связи с ЭБУ…" : "Подключение к ЭБУ…");
  } else if (userDisconnected_) {
    ph = LinkPhase::Paused;
    snprintf(m, sizeof m, "Отключено. Автоподключение приостановлено до нажатия «Подключить»");
  } else if (!settings_->autoConnect) {
    ph = ss == SessionState::Error ? LinkPhase::Error : LinkPhase::Idle;
    if (ss == SessionState::Error)
      snprintf(m, sizeof m, "Связь с ЭБУ прервана (%s). Автоподключение выключено — нажмите «Подключить»",
               linkStatusCode(session_->lastError()));
    else
      snprintf(m, sizeof m, "Не подключено");
  } else {
    ph = everConnected_ ? LinkPhase::Recovering : LinkPhase::Connecting;
    if (everConnected_)
      snprintf(m, sizeof m, "Связь с ЭБУ прервалась — восстанавливаю автоматически%s%s",
               st_.autoMsg[0] ? ": " : "", st_.autoMsg);
    else
      snprintf(m, sizeof m, "%s", st_.autoMsg[0] ? st_.autoMsg : "Поиск ЭБУ…");
  }
  st_.link = ph;
  strlcpy(st_.linkMsg, m, sizeof st_.linkMsg);
  st_.faultStop = guard_.state() == guard::State::FaultStop;
  st_.serviceActive = guard_.state() == guard::State::Executing || guard_.state() == guard::State::Active ||
                      guard_.state() == guard::State::Restoring;
  unlock();
}

void DiagEngine::persistLearned() {
  uint32_t now = millis();
  if (now - lastPersistMs_ < 2000) return;
  lastPersistMs_ = now;
  kwake::Profile g;
  if (ftdi_ && ftdi_->goodProfile(g) &&
      (g.breakMs != settings_->wakeLearnedBreak || g.modem != settings_->wakeLearnedModem)) {
    settings_->wakeLearnedBreak = g.breakMs;
    settings_->wakeLearnedModem = g.modem;
    settings_->save();
  }
}

void DiagEngine::run() {
  for (;;) {
    readHardware();
    QJob qj;
    bool have = false;
    lock();
    if (qCount_) {
      qj = q_[0];
      memmove(&q_[0], &q_[1], (qCount_ - 1) * sizeof(QJob));
      qCount_--;
      have = true;
      st_.job = JobInfo();
      st_.job.id = qj.id;
      st_.job.job = qj.job;
      st_.job.state = JobState::Queued;
    }
    bool liveOn = st_.liveActive;
    unlock();
    if (have) {
      execute(qj.job, qj.arg, qj.text);
    } else if (session_->ready()) {
      if (liveOn) livePoll();
      else keepAlive();
    } else {
      maintainLink();
    }
    purgeWatchdog();
    persistLearned();
    updateLink();
    uint32_t now = millis();
    if (st_.recording && now - lastFlushMs_ > HFM_RECORD_FLUSH_MS) {
      lastFlushMs_ = now;
      lock();
      if (!recBuf_.empty()) {
        File f = LittleFS.open(st_.recordFile, "a");
        if (f) { f.write((const uint8_t*)recBuf_.data(), recBuf_.size()); f.close(); }
        recBuf_.clear();
      }
      unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(liveOn && session_->ready() ? 1 : 10));
  }
}
#endif
