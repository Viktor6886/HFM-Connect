#pragma once
#include "FrameSession.h"
#include "HfmSession.h"
#include "ScannerCatalog.h"
namespace scanner {
struct Measure { uint8_t at, lo, hi, bytes; Timing timing; };
struct Profile {
  const char* module; const char* variant; uint8_t address,tester;
  bool iso,slow; Timing init,status,fault;
  const Measure* measures; uint8_t measureCount; uint8_t classicIdBytes=0, initAddress=0xff; bool functional=false, fastKs=false;
  uint16_t isoBaud=10400;
  bool clearFaultMemory=false;
};
const Profile* profile(const Route& route);
struct Readout {
  uint8_t ident[64]{}; uint8_t identLen=0;
  uint16_t codes[64]{}; uint8_t details[64]{}, codeCount=0; bool faultsValid=false;
  uint8_t raw[64]{},rawLen=0,at=0; bool identityValid=false;
};
class ModuleDriver {
 public:
  ModuleDriver(ByteLink& link,TraceLog& log) : wire_(link,log),words_(link,log) {}
  FrameError connect(const Profile& p,uint32_t baud);
  FrameError faults(const Profile& p);
  FrameError verifyIdentity(const Profile& p);
  FrameError clearFaults(const Profile& p);
  FrameError measurement(const Profile& p,unsigned index);
  FrameError keepAlive(const Profile& p);
  void close() { wire_.close(); words_.disconnect(); out_={}; }
  void setTiming(const LinkTiming& t) { words_.setTiming(t); }
  bool ready() const { return wire_.ready() || words_.ready(); }
  const Readout& result() const {return out_;}
 private:
  FrameSession wire_;
  HfmSession words_;
  Readout out_;
};
}
