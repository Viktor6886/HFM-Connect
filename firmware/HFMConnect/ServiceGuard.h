// ServiceGuard.h - two-phase authorisation of state-changing operations.
//
// prepare(): evaluates capability, ECU identity, the optional hardware
//            interlock and the operation preconditions; returns a plan with a
//            one-time token (valid 60 s).
// authorize(): checks the password of the access point (D-035; five wrong
//            entries lock it for 60 s) and re-checks everything at execution
//            time; the UI cannot unlock anything by itself (REV 1.4 s.59.1).
// Any failed or unknown write result puts the guard into FAULT_STOP
// (unresolved): further writes are refused until the operator acknowledges,
// reconnects and re-identifies. No automatic retry exists anywhere.
#pragma once
#include <stdint.h>

#include "HfmIdent.h"
#include "HfmProfiles.h"

namespace guard {

enum class Op : uint8_t { DtcClear, AdaptReset, PurgeTest, ImpulseClear, Count };
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
  bool interlockRequired = false; // setting: the key is fitted and must be closed
  bool engineStoppedConfirm = false; // user confirmed "ignition ON, engine stopped"
  // From a fresh rpm read: 1 = 0 rpm; 0 = running on a confirmed rpm address
  // (Bosch 20:B7); -1 = no conclusive reading (then the confirmation decides).
  int8_t engineStopped = -1;
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

enum class AuthResult : uint8_t { Ok, NoPlan, Expired, BadToken, BadPassword, PasswordLocked, IdentChanged, ChecksFailed, Locked, InterlockOpen };
const char* authCode(AuthResult r);

class ServiceGuard {
 public:
  bool prepare(Op op, const prof::EcuProfile& p, const EcuIdent& id, const Facts& f, uint32_t nowMs,
               uint32_t random32, Plan& out);
  // password: typed by the user; expected: the access point password (never
  // sent to the UI). Compared in constant time.
  AuthResult authorize(Op op, uint32_t token, const char* password, const char* expected, const prof::EcuProfile& p,
                       const EcuIdent& id, const Facts& f, uint32_t nowMs);
  // Seconds left of the password lock (0 = not locked).
  uint32_t passwordLockS(uint32_t nowMs) const;
  // The check that failed when authorize() returned ChecksFailed (static text).
  const char* lastBlockReason() const { return lastBlock_ ? lastBlock_ : ""; }

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

  static const uint32_t kTokenTtlMs = 60000;   // time to read the warning and type the password
  static const uint8_t kMaxBadPasswords = 5;
  static const uint32_t kPasswordLockMs = 60000;

 private:
  void evaluate(Op op, const prof::EcuProfile& p, const EcuIdent& id, const Facts& f, Plan& out);
  State state_ = State::Idle;
  Op active_ = Op::Count;
  Plan plan_;
  uint8_t badPw_ = 0;
  uint32_t pwLockUntil_ = 0;
  bool pwLocked_ = false;
  const char* lastBlock_ = nullptr;
};

}  // namespace guard
