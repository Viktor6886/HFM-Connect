#pragma once
#include "ByteLink.h"
#include "TraceLog.h"

namespace scanner {
enum class FrameKind : uint8_t { Ks, Iso };
enum class IsoStart : uint8_t { Direct, Aid, Lwr, D2b };
enum class FrameError : uint8_t { Ok, Offline, Timeout, Echo, Sync, Address, Length, Sum, Reply, Negative, Busy, Limit, Unsupported, Ambiguous };
const char* message(FrameError e);
struct Frame { uint8_t bytes[260]{}; size_t size = 0; };
struct Timing { uint16_t pause, gap, first, inter; };
FrameError checkFrame(FrameKind kind, const Frame& in, uint8_t ecu, uint8_t tester,
                      uint8_t command, size_t& start, size_t& count);
bool encodeFrame(FrameKind kind, uint8_t ecu, uint8_t tester, const uint8_t* payload,
                 size_t length, bool extended, Frame& out);
class FrameSession {
 public:
  FrameSession(ByteLink& link, TraceLog& trace) : link_(link), trace_(trace) {}
  FrameError connectKs(uint8_t addr, uint32_t baud, const Timing& init, bool fastAddress=false);
  FrameError connectIso(uint8_t addr, uint8_t tester, bool slow, uint8_t sessionAddress=0xff, bool functional=false,uint32_t baud=10400,IsoStart strategy=IsoStart::Direct);
  FrameError connectObd(bool fast,uint32_t& pidMask);
  FrameError obdCurrent(uint8_t pid,uint8_t* out,size_t length);
  FrameError obdFaults(uint16_t* out,size_t capacity,uint8_t& count);
  // Service 09 on ISO 9141-2 / ISO 14230-4. Each data frame carries four bytes.
  FrameError obdInformation(uint8_t pid,uint8_t (&bytes)[64],uint8_t& length);
  uint8_t obdPeer() const {return obdPeer_;}
  bool obdKwp() const {return obdKwp_;}
  FrameError request(const uint8_t* payload, size_t n, uint8_t expected, const Timing& times, CommandRisk risk=CommandRisk::Read);
  FrameError receiveNext(uint8_t expected,const Timing& times);
  void close();
  bool ready() const { return ready_; }
  const uint8_t* data() const { return rx_.bytes + start_; }
  size_t size() const { return count_; }
  uint8_t nrc() const { return nrc_; }
  Timing isoTiming() const { return iso_; }
 private:
  FrameError fastStart(bool allowSilentReply=false);
  FrameError negotiateTiming(uint8_t mode);
  FrameError send(uint8_t b, uint16_t timeout);
  FrameError receive(uint8_t& b, uint16_t timeout);
  FrameError frame(uint8_t expected, const Timing& t);
  FrameError obdSend(const uint8_t* payload,size_t length);
  FrameError obdFrame(uint8_t first,uint8_t& source,const uint8_t*& payload,size_t& length);
  FrameError obdInfoFrames(uint8_t pid,unsigned frames,bool counter,uint8_t* bytes);
  bool obdByte(uint8_t& value,uint16_t timeout);
  FrameError obdCollect(uint8_t service,uint8_t pid,uint8_t* data,size_t length,
                        uint16_t* codes,size_t capacity,uint8_t& count,bool discover);
  uint8_t obdPeer_=0;
  bool obdKwp_=false;
  FrameError fiveBaud(uint8_t octet, uint16_t idle, uint16_t bit);
  FrameError command(const uint8_t* p, size_t n, uint8_t expect, const Timing& times,bool allowSilentReply=false);
  ByteLink& link_;
  TraceLog& trace_;
  FrameKind kind_ = FrameKind::Ks;
  Frame rx_;
  size_t start_ = 0, count_ = 0;
  uint8_t address_ = 0, tester_ = 0xf2, nrc_ = 0;
  bool extended_ = false, ready_ = false, functionalStart_ = false;
  uint8_t startAddress_ = 0;
  CommandRisk risk_=CommandRisk::Read;
  Timing iso_{25, 7, 55, 20};
};
}
