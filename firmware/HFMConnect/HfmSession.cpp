#include "HfmSession.h"

PrefixClass classifyPrefix(uint8_t op) {
  switch (op) {
    // Read pages (REV 1.1 s.5):
    case 0x20:  // live data page 1
    case 0x21:  // live data page 2 (temperatures, status bits)
    case 0x24:  // adaptations 90/92/93 and configuration mask F0
    case 0x27:  // coding byte 02
    case 0xF0:  // identification 00..1B
    case 0xF1:  // fault memory 00..1F
    // VIN bytes 00..0D: a read of the PC tester and factory tester (HHT AT99);
    // ECUs without the service answer the prefix with another byte - the
    // engine reports "not supported" and resumes the session (D-034).
    case 0xF9:
      return PrefixClass::Read;
    // VDO variant live-data pages (factory tester templates for vendor V;
    // never field-tested): only with the experimental opt-in.
    case 0x12:
    case 0x22:
    case 0x23:
      return PrefixClass::ExperimentalRead;
    // State-changing prefixes (REV 1.1 s.5.2/16): never sent without a token.
    case 0x10:  // DTC clear sequence element
    case 0xF3:  // DTC erase
    case 0xCF:
    case 0xC1:
    case 0xC0:
    case 0xE0:  // control mode entry / adaptation reset
    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3:
    case 0xD4:  // actuators (lambda sonde, purge, restore)
      return PrefixClass::Write;
    default:
      return PrefixClass::Unknown;
  }
}

LinkStatus HfmSession::fail(LinkStatus st) {
  lastError_ = st;
  errCount_++;
  pageValid_ = false;
  if (st != LinkStatus::NotAllowed && st != LinkStatus::NotConnected) state_ = SessionState::Error;
  return st;
}

uint32_t HfmSession::silenceNeededMs() {
  if (!lineUsed_) return 0;
  uint32_t need = (uint32_t)timing_.resyncSilenceMs + (uint32_t)(wakeFails_ < 4 ? wakeFails_ : 4) * 1000u;
  uint32_t el = link_.nowMs() - lastLineMs_;
  return el >= need ? 0 : need - el;
}

LinkStatus HfmSession::connect(uint8_t attemptsWanted) {
  state_ = SessionState::Connecting;
  pageValid_ = false;
  writeToken_ = 0;
  lastWasWrite_ = false;
  if (!link_.isOpen() && !link_.open()) return fail(LinkStatus::LinkDown);
  LinkStatus st = LinkStatus::NoSync;
  uint16_t attempts = attemptsWanted ? attemptsWanted : (timing_.syncRetries ? timing_.syncRetries : 1);
  for (uint16_t i = 0; i < attempts; i++) {
    uint32_t wait = silenceNeededMs();
    if (i && wait < timing_.syncRetryPauseMs) wait = timing_.syncRetryPauseMs;
    if (wait) link_.sleepMs(wait);
    trace_.append(link_.nowUs(), TracePhase::Break, Parity::None, 0, 0, 0, LinkStatus::Ok, CommandRisk::Read, 0);
    syncEcho_ = syncByte_ = 0;
    st = link_.wakeup(timing_, syncEcho_, syncByte_);
    lineUsed_ = true;
    lastLineMs_ = link_.nowMs();
    uint8_t n = (uint8_t)((syncEcho_ ? 1 : 0) + (syncByte_ ? 1 : 0));
    trace_.append(link_.nowUs(), TracePhase::Sync, Parity::Space, 0xAA, syncEcho_, syncByte_, st, CommandRisk::Read, n);
    if (st == LinkStatus::Ok) break;
    if (wakeFails_ < 0xFF) wakeFails_++;
    if (st == LinkStatus::LinkDown) break;
  }
  if (st != LinkStatus::Ok) {
    lastError_ = st;
    errCount_++;
    state_ = SessionState::Error;
    return st;
  }
  wakeFails_ = 0;
  state_ = SessionState::Ready;
  lastError_ = LinkStatus::Ok;
  connectedAtMs_ = lastActivityMs_ = link_.nowMs();
  return st;
}

bool HfmSession::resyncAllowed() const {
  if (state_ != SessionState::Error || !link_.isOpen()) return false;
  if (writeToken_ || pageIsWrite_ || lastWasWrite_) return false;  // never resume around a write
  return lastError_ == LinkStatus::Timeout || lastError_ == LinkStatus::EchoMismatch ||
         lastError_ == LinkStatus::BadAck;
}

LinkStatus HfmSession::resync() {
  if (!resyncAllowed()) return LinkStatus::NotAllowed;
  // Let a late echo/reply of the failed byte arrive (exchange() purges it).
  link_.sleepMs(300);
  uint8_t ack = 0;
  LinkStatus st = xchg(0xF0, Parity::None, TracePhase::Prefix, CommandRisk::Read, ack);
  if (st != LinkStatus::Ok) {
    lastError_ = st;
    errCount_++;
    pageValid_ = false;
    state_ = SessionState::Error;
    return st;
  }
  state_ = SessionState::Ready;
  lastError_ = LinkStatus::Ok;
  page_ = 0xF0;
  pageValid_ = true;
  pageIsWrite_ = false;
  resyncCount_++;
  return LinkStatus::Ok;
}

void HfmSession::disconnect() {
  link_.close();
  state_ = SessionState::Disconnected;
  pageValid_ = false;
  writeToken_ = 0;
}

