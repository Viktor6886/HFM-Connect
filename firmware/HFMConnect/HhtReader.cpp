#include "HhtReader.h"
#include <string.h>
namespace hht {
struct Entry {
  const uint8_t* tpl; uint8_t bytes,length,descriptor,flags,extraCount,extra[6];
};
struct Screen { uint16_t conversion,parameter; uint8_t index[2]; };
#include "HhtScreens.inc"
bool Reader::exists(uint16_t i) { return i==125 || i==126 || (i<sizeof(screens)/sizeof(screens[0]) && (screens[i].index[0] || screens[i].index[1])); }
// This entry point is private. Wire words originate exclusively from the ROM
// reader and validated screen table. A browser can only select a screen ID.
LinkStatus Reader::word(HfmSession& s,uint8_t value,bool mark,uint8_t& out) {
  if (!s.ready() || s.writeArmed() || (!mark && (!s.pageValid_ || s.pageIsWrite_))) return LinkStatus::NotAllowed;
  LinkStatus st=s.xchg(value,mark ? Parity::Mark : Parity::Space,mark ? TracePhase::Prefix : TracePhase::Selector,CommandRisk::Read,out);
  if (st!=LinkStatus::Ok) return s.fail(st);
  if (mark) { s.page_=value; s.pageValid_=true; s.pageIsWrite_=false; }
  return st;
}
LinkStatus Reader::loadRom(HfmSession& s,const EcuIdent& id) {
  if (!id.extPresent) return LinkStatus::NotAllowed;
  uint8_t page=id.romPage;
  const unsigned sizes[]={50,50,50,44},dest[]={0,0xcc,0x194,0x25c};
  for (unsigned chunk=0;chunk<4;++chunk) {
    uint8_t request[224]{},reply[224]{};
    unsigned first=chunk==0,extra=0,roll=0;
    if(first) {request[0]=2; request[1]=1; request[2]=page;}
    for(unsigned i=0;i<sizes[chunk]+extra;++i) {
      unsigned pos=(i+extra+first)*4;
      uint8_t sel=uint8_t(id.descAddr+50*chunk+i);
      request[pos]=0x80;request[pos+1]=1;request[pos+2]=sel;
      if(sel==255) { ++extra; ++page; roll=i; request[pos+4]=2; request[pos+5]=1; request[pos+6]=page; }
    }
    unsigned n=(sizes[chunk]+extra+first)*4;
    for(unsigned i=0;i<n;i+=2) {
      auto e=word(s,request[i],request[i+1]!=0,reply[i]);
      if(e!=LinkStatus::Ok) return e;
    }
    if(extra) {
      unsigned pos=(roll+1+first)*4;
      memmove(reply+pos,reply+pos+4,n-pos-4);
    }
    memcpy(rom_+dest[chunk],reply,(sizes[chunk]+first)*4);
  }
  romReady_=true; return LinkStatus::Ok;
}
LinkStatus Reader::read(HfmSession& s,const EcuIdent& id,uint16_t screen,Reading& result) {
  result={}; result.screen=screen;
  if(!exists(screen) || !id.hhtCylinders || !s.ready()) return LinkStatus::NotAllowed;
  const bool b=id.vendorId==1;
  if(screen==125 || screen==126) {
    const unsigned cylinders=screen==125 ? 6 : 4;
    if(id.hhtCylinders!=cylinders || id.diagIndexRaw<0x45) return LinkStatus::NotAllowed;
    uint8_t raw[64]{},first=0; uint16_t mask=0;
    auto st=readIndex(s,id,screen,raw,mask,first);
    if(st!=LinkStatus::Ok) return st;
    const unsigned complete=(1u<<cylinders)-1;
    if((mask&complete)==complete) result.words=smoothness(raw,b,cylinders);
    return LinkStatus::Ok;
  }
  const auto& sc=screens[screen];
  unsigned idx=sc.index[b]; if(!idx) return LinkStatus::NotAllowed;
  uint8_t raw[64]{},first=0; uint16_t valid=0;
  auto st=readIndex(s,id,idx,raw,valid,first);
  if (st!=LinkStatus::Ok) return st;
  const auto d=id.diagIndexRaw;
  result.words=convert(sc.conversion,raw,b,d,id.diagSubRaw,id.hhtCylinders,sc.parameter);
  result.words.valid &= valid;
  return LinkStatus::Ok;
}
LinkStatus Reader::cranking(HfmSession& s,const EcuIdent& id,Words& result) {
  result={};
  const unsigned cylinders=id.hhtCylinders;
  if(!s.ready() || (cylinders!=4 && cylinders!=6) || id.diagIndexRaw<0x45 ||
     (id.vendorId!=1 && id.vendorId!=2))return LinkStatus::NotAllowed;
  uint8_t raw[64]{},first=0;uint16_t valid=0;
  const auto st=readIndex(s,id,cylinders==4 ? 126 : 125,raw,valid,first);
  if(st!=LinkStatus::Ok)return st;
  const unsigned needed=(1u<<cylinders)-1;
  if((valid&needed)!=needed)return LinkStatus::NotAllowed;
  result=crankingSpeeds(raw,id.vendorId==1,cylinders);
  return LinkStatus::Ok;
}
LinkStatus Reader::readIndex(HfmSession& s,const EcuIdent& id,unsigned idx,uint8_t (&raw)[64],uint16_t& valid,uint8_t& firstValue) {
  const bool b=id.vendorId==1;
  const Entry& e=entries[idx][b]; uint8_t request[64]{},reply[64]{},src[32]{},dst[32]{};
  unsigned n=0,ns=0,nd=0; valid=0xffff;
  uint8_t d=id.diagIndexRaw;
  if(d<0x45) {
    if(!e.length || !e.tpl || e.bytes!=e.length || e.length>sizeof request) return LinkStatus::NotAllowed;
    memcpy(request,e.tpl,e.bytes); n=e.bytes;
  } else {
    if(!e.descriptor) return LinkStatus::NotAllowed;
    if(!romReady_) {auto st=loadRom(s,id); if(st!=LinkStatus::Ok) return st;}
    for(unsigned i=0;i<e.bytes;i+=2) if(e.tpl[i+1]==0) dst[nd++]=i;
    valid=0;
    auto add=[&](unsigned value,bool mark) {
      if(n+2>sizeof request) return false;
      if(!mark) src[ns++]=n;
      request[n++]=uint8_t(value); request[n++]=mark; return true;
    };
    for(unsigned k=0;k<=e.extraCount;++k) {
      const Entry& a=k ? entries[e.extra[k-1]][b] : e;
      if(!a.descriptor || a.descriptor*8+2>=int(sizeof rom_)) return LinkStatus::NotAllowed;
      auto sel=rom_[a.descriptor*8-2],typ=rom_[a.descriptor*8+2];
      if(sel || typ) valid|=1u<<k;
      bool bare=!typ && (!k || b);
      uint8_t page=typeMap[b][typ];
      if(a.flags&2) {
        if(bare || b) {
          if(!bare && (page==255 || !add(page,true))) return LinkStatus::NotAllowed;
          if(!add(sel+1,false)||!add(d<0x46 ? 0xb0 : 0xb4,false)) return LinkStatus::NotAllowed;
        } else if(!add(0,true)||!add(d>=0x50 && typ>=0xa0 && typ<=0xbf ? typ-0x20 : typ,false)||
                  !add(0x90,true)||!add(sel,false)||!add(0x80,false)) return LinkStatus::NotAllowed;
      } else {
        if(!bare && (page==255 || !add(page,true))) return LinkStatus::NotAllowed;
        if(!add(sel,false)) return LinkStatus::NotAllowed;
        if(a.flags&1) {
          if(!bare && !add(page,true)) return LinkStatus::NotAllowed;
          if(!add(sel+1,false)) return LinkStatus::NotAllowed;
        }
      }
    }
    if(ns>nd) return LinkStatus::NotAllowed;
  }
  for(unsigned i=0;i<n;i+=2) {
    auto st=word(s,request[i],request[i+1]!=0,reply[i]); if(st!=LinkStatus::Ok) return st;
  }
  unsigned matched=0,selectors=0; int firstSelector=-1; bool distinct=false;
  for(unsigned i=0;i<n;i+=2) if(!request[i+1]) {
    if (!selectors) {firstValue=reply[i];firstSelector=request[i];}
    if (request[i]!=firstSelector) distinct=true;
    ++selectors; if(reply[i]==request[i]) ++matched;
  }
  const bool echoes=selectors>=2 && distinct && matched==selectors;
  memcpy(raw,reply,n);
  if(d>=0x45) for(unsigned i=0;i<ns;++i) raw[dst[i]]=reply[src[i]];
  if(idx==124 || idx==143 || idx==144) valid=0xff;
  unsigned offset=64;
  switch(idx) {
    case 4: offset=b ? 2 : 14; break;
    case 3: case 7: offset=b ? 8 : 14; break;
    case 8: case 46: offset=b ? 4 : 14; break;
    case 76: case 80: offset=b ? 6 : 14; break;
    case 77: case 81: offset=b ? 4 : 10; break;
  }
  if(echoes || (offset<64 && !raw[offset])) valid=0;
  return LinkStatus::Ok;
}
LinkStatus Reader::coding(HfmSession& s,const EcuIdent& id,uint8_t (&code)[3],uint8_t& flags,uint8_t& ack) {
  if(!id.hhtCylinders || !s.ready()) return LinkStatus::NotAllowed;
  bool b=id.vendorId==1;
  if(id.diagIndexRaw<0x45) {
    const uint8_t bosch[]={0x27,2,0x24,0xf0}, vdo[]={0x10,2,0x22,0x22,0x10,3,0x22,0x23};
    const auto* seq=b ? bosch : vdo;
    uint8_t got[8]{};
    for(unsigned i=0;i<(b ? sizeof bosch : sizeof vdo);++i) {
      auto e=word(s,seq[i],i%2==0,got[i]); if(e!=LinkStatus::Ok) return e;
    }
    flags=b ? got[1] : (got[1]|got[3]); ack=b ? got[3] : (got[5]|got[7]);
    if (b ? (ack&3)!=0 : (ack&3)!=3) return LinkStatus::BadArgReply;
  } else {
    for(unsigned i=0;i<(id.diagIndexRaw<0x50 ? 4u : 3u);++i) {
      uint8_t data[64]{},first=0;uint16_t mask=0;
      auto e=readIndex(s,id,i==3 ? 55 : 47+i,data,mask,first);
      if(e!=LinkStatus::Ok || !(mask&1)) return e==LinkStatus::Ok ? LinkStatus::NotAllowed : e;
      if(i==3) flags=first; else code[i]=first;
    }
  }
  return LinkStatus::Ok;
}

}
