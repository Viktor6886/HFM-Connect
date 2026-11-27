#include "FrameSession.h"
#include <string.h>

namespace scanner {
static uint8_t obdChecksum(const uint8_t* data,size_t length) {
  uint8_t sum=0;
  while(length--) sum+=*data++;
  return sum;
}

bool FrameSession::obdByte(uint8_t& value,uint16_t timeout) {
  if(!link_.frameRead(value,timeout))return false;
  trace_.append(link_.nowUs(),TracePhase::FrameRx,Parity::None,0,0,value,LinkStatus::Ok,CommandRisk::Read,1);
  return true;
}

// Both public OBD K-line protocols address the emissions system functionally.
FrameError FrameSession::obdSend(const uint8_t* payload,size_t length) {
  if(!length || length>2)return FrameError::Length;
  uint8_t bytes[6]={uint8_t(obdKwp_ ? 0xc0|length : 0x68),
                    uint8_t(obdKwp_ ? 0x33 : 0x6a),0xf1};
  memcpy(bytes+3,payload,length);
  bytes[3+length]=obdChecksum(bytes,3+length);
  for(size_t i=0;i<length+4;++i) {
    if(i)link_.sleepMs(5);
    auto result=send(bytes[i],100);
    if(result!=FrameError::Ok)return result;
  }
  return FrameError::Ok;
}

FrameError FrameSession::obdFrame(uint8_t first,uint8_t& source,const uint8_t*& payload,size_t& length) {
  auto* b=rx_.bytes;
  b[0]=first;
  size_t n=1, header=3;
  if(obdKwp_) {
    if((first&0xc0)!=0x80)return FrameError::Reply;
    length=first&63;
    if(!length)header=4;
    while(n<header) {if(!obdByte(b[n],20))return FrameError::Length; ++n;}
    if(b[1]!=0xf1)return FrameError::Address;
    if(header==4)length=b[3];
    if(!length || length>7)return FrameError::Length;
    while(n<header+length+1) {if(!obdByte(b[n],20))return FrameError::Length; ++n;}
  } else {
    if(first!=0x48)return FrameError::Reply;
    // ISO 9141-2 has no length byte. P1 silence or its 11-byte limit ends a frame.
    while(n<11 && obdByte(b[n],20))++n;
    if(n<5)return FrameError::Length;
    if(b[1]!=0x6b)return FrameError::Address;
    length=n-4;
  }
  if(obdChecksum(b,n-1)!=b[n-1])return FrameError::Sum;
  source=b[2];payload=b+header;
  return FrameError::Ok;
}

FrameError FrameSession::connectObd(bool fast,uint32_t& mask) {
  close();mask=0;obdPeer_=0;obdKwp_=fast;risk_=CommandRisk::Read;
  if(!link_.open() || !link_.frameBaud(10400)){close();return FrameError::Offline;}
  FrameError err=FrameError::Ok;
  uint8_t b=0,k1=0,k2=0;
  if(fast) {
    link_.sleepMs(350);
    if(!link_.frameBreak(true))err=FrameError::Offline;
    link_.sleepMs(25);
    if(!link_.frameBreak(false))err=FrameError::Offline;
    link_.frameDrain();link_.sleepMs(25);
    const uint8_t start=0x81;
    if(err==FrameError::Ok)err=obdSend(&start,1);
    bool answered=false;
    for(unsigned i=0;err==FrameError::Ok && i<8;++i) {
      if(!obdByte(b,100))break;
      const uint8_t* p;size_t length;uint8_t source;
      err=obdFrame(b,source,p,length);
      if(err==FrameError::Ok && (length!=3 || p[0]!=0xc1 || p[2]!=0x8f))err=FrameError::Sync;
      if(err==FrameError::Ok)answered=true;
      if(i==7)err=FrameError::Limit;
    }
    if(!answered && err==FrameError::Ok)err=FrameError::Sync;
  } else {
    err=fiveBaud(0x33,350,200);
    if(err==FrameError::Ok && (!obdByte(b,300) || b!=0x55 || !obdByte(k1,50) || !obdByte(k2,50)))err=FrameError::Sync;
    if(err==FrameError::Ok) {
      obdKwp_=k2==0x8f;
      if(!obdKwp_ && (k1!=k2 || (k1!=0x08 && k1!=0x94)))err=FrameError::Unsupported;
    }
    if(err==FrameError::Ok){link_.sleepMs(30);err=send(uint8_t(~k2),100);}
    if(err==FrameError::Ok && (!obdByte(b,100) || b!=0xcc))err=FrameError::Sync;
  }
  if(err==FrameError::Ok) {
    uint8_t data[4]{},count=0;
    err=obdCollect(1,0,data,sizeof data,nullptr,0,count,true);
    if(err==FrameError::Ok)for(auto v:data)mask=(mask<<8)|v;
  }
  if(err==FrameError::Ok)ready_=true;else close();
  return err;
}

// Select a responder only if the engine can be distinguished. Subsequent reads
// drain other responders, but never merge their values or trouble codes.
FrameError FrameSession::obdCollect(uint8_t service,uint8_t pid,uint8_t* data,size_t length,
                                   uint16_t* codes,size_t cap,uint8_t& count,bool discover) {
  count=0;const uint8_t rq[]={service,pid};
  link_.sleepMs(55);
  auto err=obdSend(rq,service==1 ? 2 : 1);
  if(err!=FrameError::Ok)return err;
  bool found=false,negative=false;
  unsigned candidates=0,engines=0;uint8_t peer=0,selected[4]{};
  for(unsigned i=0;i<40;++i) {
    uint8_t first=0,source=0;size_t n=0;const uint8_t* p=nullptr;
    if(!obdByte(first,100)) {
      if(!link_.isOpen())return FrameError::Offline;
      if(discover && candidates>1 && engines!=1)return FrameError::Ambiguous;
      if(discover && found){obdPeer_=peer;memcpy(data,selected,4);}
      return found ? FrameError::Ok : negative ? FrameError::Negative : FrameError::Timeout;
    }
    err=obdFrame(first,source,p,n);
    if(err!=FrameError::Ok)return err;
    if(!discover && source!=obdPeer_)continue;
    if(n==3 && p[0]==0x7f && p[1]==service){negative=true;continue;}
    if(p[0]!=uint8_t(service+0x40))return FrameError::Reply;
    if(service==1) {
      if(n!=length+2 || p[1]!=pid)return FrameError::Reply;
      if(discover) {
        ++candidates;
        // PID 05 (coolant) and 0C (RPM) in the 01 00 capability bitmap.
        bool engine=(p[2]&0x08) && (p[3]&0x10);
        if(engine)++engines;
        if(candidates==1 || engine){peer=source;memcpy(selected,p+2,4);}
      } else {
        if(found)return FrameError::Reply;
        memcpy(data,p+2,length);
      }
    } else {
      if(n<3 || (n-1)%2)return FrameError::Length;
      for(size_t j=1;j<n;j+=2) {
        uint16_t code=uint16_t(p[j])<<8|p[j+1];
        if(!code)continue;
        for(unsigned k=0;k<count;++k)if(codes[k]==code)return FrameError::Reply;
        if(count>=cap)return FrameError::Limit;
        codes[count++]=code;
      }
    }
    found=true;
  }
  return FrameError::Limit;
}

FrameError FrameSession::obdCurrent(uint8_t pid,uint8_t* out,size_t len) {
  if(!ready_)return FrameError::Offline;
  uint8_t count=0;
  auto err=obdCollect(1,pid,out,len,nullptr,0,count,false);
  if(err!=FrameError::Ok && err!=FrameError::Negative)close();
  return err;
}
FrameError FrameSession::obdInfoFrames(uint8_t pid,unsigned frames,bool counter,uint8_t* bytes) {
  const uint8_t request[]={9,pid};
  link_.sleepMs(55);
  auto err=obdSend(request,sizeof request);
  if(err!=FrameError::Ok)return err;
  unsigned received=0;
  bool negative=false;
  for(unsigned i=0;i<80;++i) {
    uint8_t first=0,source=0;size_t length=0;const uint8_t* payload=nullptr;
    if(!obdByte(first,100)) {
      if(!link_.isOpen())return FrameError::Offline;
      if(negative)return received ? FrameError::Reply : FrameError::Negative;
      if(!received)return FrameError::Timeout;
      return received==frames ? FrameError::Ok : FrameError::Length;
    }
    err=obdFrame(first,source,payload,length);
    if(err!=FrameError::Ok)return err;
    if(source!=obdPeer_)continue;
    if(length==3 && payload[0]==0x7f && payload[1]==9) {negative=true;continue;}
    if(length!=(counter ? 3u : 7u) || received>=frames)return FrameError::Length;
    if(payload[0]!=0x49 || payload[1]!=pid)return FrameError::Reply;
    if(counter)bytes[0]=payload[2];
    else {
      if(payload[2]!=received+1)return FrameError::Reply;
      memcpy(bytes+received*4,payload+3,4);
    }
    ++received;
  }
  close(); // A saturated bus may still contain packets from this request.
  return FrameError::Limit;
}

FrameError FrameSession::obdInformation(uint8_t pid,uint8_t (&bytes)[64],uint8_t& length) {
  length=0;
  if(!ready_)return FrameError::Offline;
  if(pid!=2 && pid!=4 && pid!=6)return FrameError::Unsupported;
  uint8_t frames=5;  // VIN is 17 characters, right-aligned in five packets.
  auto err=FrameError::Ok;
  if(pid!=2) {
    err=obdInfoFrames(pid-1,1,true,&frames);
    if(err==FrameError::Ok && (!frames || (pid==4 && frames%4)))err=FrameError::Length;
    if(err==FrameError::Ok && frames>(pid==4 ? 16 : 4))err=FrameError::Limit;
  }
  if(err==FrameError::Ok)err=obdInfoFrames(pid,frames,false,bytes);
  if(err==FrameError::Ok)length=frames*4;
  // An optional PID may time out or be rejected on an otherwise working ECU.
  else if(err!=FrameError::Timeout && err!=FrameError::Negative && err!=FrameError::Limit)close();
  return err;
}

FrameError FrameSession::obdFaults(uint16_t* out,size_t cap,uint8_t& count) {
  count=0;if(!ready_)return FrameError::Offline;
  uint8_t status[4]{};
  auto err=obdCurrent(1,status,sizeof status);
  if(err!=FrameError::Ok)return err;
  if((status[0]&127)>cap)return FrameError::Limit;
  err=obdCollect(3,0,nullptr,0,out,cap,count,false);
  if(err==FrameError::Ok && count!=(status[0]&127))err=FrameError::Length;
  if(err!=FrameError::Ok){count=0;if(err!=FrameError::Negative)close();}
  return err;
}
}
