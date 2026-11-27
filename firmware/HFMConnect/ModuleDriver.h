#pragma once
#include "FrameSession.h"
#include "HfmSession.h"
#include "ScannerCatalog.h"
namespace scanner {
struct Measure { uint8_t at, lo, hi, bytes; Timing timing; };
enum class EngineMode : uint8_t { Native, ObdSlow, ObdFast, KeDuty, KeBlink, KeBlinkM102 };
struct Profile {
  const char* module; const char* variant; uint8_t address,tester;
  bool iso,slow; Timing init,status,fault;
  const Measure* measures; uint8_t measureCount; uint8_t classicIdBytes=0, initAddress=0xff; bool functional=false, fastKs=false;
  uint16_t isoBaud=10400;
  bool clearFaultMemory=false;
  EngineMode mode=EngineMode::Native;
  IsoStart start=IsoStart::Direct;
  const char* isoName() const {return start==IsoStart::D2b ? "KWP2000" : "GL_ISO2";}
  bool obd() const {return mode==EngineMode::ObdSlow || mode==EngineMode::ObdFast;}
  bool blink() const {return mode==EngineMode::KeBlink || mode==EngineMode::KeBlinkM102;}
  bool ke() const {return mode==EngineMode::KeDuty || blink();}
  bool canReadFaults() const {return !classicIdBytes && mode!=EngineMode::KeDuty && start!=IsoStart::D2b;}
};
const Profile* profile(const Route& route);
uint8_t meFaultSources(uint16_t code);
struct Readout {
  bool obdResponding=false, obdKwp=false, blinkQuietEnd=false;
  uint8_t obdPeer=0, lastBlink=0;
  uint32_t pidMask=0;
  char obdVin[18]{},calibrations[4][17]{};
  uint32_t cvns[4]{};
  uint8_t calibrationCount=0,cvnCount=0;
  DutySample duty{};
  uint8_t ident[64]{}; uint8_t identLen=0,secondIdOffset=0;
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
  FrameError information(const Profile& p,unsigned pid);
  FrameError keepAlive(const Profile& p);
  void close() { wire_.close(); words_.disconnect(); passiveReady_=false; out_={}; }
  void setTiming(const LinkTiming& t) { words_.setTiming(t); }
  bool ready() const { return link_.isOpen() && (wire_.ready() || words_.ready() || passiveReady_); }
  const Readout& result() const {return out_;}
 private:
  ByteLink& link_;
  bool passiveReady_=false;
  FrameError nextBlink(bool m102);
  FrameSession wire_;
  HfmSession words_;
  Readout out_;
};
}
