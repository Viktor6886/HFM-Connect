#include "HfmServices.h"

#include <string.h>

namespace svc {

LinkStatus readFaultMemory(HfmSession& s, FaultMemory& fm, bool extended) {
  fm = FaultMemory();
  LinkStatus st = s.readFaultBytes(0x00, 16, fm.bankA);
  if (st != LinkStatus::Ok) return st;
  st = s.readFaultBytes(0x10, 16, fm.bankB);
  if (st != LinkStatus::Ok) return st;
  fm.valid = true;
  fm.tMs = s.link().nowMs();
  if (faultMemoryIsEcho(fm)) {
    // Not a fault bitmap: the ECU repeated F1 00..1F. Never decoded as codes.
    fm.valid = false;
    fm.echo = true;
    return st;
  }
  if (extended) {
    st = s.readFaultBytes(0x30, 3, fm.ext);
    if (st == LinkStatus::Ok) st = s.readFaultBytes(0x40, 3, fm.ext + 3);
    fm.extValid = (st == LinkStatus::Ok);
  }
  return st;
}

bool faultMemoryIsEcho(const FaultMemory& fm) {
  for (uint8_t i = 0; i < 16; i++)
    if (fm.bankA[i] != i || fm.bankB[i] != (uint8_t)(0x10 + i)) return false;
  return true;
}

LinkStatus readAdaptations(HfmSession& s, Adaptations& a) {
  a = Adaptations();
  static const RecipeStep steps[] = {
      {0x24, StepKind::Prefix, -1, 0}, {0x90, StepKind::Selector, -1, 0},
      {0x24, StepKind::Prefix, -1, 0}, {0x92, StepKind::Selector, -1, 0},
      {0x24, StepKind::Prefix, -1, 0}, {0x93, StepKind::Selector, -1, 0},
  };
  uint8_t r[6];
  LinkStatus st = s.runRecipe(steps, 6, r);
  if (st != LinkStatus::Ok) return st;
  static const uint8_t req[3] = {0x90, 0x92, 0x93};
  const uint8_t got[3] = {r[1], r[3], r[5]};
  if (repliesEchoRequest(req, got, 3)) {
    a.echo = true;  // the addresses came back, not adaptation values
    a.tMs = s.link().nowMs();
    return st;
  }
  a.rawIdle = r[1];
  a.rawLower = r[3];
  a.rawUpper = r[5];
  a.idleKgH = hfm::adaptIdleKgH(r[1]);
  a.lower = hfm::adaptFactor(r[3]);
  a.upper = hfm::adaptFactor(r[5]);
  a.valid = true;
  a.tMs = s.link().nowMs();
  return st;
}

LinkStatus readCoding(HfmSession& s, Coding& c) {
  c = Coding();
  static const RecipeStep steps[] = {
      {0x27, StepKind::Prefix, -1, 0},
      {0x02, StepKind::Selector, -1, 0},
      {0x24, StepKind::Prefix, -1, 0},
      {0xF0, StepKind::Selector, -1, 0},
  };
  uint8_t r[4];
  LinkStatus st = s.runRecipe(steps, 4, r);
  if (st != LinkStatus::Ok) return st;
  static const uint8_t req[2] = {0x02, 0xF0};
  const uint8_t got[2] = {r[1], r[3]};
  if (repliesEchoRequest(req, got, 2)) {
    c.echo = true;  // "02 / F0" is the request itself, not a coding byte
    c.tMs = s.link().nowMs();
    return st;
  }
  c.info = hfm::decodeCoding(r[1], r[3]);
  c.valid = true;
  c.tMs = s.link().nowMs();
  return st;
}

LinkStatus readVin(HfmSession& s, Vin& v) {
  v = Vin();
  s.link().sleepMs(100);  // REV 1.1: 100 ms pause precedes (F9,00)
  for (uint8_t i = 0; i < 14; i++) {
    LinkStatus st = s.prefix(0xF9);
    if (st != LinkStatus::Ok) {
      // The very first F9 not acknowledged with 06: this ECU has no VIN
      // service (older HFM generations). Nothing was read or changed.
      if (i == 0 && (st == LinkStatus::BadAck || st == LinkStatus::Timeout)) {
        v.unsupported = true;
        v.ackSeen = s.lastReply();
      }
      return st;
    }
    uint8_t b = 0;
    st = s.selector(i, b);
    if (st != LinkStatus::Ok) return st;
    if (i == 0 && (b == 0x00 || b == 0xFF)) {  // VIN not stored
      v.notStored = true;
      return LinkStatus::Ok;
    }
    v.raw[i] = b;
    v.text[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '?';
    v.len = (uint8_t)(i + 1);
  }
  v.valid = true;
  return LinkStatus::Ok;
}

// ---- write recipes -------------------------------------------------------------

LinkStatus dtcClear(HfmSession& s, bool& verified, FaultMemory& after, bool* confirmed) {
  verified = false;
  if (confirmed) *confirmed = false;
  // REV 1.1 s.16 DTC clear sequence and verification.
  static const RecipeStep steps[] = {
      // 10 00 is the idle telegram of the PC tester and of the factory tester
      // (its reply is a data byte that they do not check); F3 FF is the erase
      // command, its reply must be the complement 00 (REV 1.1, HHT AT6).
      {0x10, StepKind::Prefix, -1, 0}, {0x00, StepKind::Argument, -1, 200},
      {0xF3, StepKind::Prefix, -1, 0}, {0xFF, StepKind::Argument, 0x00, 50},
      {0x10, StepKind::Prefix, -1, 0}, {0x00, StepKind::Argument, -1, 500},
      {0x10, StepKind::Prefix, -1, 0}, {0x00, StepKind::Argument, -1, 200},
      {0xF3, StepKind::Prefix, -1, 0}, {0xFF, StepKind::Argument, 0x00, 50},
      {0x10, StepKind::Prefix, -1, 0}, {0x00, StepKind::Argument, -1, 500},
      {0x10, StepKind::Prefix, -1, 0}, {0x00, StepKind::Argument, -1, 0},
  };
  s.link().sleepMs(200);
  LinkStatus st = s.runRecipe(steps, sizeof(steps) / sizeof(steps[0]), nullptr);
  s.disarmWrite();
  if (st != LinkStatus::Ok) return st;
  if (confirmed) *confirmed = true;
  st = readFaultMemory(s, after, false);
  if (st != LinkStatus::Ok) return st;
  verified = true;
  for (int i = 0; i < 16; i++)
    if (after.bankA[i] || after.bankB[i]) verified = false;
  return LinkStatus::Ok;
}

LinkStatus adaptationReset(HfmSession& s, uint8_t* r) {
  // REV 1.1 s.16 adaptation reset with entry check.
  static const RecipeStep entry[] = {
      {0xCF, StepKind::Prefix, -1, 0}, {0xFF, StepKind::Argument, 0x00, 0},
      {0xC1, StepKind::Prefix, -1, 0}, {0xFC, StepKind::Argument, 0x03, 0},
  };
  static const RecipeStep body[] = {
      {0xE0, StepKind::Prefix, -1, 0}, {0x00, StepKind::Argument, -1, 0},
      {0xCF, StepKind::Prefix, -1, 0}, {0x00, StepKind::Argument, -1, 0},
      {0xC0, StepKind::Prefix, -1, 0}, {0xFC, StepKind::Argument, -1, 0},
  };
  LinkStatus st = s.runRecipe(entry, 4, r);  // entry check: FF->00 and FC->03
  if (st == LinkStatus::Ok) st = s.runRecipe(body, 6, r ? r + 4 : nullptr);
  s.disarmWrite();
  return st;
}

static LinkStatus ready96(HfmSession& s) {
  uint8_t r = 0;
  LinkStatus st = s.argument(0x96, r);
  if (st != LinkStatus::Ok) return st;
  return (r & 0x40) ? LinkStatus::Ok : LinkStatus::BadArgReply;
}

static LinkStatus pw(HfmSession& s, uint8_t prefix, uint8_t arg) {
  LinkStatus st = s.prefix(prefix);
  if (st != LinkStatus::Ok) return st;
  uint8_t r;
  return s.argument(arg, r);
}

LinkStatus purgeEnter(HfmSession& s, bool& sondeSent) {
  sondeSent = false;
  static const RecipeStep entry[] = {
      {0xCF, StepKind::Prefix, -1, 0}, {0xFF, StepKind::Argument, 0x00, 0},
      {0xC1, StepKind::Prefix, -1, 0}, {0xFC, StepKind::Argument, 0x03, 0},
  };
  LinkStatus st = s.runRecipe(entry, 4, nullptr);
  if (st != LinkStatus::Ok) return st;
  if ((st = ready96(s)) != LinkStatus::Ok) return st;
  if ((st = pw(s, 0xD0, 0x0F)) != LinkStatus::Ok) return st;  // sonde
  sondeSent = true;
  if ((st = ready96(s)) != LinkStatus::Ok) return st;
  return pw(s, 0xD4, 0x00);  // start in OFF state
}

LinkStatus purgeSet(HfmSession& s, bool on) {
  LinkStatus st = on ? ready96(s) : LinkStatus::Ok;
  if (!on) {
    uint8_t r;
    st = s.argument(0x96, r);
  }
  if (st != LinkStatus::Ok) return st;
  return pw(s, 0xD4, on ? 0x1A : 0x00);
}

LinkStatus purgeRestore(HfmSession& s, bool sondeSent) {
  uint8_t r;
  LinkStatus st = s.argument(0x96, r);
  if (st == LinkStatus::Ok) st = pw(s, 0xD0, 0x0B);
  if (st == LinkStatus::Ok) st = pw(s, 0xD1, 0x00);
  if (st == LinkStatus::Ok) st = pw(s, 0xD2, 0x00);
  if (st == LinkStatus::Ok) st = pw(s, 0xD3, 0x40);
  if (st == LinkStatus::Ok) st = pw(s, 0xD4, 0x00);
  if (st == LinkStatus::Ok && sondeSent) st = pw(s, 0xD0, 0x0F);
  s.disarmWrite();
  return st;
}

}  // namespace svc
