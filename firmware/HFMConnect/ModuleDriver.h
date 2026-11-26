#pragma once
#include "FrameSession.h"
#include "HfmSession.h"
#include "ScannerCatalog.h"
namespace scanner {
struct Measure { uint8_t at, lo, hi, bytes; Timing timing; };
enum class EngineMode : uint8_t { Native, ObdSlow, ObdFast, KeDuty, KeBlink };
struct Profile {
  const char* module; const char* variant; uint8_t address,tester;
  bool iso,slow; Timing init,status,fault;
  const Measure* measures; uint8_t measureCount; uint8_t classicIdBytes=0, initAddress=0xff; bool functional=false, fastKs=false;
  uint16_t isoBaud=10400;
  bool clearFaultMemory=false;
  EngineMode mode=EngineMode::Native;
  bool obd() const {return mode==EngineMode::ObdSlow || mode==EngineMode::ObdFast;}
  bool ke() const {return mode==EngineMode::KeDuty || mode==EngineMode::KeBlink;}
  bool canReadFaults() const {return !classicIdBytes && mode!=EngineMode::KeDuty;}
};
const Profile* profile(const Route& route);
uint8_t meFaultSources(uint16_t code);
struct Readout {
  bool obdResponding=false, obdKwp=false;
  uint8_t obdPeer=0, lastBlink=0;
  uint32_t pidMask=0;
  DutySample duty{};
  uint8_t ident[64]{}; uint8_t identLen=0;
  uint16_t codes[64]{}; uint8_t details[64]{}, codeCount=0; bool faultsValid=false;
  uint8_t raw[64]{},rawLen=0,at=0; bool identityValid=false;
};
class ModuleDriver {
 public:
  ModuleDriver(ByteLink& link,TraceLog& log) : link_(link),wire_(link,log),words_(link,log) {}
  FrameError connect(const Profile& p,uint32_t baud);
  FrameError faults(const Profile& p);
  FrameError verifyIdentity(const Profile& p);
  FrameError clearFaults(const Profile& p);
  FrameError measurement(const Profile& p,unsigned index);
  FrameError keepAlive(const Profile& p);
  void close() { wire_.close(); words_.disconnect(); passiveReady_=false; out_={}; }
  void setTiming(const LinkTiming& t) { words_.setTiming(t); }
  bool ready() const { return link_.isOpen() && (wire_.ready() || words_.ready() || passiveReady_); }
  const Readout& result() const {return out_;}
 private:
  ByteLink& link_;
  bool passiveReady_=false;
  FrameError nextBlink();
  FrameSession wire_;
  HfmSession words_;
  Readout out_;
};
}
