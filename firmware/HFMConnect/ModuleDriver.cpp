#include "ModuleDriver.h"
#include <string.h>
namespace scanner {
#include "ModuleProfiles.inc"
const Profile* profile(const Route& r) {
  for (const auto& p : profiles)
    if (!strcmp(r.module,p.module) && !strcmp(r.variant,p.variant)) return &p;
  return nullptr;
}
FrameError ModuleDriver::connect(const Profile& p,uint32_t baud) {
  close();
  if (p.classicIdBytes) {
    LinkStatus st=words_.connect(1);
    if (st==LinkStatus::Ok) st=words_.readIdent(out_.ident,p.classicIdBytes);
    if (st!=LinkStatus::Ok) {close();return st==LinkStatus::LinkDown ? FrameError::Offline : FrameError::Sync;}
    bool allEcho=true;
    for(unsigned i=0;i<p.classicIdBytes;++i) if(out_.ident[i]!=i) allEcho=false;
    if(allEcho) {close();return FrameError::Echo;}
    out_.identLen=p.classicIdBytes;out_.identityValid=true;return FrameError::Ok;
  }
  auto e=p.iso ? wire_.connectIso(p.initAddress==0xff ? p.address : p.initAddress,p.tester,p.slow,p.address,p.functional,p.isoBaud) :
                 wire_.connectKs(p.address,p.fastKs ? 9600 : baud,p.init,p.fastKs);
  if (e!=FrameError::Ok) return e;
  if (p.iso) {
    const uint8_t id[]={0x3c,0};
    e=wire_.request(id,sizeof id,0x7c,wire_.isoTiming());
    if (e==FrameError::Ok && (wire_.size()<2 || wire_.data()[0]!=0)) e=FrameError::Reply;
  }
  size_t skip=p.iso ? 1 : 0;
  if (e==FrameError::Ok && wire_.size()-skip>sizeof out_.ident) e=FrameError::Length;
  if (e==FrameError::Ok) {
    out_.identLen=wire_.size()-skip;
    memcpy(out_.ident,wire_.data()+skip,out_.identLen);
    out_.identityValid=true;
  } else close();
  return e;
}
FrameError ModuleDriver::keepAlive(const Profile& p) {
  if(p.classicIdBytes) {
    uint8_t value=0;
    return words_.readIdent(&value,1)==LinkStatus::Ok && value==out_.ident[0] ? FrameError::Ok : FrameError::Reply;
  }
  if (p.iso) {
    uint8_t request=0x3f;
    return wire_.request(&request,1,0x7f,wire_.isoTiming());
  }
  uint8_t status[]={0x50,1};
  auto result=wire_.request(status,2,0xd0,p.status);
  if (result!=FrameError::Ok) return result;
  if (!wire_.size()) return FrameError::Length;
  uint8_t state=wire_.data()[0];
  return state==8 || state==0x72 || state==0x74 || state==0x78 ? FrameError::Negative : FrameError::Ok;
}
FrameError ModuleDriver::faults(const Profile& p) {
  out_.faultsValid=false; out_.codeCount=0;
  if (p.classicIdBytes) return FrameError::Reply;
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
FrameError ModuleDriver::measurement(const Profile& p,unsigned index) {
  out_.rawLen=0;
  if(index>=p.measureCount) return FrameError::Reply;
  if(p.fastKs) {auto status=keepAlive(p);if(status!=FrameError::Ok)return status;}
  const auto& m=p.measures[index]; uint8_t req[]={0x30,1,m.lo,m.hi,m.bytes};
  auto e=wire_.request(req,sizeof req,0xb0,m.timing);
  if(e!=FrameError::Ok) return e;
  if(wire_.size()!=size_t(4+m.bytes) || memcmp(wire_.data(),req+1,4)) return FrameError::Reply;
  memcpy(out_.raw,wire_.data()+4,m.bytes);out_.rawLen=m.bytes;out_.at=m.at;
  return FrameError::Ok;
}
}
