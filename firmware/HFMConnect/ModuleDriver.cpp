#include "ModuleDriver.h"
#include <string.h>
namespace scanner {
#include "ModuleProfiles.inc"
struct FaultReference {uint16_t code;uint8_t sources;};
static constexpr FaultReference meReferences[]={
#include "MeFaultRefs.inc"
};
uint8_t meFaultSources(uint16_t code) {
  for(const auto& entry:meReferences) {
    if(entry.code==code)return entry.sources;
    if(entry.code>code)break;
  }
  return 0;
}
static constexpr Measure obdMeasures[]={
  {4,0,0,1,{}},{5,0,0,1,{}},{11,0,0,1,{}},{12,0,0,2,{}},
  {13,0,0,1,{}},{15,0,0,1,{}},{16,0,0,2,{}},{17,0,0,1,{}}
};
static constexpr Measure dutyMeasure[]={{0,0,0,0,{}}};
static Profile engineProfile(const char* module,const char* variant,EngineMode mode,const Measure* reads,uint8_t count) {
  Profile p{};p.module=module;p.variant=variant;p.mode=mode;p.measures=reads;p.measureCount=count;
  return p;
}
const Profile* profile(const Route& r) {
  static const Profile engines[]={
    engineProfile("ME-OBD","5 baud",EngineMode::ObdSlow,obdMeasures,8),
    engineProfile("ME-OBD","fast init",EngineMode::ObdFast,obdMeasures,8),
    engineProfile("KE","X11 PWM",EngineMode::KeDuty,dutyMeasure,1),
    engineProfile("KE","N3 M104/M119",EngineMode::KeBlink,nullptr,0),
    engineProfile("KE","M102.985 CA 1988",EngineMode::KeBlinkM102,nullptr,0),
    engineProfile("KE","M102.985 CA 1991",EngineMode::KeBlinkM102,nullptr,0)
  };
  if(r.protocol==Protocol::Obd || r.protocol==Protocol::Duty || r.protocol==Protocol::Blink) {
    for(const auto& p:engines)if(!strcmp(r.module,p.module) && !strcmp(r.variant,p.variant))return &p;
    return nullptr;
  }
  for (const auto& p : profiles)
    if (!strcmp(r.module,p.module) && !strcmp(r.variant,p.variant)) return &p;
  return nullptr;
}
FrameError ModuleDriver::connect(const Profile& p,uint32_t baud) {
  close();
  if(p.obd()) {
    auto err=wire_.connectObd(p.mode==EngineMode::ObdFast,out_.pidMask);
    if(err==FrameError::Ok) {
      out_.obdResponding=true;out_.obdPeer=wire_.obdPeer();out_.obdKwp=wire_.obdKwp();
    }
    return err;
  }
  if(p.ke()) {
    if(!link_.open())return FrameError::Offline;
    if(p.mode==EngineMode::KeDuty ? !link_.supportsDuty() : !link_.supportsKeBlink()) {close();return FrameError::Unsupported;}
    passiveReady_=true;return FrameError::Ok;
  }
  if (p.classicIdBytes) {
    LinkStatus st=words_.connect(1);
    if (st==LinkStatus::Ok) st=words_.readIdent(out_.ident,p.classicIdBytes);
    if (st!=LinkStatus::Ok) {close();return st==LinkStatus::LinkDown ? FrameError::Offline : FrameError::Sync;}
    bool allEcho=true;
    for(unsigned i=0;i<p.classicIdBytes;++i) if(out_.ident[i]!=i) allEcho=false;
    if(allEcho) {close();return FrameError::Echo;}
    out_.identLen=p.classicIdBytes;out_.identityValid=true;return FrameError::Ok;
  }
  auto e=p.iso ? wire_.connectIso(p.initAddress==0xff ? p.address : p.initAddress,p.tester,p.slow,p.address,p.functional,p.isoBaud,p.start) :
                 wire_.connectKs(p.address,p.fastKs ? 9600 : baud,p.init,p.fastKs);
  if (e!=FrameError::Ok) return e;
  if (p.iso) {
    const bool kwp=p.start==IsoStart::D2b;
    const uint8_t id[]={uint8_t(kwp ? 0x1a : 0x3c),uint8_t(kwp ? 0x86 : 0)};
    e=wire_.request(id,sizeof id,id[0]+0x40,wire_.isoTiming());
    if(e==FrameError::Ok && (wire_.size()<2 || wire_.data()[0]!=id[1]))e=FrameError::Reply;
  }
  size_t skip=p.iso ? 1 : 0;
  if (e==FrameError::Ok && wire_.size()-skip>sizeof out_.ident) e=FrameError::Length;
  if (e==FrameError::Ok) {
    out_.identLen=wire_.size()-skip;
    memcpy(out_.ident,wire_.data()+skip,out_.identLen);
    if(p.start==IsoStart::Aid) {
      const uint8_t id[]={0x3c,1};
      e=wire_.request(id,sizeof id,0x7c,wire_.isoTiming());
      if(e==FrameError::Ok && (wire_.size()<2 || wire_.data()[0]!=1))e=FrameError::Reply;
      if(e==FrameError::Ok && wire_.size()-1>sizeof out_.ident-out_.identLen)e=FrameError::Length;
      if(e==FrameError::Ok) {
        out_.secondIdOffset=out_.identLen;
        memcpy(out_.ident+out_.identLen,wire_.data()+1,wire_.size()-1);
        out_.identLen+=wire_.size()-1;
      }
    }
    out_.identityValid=e==FrameError::Ok;
  }
  if(e!=FrameError::Ok)close();
  return e;
}
FrameError ModuleDriver::keepAlive(const Profile& p) {
  if(p.ke())return ready() ? FrameError::Ok : FrameError::Offline;
  if(p.obd()) {
    uint8_t mask[4]{};
    auto err=wire_.obdCurrent(0,mask,sizeof mask);
    if(err!=FrameError::Ok)close();
    return err;
  }
  if(p.classicIdBytes) {
    uint8_t value=0;
    return words_.readIdent(&value,1)==LinkStatus::Ok && value==out_.ident[0] ? FrameError::Ok : FrameError::Reply;
  }
  if (p.iso) {
    uint8_t request=p.start==IsoStart::D2b ? 0x3e : 0x3f;
    return wire_.request(&request,1,request+0x40,wire_.isoTiming());
  }
  uint8_t status[]={0x50,1};
  auto result=wire_.request(status,2,0xd0,p.status);
  if (result!=FrameError::Ok) return result;
  if (!wire_.size()) return FrameError::Length;
  uint8_t state=wire_.data()[0];
  return state==8 || state==0x72 || state==0x74 || state==0x78 ? FrameError::Negative : FrameError::Ok;
}
FrameError ModuleDriver::nextBlink(bool m102) {
  out_.blinkQuietEnd=false;
  if(out_.faultsValid) {out_.codeCount=0;out_.faultsValid=false;}
  bool idle=false;
  uint8_t code=link_.readKeBlink(idle,m102);out_.lastBlink=code;
  if(m102 && idle && !code && out_.codeCount && link_.isOpen()) {
    out_.faultsValid=true;out_.blinkQuietEnd=true;return FrameError::Ok;
  }
  if(m102 && (code>12 || (code && out_.codeCount && code<=out_.codes[out_.codeCount-1]))) {
    out_.codeCount=out_.lastBlink=0;return FrameError::Reply;
  }
  if(!code) {out_.codeCount=0;return link_.isOpen() ? FrameError::Timeout : FrameError::Offline;}
  if(code==1 && !out_.codeCount) {out_.faultsValid=true;return FrameError::Ok;}
  if(out_.codeCount && code==out_.codes[0]) {out_.faultsValid=true;return FrameError::Ok;}
  for(unsigned i=0;i<out_.codeCount;++i)if(out_.codes[i]==code) {out_.codeCount=0;out_.lastBlink=0;return FrameError::Reply;}
  if(code==1 || out_.codeCount>=64) {out_.codeCount=0;out_.lastBlink=0;return FrameError::Reply;}
  out_.codes[out_.codeCount++]=code;
  return FrameError::Ok;
}
FrameError ModuleDriver::faults(const Profile& p) {
  if(p.blink())return nextBlink(p.mode==EngineMode::KeBlinkM102);
  out_.faultsValid=false; out_.codeCount=0;
  if(!p.canReadFaults())return FrameError::Unsupported;
  if(p.obd()) {
    if(!(out_.pidMask&0x80000000u))return FrameError::Unsupported;
    auto err=wire_.obdFaults(out_.codes,64,out_.codeCount);
    out_.faultsValid=err==FrameError::Ok;
    if(!wire_.ready())close();
    return err;
  }
  if(p.fastKs) {auto status=keepAlive(p);if(status!=FrameError::Ok)return status;}
  uint8_t payload[]={uint8_t(p.iso ? 0xa1 : 0x10),0};
  for (unsigned i=0;i<=64;++i) {
    FrameError e;
    if (p.iso && i) e=wire_.receiveNext(0xe1,wire_.isoTiming());
    else e=wire_.request(payload,p.iso ? 1 : 2,p.iso ? 0xe1 : 0x90,p.iso ? wire_.isoTiming() : p.fault);
    if(e!=FrameError::Ok) return e;
    const auto* data=wire_.data(); size_t n=wire_.size();
    if (p.iso ? n>=2 && data[0]==0xff && data[1]==0xff : n>=1 && data[0]==0x28) {
      out_.faultsValid=true;return FrameError::Ok;
    }
    if (i>=64) return FrameError::Limit;
    if (p.iso) {
      if(n<3) return FrameError::Length;
      out_.codes[i]=(uint16_t(data[0])<<8)|data[1]; out_.details[i]=data[2];
    } else {
      if(n<2 || data[0]!=1) return FrameError::Reply;
      out_.codes[i]=data[1]; out_.details[i]=0;
    }
    ++out_.codeCount;
  }
  return FrameError::Limit;
}
FrameError ModuleDriver::verifyIdentity(const Profile& p) {
  if(!p.iso || !p.clearFaultMemory || !out_.identityValid) return FrameError::Reply;
  const uint8_t id[]={0x3c,0};
  auto error=wire_.request(id,sizeof id,0x7c,wire_.isoTiming());
  if(error==FrameError::Ok && (wire_.size()!=size_t(out_.identLen)+1 || wire_.data()[0] ||
                               memcmp(wire_.data()+1,out_.ident,out_.identLen))) error=FrameError::Address;
  if(error!=FrameError::Ok) close();
  return error;
}
FrameError ModuleDriver::clearFaults(const Profile& p) {
  if(!p.iso || !p.clearFaultMemory || !out_.identityValid || !out_.faultsValid || !out_.codeCount) return FrameError::Reply;
  out_.faultsValid=false;out_.codeCount=0;
  const uint8_t clear=0x14;
  auto error=wire_.request(&clear,1,0x54,wire_.isoTiming(),CommandRisk::Service);
  if(error==FrameError::Ok && wire_.size()!=0) return FrameError::Reply;
  return error;
}
FrameError ModuleDriver::information(const Profile& p,unsigned pid) {
  if(!p.obd() || (pid!=2 && pid!=4 && pid!=6))return FrameError::Unsupported;
  if(pid==2)out_.obdVin[0]=0;
  if(pid==4)out_.calibrationCount=0;
  if(pid==6)out_.cvnCount=0;
  uint8_t raw[64]{},len=0;
  const auto err=wire_.obdInformation(uint8_t(pid),raw,len);
  if(err!=FrameError::Ok) {if(!wire_.ready())close();return err;}
  if(pid==2) {
    if(len!=20 || raw[0] || raw[1] || raw[2])return FrameError::Reply;
    for(unsigned i=3;i<len;++i) {
      const uint8_t c=raw[i];
      if(!((c>='0' && c<='9') || (c>='A' && c<='Z' && c!='I' && c!='O' && c!='Q')))return FrameError::Reply;
    }
    memcpy(out_.obdVin,raw+3,17);out_.obdVin[17]=0;
  } else if(pid==4) {
    for(unsigned i=0;i<len/16;++i) {
      auto* field=raw+i*16;
      unsigned n=16;
      while(n && (field[n-1]==0 || field[n-1]==' '))--n;
      if(!n)return FrameError::Reply;
      for(unsigned j=0;j<n;++j)if(field[j]<0x20 || field[j]>0x7e)return FrameError::Reply;
      memcpy(out_.calibrations[i],field,n);out_.calibrations[i][n]=0;
    }
    out_.calibrationCount=len/16;
  } else {
    for(unsigned i=0;i<len/4;++i) {
      const auto* b=raw+i*4;
      out_.cvns[i]=(uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(uint32_t(b[2])<<8)|b[3];
    }
    out_.cvnCount=len/4;
  }
  return FrameError::Ok;
}
FrameError ModuleDriver::measurement(const Profile& p,unsigned index) {
  out_.rawLen=0;
  if(index>=p.measureCount) return FrameError::Reply;
  if(p.start==IsoStart::D2b) {
    const uint8_t request[]={0x21,p.measures[index].at};
    auto err=wire_.request(request,2,0x61,wire_.isoTiming());
    if(err!=FrameError::Ok)return err;
    if(wire_.size()<2 || wire_.size()-1>sizeof out_.raw || wire_.data()[0]!=request[1])return FrameError::Reply;
    out_.rawLen=wire_.size()-1;out_.at=request[1];memcpy(out_.raw,wire_.data()+1,out_.rawLen);
    return FrameError::Ok;
  }
  if(p.mode==EngineMode::KeDuty) {
    out_.duty={};
    bool ok=link_.sampleDuty(out_.duty);
    return ok ? FrameError::Ok : link_.isOpen() ? FrameError::Reply : FrameError::Offline;
  }
  if(p.obd()) {
    const auto& m=p.measures[index];
    if(!(out_.pidMask&(uint32_t(1)<<(32-m.at))))return FrameError::Unsupported;
    auto err=wire_.obdCurrent(m.at,out_.raw,m.bytes);
    if(err==FrameError::Ok) {out_.rawLen=m.bytes;out_.at=m.at;}
    if(!wire_.ready())close();
    return err;
  }
  if(p.fastKs) {auto status=keepAlive(p);if(status!=FrameError::Ok)return status;}
  const auto& m=p.measures[index]; uint8_t req[]={0x30,1,m.lo,m.hi,m.bytes};
  auto e=wire_.request(req,sizeof req,0xb0,m.timing);
  if(e!=FrameError::Ok) return e;
  if(wire_.size()!=size_t(4+m.bytes) || memcmp(wire_.data(),req+1,4)) return FrameError::Reply;
  memcpy(out_.raw,wire_.data()+4,m.bytes);out_.rawLen=m.bytes;out_.at=m.at;
  return FrameError::Ok;
}
}
