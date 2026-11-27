#include "FrameSession.h"
#include <string.h>
namespace scanner {
const char* message(FrameError e) {
  static const char* const names[] = {"Готово", "Адаптер отключён", "Истекло время ответа",
    "Эхо не совпало", "Нет синхронизации", "Ответ от другого адреса", "Неверная длина ответа",
    "Ошибка контрольной суммы", "Неожиданный ответ ЭБУ", "ЭБУ отклонил запрос",
    "ЭБУ занят", "Достигнут предел ответа", "Режим не поддержан", "Ответили несколько ЭБУ двигателя"};
  return names[static_cast<unsigned>(e)];
}
static uint16_t sum(const uint8_t* p, size_t n) {
  uint16_t total = 0;
  while (n--) total += *p++;
  return total;
}
bool encodeFrame(FrameKind kind, uint8_t ecu, uint8_t tester, const uint8_t* p,
                 size_t n, bool extended, Frame& out) {
  out.size = 0;
  if (!p || !n || n > (kind == FrameKind::Ks ? 251u : 255u)) return false;
  auto* b = out.bytes;
  if (kind == FrameKind::Ks) {
    b[0] = ecu; b[1] = p[0]; b[2] = uint8_t(n + 4);
    memcpy(b + 3, p + 1, n - 1);
    uint16_t total = sum(b, n + 2);
    b[n+2] = uint8_t(total); b[n+3] = uint8_t(total >> 8); out.size = n+4;
  } else {
    size_t h = (extended || n > 63) ? 4 : 3;
    b[0] = h == 4 ? 0x80 : uint8_t(0x80 | n);
    b[1] = ecu; b[2] = tester;
    if (h == 4) b[3] = uint8_t(n);
    memcpy(b+h, p, n); b[h+n] = uint8_t(sum(b,h+n)); out.size = h+n+1;
  }
  return true;
}
FrameError checkFrame(FrameKind kind, const Frame& f, uint8_t ecu, uint8_t tester,
                      uint8_t cmd, size_t& off, size_t& count) {
  const auto* b = f.bytes; size_t n = f.size; off = count = 0;
  if (n < 5 || n > sizeof f.bytes) return FrameError::Length;
  if (kind == FrameKind::Ks) {
    if (b[2] != n) return FrameError::Length;
    if (sum(b,n-2) != (uint16_t(b[n-1]) << 8 | b[n-2])) return FrameError::Sum;
    if (b[0] != ecu) return FrameError::Address;
    if (b[1] != cmd) return FrameError::Reply;
    off = 3; count = n-5;
  } else {
    size_t len = b[0] & 63, h = len ? 3 : 4;
    if (!len) len = b[3];
    if ((b[0] & 0xc0) != 0x80 || !len || n != h+len+1) return FrameError::Length;
    if (uint8_t(sum(b,n-1)) != b[n-1]) return FrameError::Sum;
    if (b[1] != tester || b[2] != ecu) return FrameError::Address;
    if (b[h] == 0x7f && len == 3 && b[h+1] == uint8_t(cmd-0x40)) {
      off = h+1; count = 2; return FrameError::Negative;
    }
    if (b[h] != cmd) return FrameError::Reply;
    off = h+1; count = len-1;
  }
  return FrameError::Ok;
}
FrameError FrameSession::send(uint8_t b, uint16_t timeout) {
  uint8_t echo = 0;
  FrameError e = FrameError::Offline;
  bool got = false;
  if (link_.frameWrite(b)) {
    got = link_.frameRead(echo,timeout);
    e = !got ? FrameError::Timeout : echo == b ? FrameError::Ok : FrameError::Echo;
  }
  auto status = e == FrameError::Ok ? LinkStatus::Ok : e == FrameError::Echo ? LinkStatus::EchoMismatch :
                e == FrameError::Timeout ? LinkStatus::Timeout : LinkStatus::LinkDown;
  trace_.append(link_.nowUs(),TracePhase::FrameTx,Parity::None,b,echo,0,status,risk_,got ? 1 : 0);
  return e;
}
FrameError FrameSession::receive(uint8_t& b, uint16_t timeout) {
  b = 0; bool got = link_.frameRead(b,timeout);
  auto status = got ? LinkStatus::Ok : link_.isOpen() ? LinkStatus::Timeout : LinkStatus::LinkDown;
  trace_.append(link_.nowUs(),TracePhase::FrameRx,Parity::None,0,0,b,status,risk_,got ? 1 : 0);
  return got ? FrameError::Ok : status == LinkStatus::Timeout ? FrameError::Timeout : FrameError::Offline;
}
FrameError FrameSession::fiveBaud(uint8_t octet, uint16_t idle, uint16_t bit) {
  link_.sleepMs(idle);
  uint16_t word = (uint16_t(octet) << 1) | 0x200; // start + eight LSB-first bits + stop
  for (unsigned i=0;i<10;++i) {
    if (!link_.frameBreak(!(word & (1u<<i)))) { link_.frameBreak(false); return FrameError::Offline; }
    // Discard transceiver feedback as soon as the stop bit begins.
    if (i == 9) link_.frameDrain();
    link_.sleepMs(bit);
  }
  return FrameError::Ok;
}
FrameError FrameSession::frame(uint8_t expected,const Timing& t) {
  size_t need = 3;
  rx_.size = 0; start_ = count_ = 0;
  while (rx_.size < need) {
    auto e = receive(rx_.bytes[rx_.size],rx_.size ? t.inter : t.first);
    if (e != FrameError::Ok) return e;
    ++rx_.size;
    if (rx_.size == 3) {
      if (kind_ == FrameKind::Ks) need = rx_.bytes[2];
      else if ((rx_.bytes[0] & 0xc0) != 0x80) return FrameError::Length;
      else need = (rx_.bytes[0] & 63) ? 4+(rx_.bytes[0]&63) : 4;
      if (need < 4) return FrameError::Length;
    } else if (rx_.size == 4 && kind_ == FrameKind::Iso && !(rx_.bytes[0]&63)) need = 5+rx_.bytes[3];
  }
  return checkFrame(kind_,rx_,address_,tester_,expected,start_,count_);
}
FrameError FrameSession::command(const uint8_t* p,size_t n,uint8_t expect,const Timing& t,bool allowSilentReply) {
  Frame tx;
  nrc_ = 0; count_ = 0;
  const bool start = kind_==FrameKind::Iso && n==1 && p[0]==0x81;
  if (!encodeFrame(kind_,start ? startAddress_ : address_,tester_,p,n,extended_,tx)) return FrameError::Length;
  if (functionalStart_ && start) {
    tx.bytes[0]|=0x40;tx.bytes[tx.size-1]=uint8_t(sum(tx.bytes,tx.size-1));
  }
  for (unsigned attempt=0;attempt<2;++attempt) {
    link_.sleepMs(t.pause);
    for (size_t i=0;i<tx.size;++i) {
      if (i) link_.sleepMs(t.gap);
      auto e = send(tx.bytes[i],100);
      if (e != FrameError::Ok) return e;
    }
    Timing rxTime=t;
    for (unsigned pending=0;pending<4;++pending) {
      auto e=frame(expect,rxTime);
      if(allowSilentReply && !pending && e==FrameError::Timeout && !rx_.size)return FrameError::Ok;
      if (e!=FrameError::Negative) return e;
      nrc_=data()[1];
      if (nrc_==0x21) { if(risk_!=CommandRisk::Read) return FrameError::Busy; break; }
      if (nrc_!=0x78) return e;
      rxTime.first=2500;
      if (pending==3) return FrameError::Busy;
    }
  }
  return FrameError::Busy;
}
FrameError FrameSession::request(const uint8_t* p,size_t n,uint8_t expect,const Timing& t,CommandRisk risk) {
  if (!ready_) return FrameError::Offline;
  risk_=risk;
  auto e = command(p,n,expect,t);
  risk_=CommandRisk::Read;
  if (e!=FrameError::Ok && e!=FrameError::Negative && e!=FrameError::Busy) close();
  return e;
}
FrameError FrameSession::receiveNext(uint8_t expected,const Timing& times) {
  if (!ready_) return FrameError::Offline;
  auto e=frame(expected,times);
  if (e!=FrameError::Ok) close();
  return e;
}
void FrameSession::close() { ready_=false; count_=0; link_.close(); }
FrameError FrameSession::connectKs(uint8_t addr,uint32_t baud,const Timing& init,bool fastAddress) {
  close(); kind_=FrameKind::Ks; address_=addr; extended_=false;
  if (!link_.open() || !link_.frameBaud(baud)) { close(); return FrameError::Offline; }
  uint8_t odd=addr & 0x7f;
  if (!__builtin_parity(unsigned(odd))) odd|=0x80;
  FrameError e;
  if (fastAddress) { link_.sleepMs(init.pause); e=send(odd,100); }
  else e=fiveBaud(odd,init.pause,init.gap);
  uint8_t b=0;
  if (e==FrameError::Ok) e=receive(b,init.first);
  if (e==FrameError::Ok && b!=0x55) e=FrameError::Sync;
  for (int i=0;i<2 && e==FrameError::Ok;++i) e=receive(b,300);
  if (e==FrameError::Ok) e=frame(0x8f,{0,0,init.first,fastAddress ? uint16_t(1995) : uint16_t(300)});
  if (e==FrameError::Ok && count_!=13) e=FrameError::Length;
  if (e==FrameError::Ok) ready_=true; else close();
  return e;
}
FrameError FrameSession::fastStart(bool allowSilentReply) {
  link_.sleepMs(300);
  const bool low=link_.frameBreak(true);
  link_.sleepMs(25);
  const bool released=link_.frameBreak(false);
  if(!low || !released)return FrameError::Offline;
  link_.frameDrain();
  const uint8_t start=0x81;
  auto err=command(&start,1,0xc1,iso_,allowSilentReply);
  if(err==FrameError::Ok && count_!=2 && !(allowSilentReply && !rx_.size))return FrameError::Length;
  return err;
}
FrameError FrameSession::negotiateTiming(uint8_t mode) {
  const uint8_t timing[]={0x83,mode};
  auto err=command(timing,2,0xc3,iso_);
  if(err!=FrameError::Ok)return err;
  if(count_!=6 || data()[0]!=mode || !data()[2])return FrameError::Reply;
  const auto* d=data();
  iso_.first=uint16_t(d[2])*25;
  iso_.pause=(d[3]+1)/2+5;
  iso_.gap=(d[5]+1)/2;
  return FrameError::Ok;
}
FrameError FrameSession::connectIso(uint8_t addr,uint8_t tester,bool slow,uint8_t sessionAddress,bool functional,uint32_t baud,IsoStart strategy) {
  close(); kind_=FrameKind::Iso; startAddress_=addr; address_=sessionAddress==0xff ? addr : sessionAddress;
  tester_=tester; extended_=false;
  functionalStart_=functional;
  iso_={25,7,55,20};
  if (!link_.open() || !link_.frameBaud(baud)) { close(); return FrameError::Offline; }
  uint8_t k1=0,k2=0,b=0;
  FrameError e=FrameError::Ok;
  if (slow) {
    e=fiveBaud(addr,300,200);
    if (e==FrameError::Ok) e=receive(b,300);
    if (e==FrameError::Ok && b!=0x55) e=FrameError::Sync;
    if (e==FrameError::Ok) e=receive(k1,20);
    if (e==FrameError::Ok) e=receive(k2,20);
    if (e==FrameError::Ok) { link_.sleepMs(30); e=send(uint8_t(~k2),50); }
    if (e==FrameError::Ok) e=receive(b,50);
    if (e==FrameError::Ok && b!=uint8_t(~addr) && b!=uint8_t(~addr^0x80)) e=FrameError::Sync;
  } else {
    if(strategy==IsoStart::Aid || strategy==IsoStart::Lwr) {
      e=fastStart(strategy==IsoStart::Lwr);
      if(e==FrameError::Ok) {
        const uint8_t end=0x82;
        e=command(&end,1,0xc2,iso_);
        if(e==FrameError::Ok && count_)e=FrameError::Length;
      }
    }
    if(e==FrameError::Ok) {
      const uint8_t physical=address_;
      if(strategy==IsoStart::D2b)address_=startAddress_;
      e=fastStart();
      // Some masters answer the functional start from their physical address.
      if(strategy==IsoStart::D2b && e==FrameError::Address)
        e=checkFrame(FrameKind::Iso,rx_,physical,tester_,0xc1,start_,count_);
      address_=physical;
      if(e==FrameError::Ok && count_!=2)e=FrameError::Length;
    }
    if (e==FrameError::Ok) { k1=data()[0]; k2=data()[1]; }
  }
  const bool kwp=strategy==IsoStart::D2b;
  if (e==FrameError::Ok && (kwp ? k2!=0x8f : k2==0x8f || (k1!=0x43 && k1!=0xc2 && k1!=0xc4)))e=FrameError::Reply;
  functionalStart_=false;
  extended_=k1==0x43;
  if(e==FrameError::Ok)e=negotiateTiming(kwp ? 2 : 0);
  if(e==FrameError::Ok && strategy!=IsoStart::Direct) {
    const uint8_t present=kwp ? 0x3e : 0x3f;
    e=command(&present,1,present+0x40,iso_);
    if(e==FrameError::Ok && count_)e=FrameError::Reply;
    if(e==FrameError::Ok && strategy==IsoStart::Lwr)e=negotiateTiming(0);
  }
  if(e==FrameError::Ok)ready_=true;else close();
  return e;
}
}
