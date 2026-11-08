#include "ServiceGuard.h"

#include <stdio.h>
#include <string.h>

namespace guard {

const char* opCode(Op op) {
  switch (op) {
    case Op::DtcClear: return "dtc_clear";
    case Op::AdaptReset: return "adapt_reset";
    case Op::PurgeTest: return "purge_test";
    default: return "none";
  }
}

const char* opName(Op op) {
  switch (op) {
    case Op::DtcClear: return "Стирание памяти неисправностей";
    case Op::AdaptReset: return "Сброс самоадаптаций смеси";
    case Op::PurgeTest: return "Тест клапана продувки адсорбера Y58/1";
    default: return "—";
  }
}

uint8_t opFeature(Op op) {
  switch (op) {
    case Op::DtcClear: return prof::F_DTC_CLEAR;
    case Op::AdaptReset: return prof::F_ADAPT_RESET;
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
    case AuthResult::BadPhrase: return "BAD_PHRASE";
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
  add(p, "session", "Сеанс K-Line активен", f.sessionReady ? 1 : 0);
  add(p, "interlock", "Аппаратный ключ WRITE-EN замкнут", f.interlockClosed ? 1 : 0);
  add(p, "ident", "ЭБУ идентифицирован (F0, 28 байт)", id.present ? 1 : 0);
  int8_t bat = f.batteryV < 0 ? (f.batteryManualConfirm ? 1 : -1) : (f.batteryV >= 11.0f ? 1 : 0);
  add(p, "battery", "Напряжение бортсети ≥ 11 В (АЦП или ручное подтверждение)", bat);

  bool liveFresh = f.liveAgeMs <= 3000;
  switch (op) {
    case Op::DtcClear:
      add(p, "fault_snapshot", "Свежее чтение памяти неисправностей (< 60 с) с ненулевыми байтами",
          (f.faultMemAgeMs <= 60000 && f.faultMemNonZero) ? 1 : 0);
      add(p, "engine_stopped", "Зажигание ВКЛ, двигатель остановлен (20:B7 = 0)",
          !liveFresh ? -1 : (f.engineStopped == 1 ? 1 : 0));
      break;
    case Op::AdaptReset:
      add(p, "engine_stopped", "Зажигание ВКЛ, двигатель остановлен (20:B7 = 0, свежее чтение)",
          !liveFresh ? -1 : (f.engineStopped == 1 ? 1 : 0));
      add(p, "fault_snapshot", "Снимок памяти неисправностей сохранён (< 120 с)", f.faultMemAgeMs <= 120000 ? 1 : 0);
      add(p, "adapt_backup", "Резервная копия адаптаций прочитана (< 120 с)", f.adaptAgeMs <= 120000 ? 1 : 0);
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
  const char* verb = op == Op::DtcClear ? "CLEAR DTC" : (op == Op::AdaptReset ? "RESET ADAPT" : "PURGE TEST");
  snprintf(p.phrase, sizeof p.phrase, "%s %s", verb, id.partValid ? id.partNumber : "UNKNOWN");
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

AuthResult ServiceGuard::authorize(Op op, uint32_t token, const char* phrase, const prof::EcuProfile& pr,
                                   const EcuIdent& id, const Facts& f, uint32_t nowMs) {
  if (locked()) return AuthResult::Locked;
  if (state_ != State::Prepared || plan_.op != op || !plan_.token) return AuthResult::NoPlan;
  if (token != plan_.token) return AuthResult::BadToken;
  if ((int32_t)(nowMs - plan_.expiresMs) > 0) {
    plan_ = Plan();
    state_ = State::Idle;
    return AuthResult::Expired;
  }
  if (!f.interlockClosed) return AuthResult::InterlockOpen;
  if (!phrase || strcmp(phrase, plan_.phrase) != 0) return AuthResult::BadPhrase;
  if (strcmp(id.fingerprint, plan_.fingerprint) != 0) return AuthResult::IdentChanged;
  Plan fresh;
  evaluate(op, pr, id, f, fresh);
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
