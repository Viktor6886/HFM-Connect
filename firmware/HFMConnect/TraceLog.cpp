#include "TraceLog.h"

#include <stdio.h>

void TraceLog::append(uint32_t tUs, TracePhase ph, Parity par, uint8_t tx, uint8_t echo, uint8_t resp,
                      LinkStatus st, CommandRisk risk, uint8_t rxCount) {
  if (st != LinkStatus::Ok) errors_++;
  if (!enabled_) return;
  lk();
  TraceEvent& e = buf_[head_];
  e.seq = nextSeq_++;
  e.tUs = tUs;
  e.phase = (uint8_t)ph;
  e.parity = (uint8_t)par;
  e.tx = tx;
  e.echo = echo;
  e.resp = resp;
  e.status = (uint8_t)st;
  e.risk = (uint8_t)risk;
  e.rxCount = rxCount;
  head_ = (uint16_t)((head_ + 1) % kCapacity);
  if (count_ < kCapacity) count_++;
  TraceEvent copy = e;
  ulk();
  if (obs_) obs_(copy, obsCtx_);
}

uint16_t TraceLog::copySince(uint32_t afterSeq, TraceEvent* out, uint16_t max) const {
  uint16_t n = 0;
  lk();
  uint16_t start = (uint16_t)((head_ + kCapacity - count_) % kCapacity);
  for (uint16_t i = 0; i < count_ && n < max; i++) {
    const TraceEvent& e = buf_[(start + i) % kCapacity];
    if (e.seq > afterSeq) out[n++] = e;
  }
  ulk();
  return n;
}

void TraceLog::writeJson(JsonWriter& w, uint32_t afterSeq, uint16_t max) const {
  w.beginArray();
  TraceEvent chunk[32];
  uint16_t total = 0;
  uint32_t after = afterSeq;
  while (total < max) {
    uint16_t want = (uint16_t)((max - total) < 32 ? (max - total) : 32);
    uint16_t n = copySince(after, chunk, want);
    if (!n) break;
    for (uint16_t i = 0; i < n; i++) {
      const TraceEvent& e = chunk[i];
      w.beginObject();
      w.kv("seq", e.seq);
      w.kv("t_us", e.tUs);
      w.kv("phase", tracePhaseCode((TracePhase)e.phase));
      w.kv("par", e.parity == (uint8_t)Parity::Space ? "S" : "N");
      w.key("tx").hex(e.tx);
      w.key("echo").hex(e.echo);
      w.key("rx").hex(e.resp);
      w.kv("n", (unsigned)e.rxCount);
      w.kv("st", linkStatusCode((LinkStatus)e.status));
      w.kv("risk", commandRiskCode((CommandRisk)e.risk));
      w.endObject();
    }
    total = (uint16_t)(total + n);
    after = chunk[n - 1].seq;
  }
  w.endArray();
}

void TraceLog::writeJsonl(std::string& out, const TraceEvent& e, const char* sessionId,
                          const char* transport, const char* ecuProfile) {
  char line[320];
  bool txPhase = e.phase == (uint8_t)TracePhase::Prefix || e.phase == (uint8_t)TracePhase::Selector ||
                 e.phase == (uint8_t)TracePhase::Argument || e.phase == (uint8_t)TracePhase::Sync;
  bool echoOk = e.rxCount >= 1 && e.echo == e.tx;
  bool ackOk = e.phase != (uint8_t)TracePhase::Prefix || (e.rxCount >= 2 && e.resp == (uint8_t)(e.tx ^ 0xFF));
  snprintf(line, sizeof line,
           "{\"schema\":\"hfm.trace.v1\",\"seq\":%lu,\"monotonic_us\":%lu,\"session_id\":\"%s\","
           "\"transport\":\"%s\",\"ecu_profile_id\":\"%s\",\"phase\":\"%s\",\"parity_mode\":\"%s\","
           "\"tx_hex\":\"%02X\",\"rx_hex\":\"%s\",\"echo_ok\":%s,\"ack_ok\":%s,\"partial\":%s,"
           "\"status\":\"%s\",\"command_risk\":\"%s\"}\n",
           (unsigned long)e.seq, (unsigned long)e.tUs, sessionId ? sessionId : "", transport ? transport : "",
           ecuProfile ? ecuProfile : "UNRESOLVED", tracePhaseCode((TracePhase)e.phase),
           e.parity == (uint8_t)Parity::Space ? "S" : "N", e.tx,
           e.rxCount == 0 ? "" : (e.rxCount == 1 ? "--" : "++"),
           txPhase && echoOk ? "true" : "false", ackOk ? "true" : "false", e.rxCount < 2 ? "true" : "false",
           linkStatusCode((LinkStatus)e.status), commandRiskCode((CommandRisk)e.risk));
  // Replace the rx placeholder with real bytes (keeps snprintf format simple).
  std::string s(line);
  size_t p = s.find("\"rx_hex\":\"");
  if (p != std::string::npos) {
    p += 10;
    size_t q = s.find('"', p);
    char rx[8] = {0};
    if (e.rxCount == 1) snprintf(rx, sizeof rx, "%02X", e.echo);
    else if (e.rxCount >= 2) snprintf(rx, sizeof rx, "%02X %02X", e.echo, e.resp);
    s.replace(p, q - p, rx);
  }
  out += s;
}
