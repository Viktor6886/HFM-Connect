#include "ServiceGuard.h"

#include <stdio.h>
#include <string.h>

namespace guard {

const char* opCode(Op op) {
  switch (op) {
    case Op::DtcClear: return "dtc_clear";
    case Op::AdaptReset: return "adapt_reset";
    case Op::PurgeTest: return "purge_test";
    case Op::ImpulseClear: return "impulse_clear";
    default: return "none";
  }
}

const char* opName(Op op) {
  switch (op) {
    case Op::DtcClear: return "Стирание памяти неисправностей";
    case Op::AdaptReset: return "Сброс самоадаптаций смеси";
    case Op::PurgeTest: return "Проверка клапана продувки адсорбера Y58/1";
    case Op::ImpulseClear: return "Импульсное стирание кода";
    default: return "—";
  }
}

uint8_t opFeature(Op op) {
  switch (op) {
    case Op::DtcClear: return prof::F_DTC_CLEAR;
    case Op::AdaptReset: return prof::F_ADAPT_RESET;
    case Op::ImpulseClear: return prof::F_IMPULSE_CLEAR;
    default: return prof::F_PURGE_TEST;
  }
}

const char* stateCode(State s) {
  switch (s) {
    case State::Idle: return "IDLE";
    case State::Prepared: return "PREPARED";
    case State::Executing: return "EXECUTING";
    case State::Active: return "ACTIVE";
    case State::Restoring: return "RESTORING";
    default: return "FAULT_STOP";
  }
}

const char* authCode(AuthResult r) {
  switch (r) {
    case AuthResult::Ok: return "OK";
    case AuthResult::NoPlan: return "NO_PLAN";
    case AuthResult::Expired: return "TOKEN_EXPIRED";
    case AuthResult::BadToken: return "BAD_TOKEN";
    case AuthResult::BadPassword: return "BAD_PASSWORD";
    case AuthResult::PasswordLocked: return "PASSWORD_LOCKED";
    case AuthResult::IdentChanged: return "IDENT_CHANGED";
    case AuthResult::ChecksFailed: return "PRECONDITIONS_FAILED";
    case AuthResult::Locked: return "LOCKED_UNRESOLVED";
    default: return "INTERLOCK_OPEN";
  }
}

static void add(Plan& p, const char* id, const char* text, int8_t r) {
  if (p.nChecks >= sizeof(p.checks) / sizeof(p.checks[0])) return;
  p.checks[p.nChecks++] = Check{id, text, r};
}

void ServiceGuard::evaluate(Op op, const prof::EcuProfile& pr, const EcuIdent& id, const Facts& f, Plan& p) {
  p = Plan();
  p.op = op;
  const prof::Capability& cap = pr.caps[opFeature(op)];
  add(p, "capability", cap.allowed ? "Операция разрешена профилем ЭБУ" : cap.reason, cap.allowed ? 1 : 0);
  add(p, "unresolved", "Нет незавершённой/неопределённой предыдущей операции", locked() ? 0 : 1);
  if (op != Op::ImpulseClear) {
    add(p, "session", "Связь с ЭБУ по K-Line есть", f.sessionReady ? 1 : 0);
    add(p, "ident", "ЭБУ идентифицирован", id.present ? 1 : 0);
  }
  if (f.interlockRequired) add(p, "interlock", "Аппаратный ключ WRITE-EN замкнут", f.interlockClosed ? 1 : 0);
  int8_t bat = f.batteryV < 0 ? (f.batteryManualConfirm ? 1 : -1) : (f.batteryV >= 11.0f ? 1 : 0);
  add(p, "battery", "Напряжение бортсети не ниже 11 В (измерено или подтверждено вами)", bat);

  bool liveFresh = f.liveAgeMs <= 3000;
  // Engine stopped: a fresh reading decides when it is conclusive (0 rpm, or a
  // running engine on a confirmed rpm address); otherwise - no reading, or a
  // reading the engine cannot vouch for - the user's own confirmation counts.
  int8_t stopped = (liveFresh && f.engineStopped >= 0) ? (f.engineStopped == 1 ? 1 : 0)
                                                       : (f.engineStoppedConfirm ? 1 : -1);
  const char* stoppedText = stopped == 0
                                ? "Двигатель работает: ЭБУ сообщает обороты больше 0. Заглушите двигатель, "
                                  "зажигание оставьте включённым"
                                : "Зажигание включено, двигатель заглушен (по оборотам или подтверждено вами)";
  switch (op) {
    case Op::DtcClear:
      // The device reads the fault memory itself right before planning (the
      // entries "before" go into the report and are compared with the result).
      add(p, "fault_snapshot", "Память неисправностей прочитана перед стиранием (записи «до» сохранятся в отчёте)",
          f.faultMemAgeMs <= 120000 ? 1 : 0);
      add(p, "engine_stopped", stoppedText, stopped);
      break;
    case Op::AdaptReset:
      add(p, "engine_stopped", stoppedText, stopped);
      add(p, "fault_snapshot", "Память неисправностей прочитана (< 2 мин) — сохранится в отчёте", f.faultMemAgeMs <= 120000 ? 1 : 0);
      add(p, "adapt_backup", "Самоадаптации прочитаны (< 2 мин) — значения «до сброса»", f.adaptAgeMs <= 120000 ? 1 : 0);
      break;
    case Op::ImpulseClear:
      add(p, "engine_stopped", stoppedText, f.engineStoppedConfirm ? 1 : -1);
      break;
    case Op::PurgeTest:
      add(p, "rpm_window", "Обороты 400–1500 об/мин", !liveFresh || f.rpm < 0 ? -1 : (f.rpm >= 400 && f.rpm <= 1500 ? 1 : 0));
      add(p, "ect_window", "Температура ОЖ 80–110 °C", !liveFresh || f.ect < -100 ? -1 : (f.ect >= 80 && f.ect <= 110 ? 1 : 0));
      break;
    default:
      break;
  }
  p.ok = true;
  for (uint8_t i = 0; i < p.nChecks; i++)
    if (p.checks[i].result != 1) {
      p.ok = false;
      if (!p.blockReason) p.blockReason = p.checks[i].text;
    }
  p.phrase[0] = 0;  // replaced by the access point password (D-035)
  snprintf(p.fingerprint, sizeof p.fingerprint, "%s", id.fingerprint);
}

bool ServiceGuard::prepare(Op op, const prof::EcuProfile& pr, const EcuIdent& id, const Facts& f, uint32_t nowMs,
                           uint32_t random32, Plan& out) {
  evaluate(op, pr, id, f, out);
  if (out.ok) {
    out.token = random32 ? random32 : 0x5A5A1234u;
    out.expiresMs = nowMs + kTokenTtlMs;
    if (state_ == State::Idle || state_ == State::Prepared) state_ = State::Prepared;
  }
  plan_ = out;
  return out.ok;
}

uint32_t ServiceGuard::passwordLockS(uint32_t nowMs) const {
  if (!pwLocked_ || (int32_t)(nowMs - pwLockUntil_) >= 0) return 0;
  return (pwLockUntil_ - nowMs + 999) / 1000;
}

static bool sameSecret(const char* a, const char* b) {
  // Constant time over the longer string: no early exit on the first mismatch.
  size_t la = a ? strlen(a) : 0, lb = b ? strlen(b) : 0;
  size_t n = la > lb ? la : lb;
  unsigned diff = (unsigned)(la ^ lb);
  for (size_t i = 0; i < n; i++) {
    unsigned char x = i < la ? (unsigned char)a[i] : 0, y = i < lb ? (unsigned char)b[i] : 0;
    diff |= (unsigned)(x ^ y);
  }
  return diff == 0 && lb > 0;
}

AuthResult ServiceGuard::authorize(Op op, uint32_t token, const char* password, const char* expected,
                                   const prof::EcuProfile& pr, const EcuIdent& id, const Facts& f, uint32_t nowMs) {
  if (locked()) return AuthResult::Locked;
  if (passwordLockS(nowMs)) return AuthResult::PasswordLocked;
  pwLocked_ = false;
  if (state_ != State::Prepared || plan_.op != op || !plan_.token) return AuthResult::NoPlan;
  if (token != plan_.token) return AuthResult::BadToken;
  if ((int32_t)(nowMs - plan_.expiresMs) > 0) {
    plan_ = Plan();
    state_ = State::Idle;
    return AuthResult::Expired;
  }
  if (f.interlockRequired && !f.interlockClosed) return AuthResult::InterlockOpen;
  if (!sameSecret(password, expected)) {
    if (++badPw_ >= kMaxBadPasswords) {
      badPw_ = 0;
      pwLocked_ = true;
      pwLockUntil_ = nowMs + kPasswordLockMs;
      plan_ = Plan();
      state_ = State::Idle;
      return AuthResult::PasswordLocked;
    }
    return AuthResult::BadPassword;
  }
  badPw_ = 0;
  if (strcmp(id.fingerprint, plan_.fingerprint) != 0) return AuthResult::IdentChanged;
  Plan fresh;
  evaluate(op, pr, id, f, fresh);
  lastBlock_ = fresh.blockReason;
  if (!fresh.ok) return AuthResult::ChecksFailed;
  plan_.token = 0;  // one-time
  return AuthResult::Ok;
}

void ServiceGuard::finish(bool success) {
  state_ = success ? State::Idle : State::FaultStop;
  active_ = Op::Count;
  plan_ = Plan();
}

}  // namespace guard
