// ServiceGuard.h - two-phase authorisation of state-changing operations.
//
// prepare(): evaluates capability, hardware interlock, ECU identity and
//            operation preconditions; returns a plan with a one-time token
//            (valid 20 s, HFMreader confirm window) and a confirmation phrase.
// authorize(): re-checks everything at execution time; the UI cannot unlock
//            anything by itself (REV 1.4 s.59.1, M14-16).
// Any failed or unknown write result puts the guard into FAULT_STOP
// (unresolved): further writes are refused until the operator acknowledges,
// reconnects and re-identifies. No automatic retry exists anywhere.
#pragma once
#include <stdint.h>

#include "HfmIdent.h"
#include "HfmProfiles.h"

namespace guard {

enum class Op : uint8_t { DtcClear, AdaptReset, PurgeTest, Count };
const char* opCode(Op op);
const char* opName(Op op);
uint8_t opFeature(Op op);  // prof::Feature

enum class State : uint8_t { Idle, Prepared, Executing, Active, Restoring, FaultStop };
const char* stateCode(State s);

// Facts collected by the engine right before prepare/authorize.
// Tri-state ints: 1 = true, 0 = false, -1 = unknown (never treated as true).
struct Facts {
  bool sessionReady = false;
  bool interlockClosed = false;   // WRITE-EN hardware jumper/key
  int8_t engineStopped = -1;      // 20:B7 == 0 from a fresh read
  float rpm = -1;                 // -1 unknown
  float ect = -999;               // -999 unknown
  uint32_t liveAgeMs = 0xFFFFFFFF;
  float batteryV = -1;            // -1 = no ADC fitted / unknown
  bool batteryManualConfirm = false;
  uint32_t faultMemAgeMs = 0xFFFFFFFF;
  bool faultMemNonZero = false;
  uint32_t adaptAgeMs = 0xFFFFFFFF;
};

struct Check {
  const char* id;
  const char* text;
  int8_t result;  // 1 ok, 0 fail, -1 unknown (fails for writes)
};

struct Plan {
  Op op = Op::Count;
  bool ok = false;
  uint32_t token = 0;
  uint32_t expiresMs = 0;
  char phrase[48] = {0};
  char fingerprint[24] = {0};
  Check checks[10];
  uint8_t nChecks = 0;
  const char* blockReason = nullptr;
};

enum class AuthResult : uint8_t { Ok, NoPlan, Expired, BadToken, BadPhrase, IdentChanged, ChecksFailed, Locked, InterlockOpen };
const char* authCode(AuthResult r);

class ServiceGuard {
 public:
  bool prepare(Op op, const prof::EcuProfile& p, const EcuIdent& id, const Facts& f, uint32_t nowMs,
               uint32_t random32, Plan& out);
  AuthResult authorize(Op op, uint32_t token, const char* phrase, const prof::EcuProfile& p, const EcuIdent& id,
                       const Facts& f, uint32_t nowMs);

  void begin(Op op) { state_ = (op == Op::PurgeTest) ? State::Active : State::Executing; active_ = op; }
  void restoring() { state_ = State::Restoring; }
  // success=false or unknown result -> FAULT_STOP (unresolved)
  void finish(bool success);
  void acknowledgeFault() { if (state_ == State::FaultStop) state_ = State::Idle; plan_ = Plan(); }

  State state() const { return state_; }
  Op activeOp() const { return active_; }
  bool locked() const { return state_ == State::FaultStop; }
  const Plan& plan() const { return plan_; }
  void cancel() { if (state_ == State::Prepared) state_ = State::Idle; plan_ = Plan(); }

  static const uint32_t kTokenTtlMs = 20000;

 private:
  void evaluate(Op op, const prof::EcuProfile& p, const EcuIdent& id, const Facts& f, Plan& out);
  State state_ = State::Idle;
  Op active_ = Op::Count;
  Plan plan_;
};

}  // namespace guard