LinkStatus HfmSession::xchg(uint8_t tx, Parity par, TracePhase ph, CommandRisk risk, uint8_t& resp) {
  uint8_t echo = 0;
  resp = 0;
  LinkStatus st = link_.exchange(tx, par, timing_.byteDeadlineMs, echo, resp);
  lastTx_ = tx;
  lastReply_ = resp;
  txCount_++;
  lastActivityMs_ = link_.nowMs();
  lineUsed_ = true;
  lastLineMs_ = lastActivityMs_;
  uint8_t n = 2;
  if (st == LinkStatus::Timeout) n = (uint8_t)(echo == tx ? 1 : 0);
  if (st == LinkStatus::Ok && echo != tx) st = LinkStatus::EchoMismatch;
  if (st == LinkStatus::Ok && ph == TracePhase::Prefix && resp != (uint8_t)(tx ^ 0xFF)) st = LinkStatus::BadAck;
  trace_.append(link_.nowUs(), ph, par, tx, echo, resp, st, risk, n);
  return st;
}

LinkStatus HfmSession::prefix(uint8_t op) {
  if (state_ != SessionState::Ready) return fail(LinkStatus::NotConnected);
  PrefixClass pc = classifyPrefix(op);
  CommandRisk risk = CommandRisk::Read;
  if (pc == PrefixClass::Unknown) return fail(LinkStatus::NotAllowed);
  if (pc == PrefixClass::ExperimentalRead && !experimentalReads_) return fail(LinkStatus::NotAllowed);
  if (pc == PrefixClass::Write) {
    if (!writeToken_) return fail(LinkStatus::NotAllowed);
    risk = CommandRisk::Write;
    lastWasWrite_ = true;
  }
  uint8_t ack = 0;
  LinkStatus st = xchg(op, Parity::None, TracePhase::Prefix, risk, ack);
  if (st != LinkStatus::Ok) return fail(st);
  page_ = op;
  pageValid_ = true;
  pageIsWrite_ = (pc == PrefixClass::Write);
  if (pageIsWrite_) lastWasWrite_ = true;
  return LinkStatus::Ok;
}

LinkStatus HfmSession::selector(uint8_t sel, uint8_t& data) {
  if (state_ != SessionState::Ready) return fail(LinkStatus::NotConnected);
  // A selector relies on the remembered page; without a valid read page the
  // byte would be interpreted in an unknown context -> refuse.
  if (!pageValid_ || pageIsWrite_) return fail(LinkStatus::NotAllowed);
  LinkStatus st = xchg(sel, Parity::Space, TracePhase::Selector, CommandRisk::Read, data);
  if (st != LinkStatus::Ok) return fail(st);
  return LinkStatus::Ok;
}

LinkStatus HfmSession::argument(uint8_t arg, uint8_t& reply, int16_t expect) {
  if (state_ != SessionState::Ready) return fail(LinkStatus::NotConnected);
  if (!pageValid_ || !pageIsWrite_ || !writeToken_) return fail(LinkStatus::NotAllowed);
  LinkStatus st = xchg(arg, Parity::Space, TracePhase::Argument, CommandRisk::Write, reply);
  if (st != LinkStatus::Ok) return fail(st);
  if (expect >= 0 && reply != (uint8_t)expect) return fail(LinkStatus::BadArgReply);
  return LinkStatus::Ok;
}

LinkStatus HfmSession::readGroup(uint8_t op, const uint8_t* sels, uint8_t n, uint8_t* out) {
  LinkStatus st = prefix(op);
  if (st != LinkStatus::Ok) return st;
  for (uint8_t i = 0; i < n; i++) {
    st = selector(sels[i], out[i]);
    if (st != LinkStatus::Ok) return st;
  }
  return LinkStatus::Ok;
}

LinkStatus HfmSession::readIdent(uint8_t* out, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    LinkStatus st = prefix(0xF0);
    if (st != LinkStatus::Ok) return st;
    st = selector(i, out[i]);
    if (st != LinkStatus::Ok) return st;
  }
  return LinkStatus::Ok;
}

LinkStatus HfmSession::readFaultBytes(uint8_t first, uint8_t count, uint8_t* out) {
  for (uint8_t i = 0; i < count; i++) {
    LinkStatus st = prefix(0xF1);
    if (st != LinkStatus::Ok) return st;
    st = selector((uint8_t)(first + i), out[i]);
    if (st != LinkStatus::Ok) return st;
  }
  return LinkStatus::Ok;
}

LinkStatus HfmSession::runRecipe(const RecipeStep* steps, uint8_t n, uint8_t* responses) {
  for (uint8_t i = 0; i < n; i++) {
    const RecipeStep& s = steps[i];
    LinkStatus st;
    uint8_t r = 0;
    switch (s.kind) {
      case StepKind::Prefix:
        st = prefix(s.byte);
        r = (uint8_t)(s.byte ^ 0xFF);
        break;
      case StepKind::Selector:
        st = selector(s.byte, r);
        if (st == LinkStatus::Ok && s.expect >= 0 && r != (uint8_t)s.expect) st = fail(LinkStatus::BadArgReply);
        break;
      default:
        st = argument(s.byte, r, s.expect);
        break;
    }
    if (responses) responses[i] = r;
    if (st != LinkStatus::Ok) return st;
    if (s.delayMs) link_.sleepMs(s.delayMs);
  }
  return LinkStatus::Ok;
}
