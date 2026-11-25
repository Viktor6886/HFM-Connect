#include "TraceLog.h"

#include <stdio.h>
#include <string.h>

void TraceLog::append(uint32_t tUs, TracePhase ph, Parity par, uint8_t tx, uint8_t echo, uint8_t resp,
                      LinkStatus st, CommandRisk risk, uint8_t rxCount) {
  lk();
  if (st != LinkStatus::Ok) errors_++;
  if (!enabled_) { ulk();return; }
  TraceEvent& e = buf_[head_];
  e.seq = nextSeq_++;
  e.tUs = tUs;
  e.phase = (uint8_t)ph;
  e.parity = (uint8_t)par;
  e.tx = tx;
  e.echo = echo;
  e.resp = resp;
  e.status = (uint8_t)st;
  e.risk = (uint8_t)risk; e.route=route_;e.contact=contact_;
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
      w.kv("t_us", e.tUs).kv("route",int(e.route)).kv("contact",unsigned(e.contact));
      w.kv("phase", tracePhaseCode((TracePhase)e.phase));
      w.kv("par", parityCode((Parity)e.parity));
      const bool frameRx = e.phase == (uint8_t)TracePhase::FrameRx;
      const bool frameTx = e.phase == (uint8_t)TracePhase::FrameTx;
      w.key("tx"); if (frameRx) w.null(); else w.hex(e.tx);
      w.key("echo"); if (frameRx || !e.rxCount) w.null(); else w.hex(e.echo);
      w.key("rx"); if (frameTx || e.rxCount<(frameRx ? 1 : 2)) w.null(); else w.hex(e.resp);
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
  const bool frameRx=e.phase==(uint8_t)TracePhase::FrameRx;
  const bool frameTx=e.phase==(uint8_t)TracePhase::FrameTx;
  const bool txPhase=frameTx || e.phase==(uint8_t)TracePhase::Prefix || e.phase==(uint8_t)TracePhase::Selector ||
                     e.phase==(uint8_t)TracePhase::Argument || e.phase==(uint8_t)TracePhase::Sync;
  char tx[3]{},rx[7]{};
  if(txPhase) snprintf(tx,sizeof tx,"%02X",e.tx);
  if(e.rxCount==1) snprintf(rx,sizeof rx,"%02X",frameRx ? e.resp : e.echo);
  if(e.rxCount==2) snprintf(rx,sizeof rx,"%02X %02X",e.echo,e.resp);
  const char* source=transport && (strstr(transport,"sim") || strstr(transport,"fixture")) ? "synthetic" :
                     transport && strstr(transport,"replay") ? "capture_replay" : "hardware";
  JsonWriter w;
  w.beginObject().kv("schema","hfm.trace.v2").kv("seq",e.seq).kv("monotonic_us",e.tUs)
   .kv("session_id",sessionId).kv("source",source).kv("transport",transport)
   .kv("route",int(e.route)).kv("contact",unsigned(e.contact)).kv("ecu_profile_id",e.route<0 ? ecuProfile : "catalog")
   .kv("phase",tracePhaseCode((TracePhase)e.phase)).kv("parity_mode",parityCode((Parity)e.parity))
   .kv("tx_hex",tx).kv("rx_hex",rx).key("rx_ninth").null().key("echo_ok");
  if(txPhase && e.rxCount) w.value(e.echo==e.tx); else w.null();
  w.key("ack_ok"); if(e.phase==(uint8_t)TracePhase::Prefix && e.rxCount==2) w.value(e.resp==uint8_t(~e.tx)); else w.null();
  w.kv("partial",txPhase || frameRx ? e.rxCount<(frameTx || frameRx ? 1 : 2) : false)
   .kv("status",linkStatusCode((LinkStatus)e.status)).kv("command_risk",commandRiskCode((CommandRisk)e.risk)).endObject();
  out+=w.str();out+='\n';
}
