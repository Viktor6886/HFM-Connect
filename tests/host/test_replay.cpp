// Trace replay: a recorded JSONL trace (hfm.trace.v1) is replayed through a
// ByteLink so the full decode path runs without hardware.
//
// tests/traces/sim_*.jsonl are SYNTHETIC traces recorded from SimEcu (marked
// transport "sim"). Real ECU captures go to tests/traces/ecu_*.jsonl and are
// replayed by the same code (none exist yet).
#include <stdlib.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "HfmDtc.h"
#include "HfmIdent.h"
#include "HfmLive.h"
#include "HfmServices.h"
#include "SimEcu.h"
#include "minitest.h"

#ifndef HFM_TRACE_DIR
#define HFM_TRACE_DIR "tests/traces"
#endif

struct TraceLine {
  std::string phase;
  uint8_t tx = 0, echo = 0, resp = 0;
  int n = 0;
  std::string status;
};

static std::string field(const std::string& l, const char* k) {
  std::string key = std::string("\"") + k + "\":";
  size_t p = l.find(key);
  if (p == std::string::npos) return "";
  p += key.size();
  if (l[p] == '"') {
    size_t q = l.find('"', p + 1);
    return l.substr(p + 1, q - p - 1);
  }
  size_t q = l.find_first_of(",}", p);
  return l.substr(p, q - p);
}

static std::vector<TraceLine> loadTrace(const std::string& path) {
  std::vector<TraceLine> out;
  std::ifstream f(path);
  std::string l;
  while (std::getline(f, l)) {
    if (l.empty() || l[0] != '{') continue;
    TraceLine t;
    t.phase = field(l, "phase");
    t.tx = (uint8_t)strtoul(field(l, "tx_hex").c_str(), nullptr, 16);
    std::string rx = field(l, "rx_hex");
    if (rx.size() >= 2) { t.echo = (uint8_t)strtoul(rx.substr(0, 2).c_str(), nullptr, 16); t.n = 1; }
    if (rx.size() >= 5) { t.resp = (uint8_t)strtoul(rx.substr(3, 2).c_str(), nullptr, 16); t.n = 2; }
    t.status = field(l, "status");
    out.push_back(t);
  }
  return out;
}

class ReplayLink : public ByteLink {
 public:
  explicit ReplayLink(std::vector<TraceLine> t) : t_(std::move(t)) {}
  bool open() override { open_ = true; return true; }
  void close() override { open_ = false; }
  bool isOpen() const override { return open_; }
  LinkStatus wakeup(const LinkTiming&, uint8_t& echo, uint8_t& sync) override {
    while (i_ < t_.size() && t_[i_].phase == "BREAK") i_++;
    if (i_ >= t_.size() || t_[i_].phase != "SYNC") return LinkStatus::NoSync;
    const TraceLine& l = t_[i_++];
    echo = l.echo;
    sync = l.resp;
    return l.status == "OK" ? LinkStatus::Ok : LinkStatus::NoSync;
  }
  LinkStatus exchange(uint8_t tx, Parity, uint16_t, uint8_t& echo, uint8_t& resp) override {
    if (i_ >= t_.size()) return LinkStatus::Timeout;
    const TraceLine& l = t_[i_++];
    if (l.tx != tx) { mismatches++; return LinkStatus::EchoMismatch; }
    echo = l.echo;
    resp = l.resp;
    return l.n < 2 ? LinkStatus::Timeout : LinkStatus::Ok;
  }
  void sleepMs(uint32_t ms) override { t += ms; }
  uint32_t nowMs() override { return t; }
  uint32_t nowUs() override { return t * 1000; }
  const char* name() const override { return "replay"; }
  size_t consumed() const { return i_; }
  int mismatches = 0;

 private:
  std::vector<TraceLine> t_;
  size_t i_ = 0;
  bool open_ = false;
  uint32_t t = 0;
};

// Records a reference session on the simulator and returns its JSONL.
static std::string recordSimSession() {
  SimEcu sim;
  sim.setScenario(SimEcu::Scenario::Faults);
  TraceLog log;
  HfmSession s(sim, log);
  s.connect();
  uint8_t raw[28];
  s.readIdent(raw, 28);
  svc::FaultMemory fm;
  svc::readFaultMemory(s, fm, false);
  live::Snapshot snap;
  live::pollNative(s, snap);
  static TraceEvent ev[TraceLog::kCapacity];
  uint16_t n = log.copySince(0, ev, TraceLog::kCapacity);
  std::string out;
  for (uint16_t i = 0; i < n; i++) TraceLog::writeJsonl(out, ev[i], "golden", "sim", "BOSCH_0135451332_41");
  return out;
}

static const char* kGolden = HFM_TRACE_DIR "/sim_connect_ident_dtc_live.jsonl";

TEST(golden_trace_is_reproducible) {
  std::string rec = recordSimSession();
  if (getenv("HFM_REGEN_TRACES")) {
    std::ofstream(kGolden, std::ios::binary) << rec;
  }
  std::ifstream f(kGolden, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  CHECK(!ss.str().empty());
  // Timestamps are deterministic in SimEcu (virtual clock) -> byte-exact.
  CHECK(ss.str() == rec);
}

TEST(replay_golden_trace_decodes_identically) {
  std::vector<TraceLine> t = loadTrace(kGolden);
  CHECK(t.size() > 100);
  ReplayLink link(t);
  TraceLog log;
  HfmSession s(link, log);
  CHECK(s.connect() == LinkStatus::Ok);
  uint8_t raw[28];
  CHECK(s.readIdent(raw, 28) == LinkStatus::Ok);
  EcuIdent id;
  CHECK(parseIdent(raw, 28, id));
  CHECK_STR(id.partNumber, "0135451332");
  svc::FaultMemory fm;
  CHECK(svc::readFaultMemory(s, fm, false) == LinkStatus::Ok);
  dtc::Decoded d[8];
  dtc::Variant v;
  CHECK_EQ(dtc::decodeBank(fm.bankA, 0, v, d, 8), 2);
  CHECK_EQ(d[0].code, 10);
  CHECK_EQ(d[1].code, 24);
  live::Snapshot snap;
  CHECK(live::pollNative(s, snap) == LinkStatus::Ok);
  live::Decoded ld;
  live::decode(snap, prof::NormGroup::M104_CODED, false, nullptr, ld);
  CHECK(ld.v[live::P_RPM].hasValue);
  CHECK_EQ(link.mismatches, 0);
  CHECK_EQ(link.consumed(), t.size());
}

TEST(replay_detects_divergent_request) {
  std::vector<TraceLine> t = loadTrace(kGolden);
  ReplayLink link(t);
  TraceLog log;
  HfmSession s(link, log);
  s.connect();
  uint8_t d;
  // Trace expects F0 first; asking for page 20 must be flagged, not decoded.
  CHECK(s.prefix(0x20) != LinkStatus::Ok);
  CHECK_EQ(link.mismatches, 1);
  CHECK(s.state() == SessionState::Error);
  (void)d;
}
