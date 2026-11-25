#include "ModuleDriver.h"
#include "minitest.h"
#include <deque>
#include <vector>
using namespace scanner;
class BenchFrames : public ByteLink {
 public:
  bool opened=false, echoPending=false;
  uint8_t echo=0;
  uint32_t clock=0, baud=0;
  std::deque<uint8_t> received;
  std::vector<uint8_t> sent, levels;
  bool open() override { return opened=true; }
  void close() override { opened=false; }
  bool isOpen() const override { return opened; }
  LinkStatus wakeup(const LinkTiming&,uint8_t&,uint8_t&) override { return LinkStatus::NotAllowed; }
  LinkStatus exchange(uint8_t,Parity,uint16_t,uint8_t&,uint8_t&) override { return LinkStatus::NotAllowed; }
  void sleepMs(uint32_t n) override { clock+=n; }
  uint32_t nowMs() override { return clock; }
  uint32_t nowUs() override { return clock*1000; }
  const char* name() const override { return "fixture"; }
  bool frameBaud(uint32_t b) override { baud=b; return true; }
  bool frameBreak(bool low) override { levels.push_back(low); return true; }
  bool frameWrite(uint8_t b) override { sent.push_back(b); echo=b; echoPending=true; return true; }
  bool frameRead(uint8_t& b,uint16_t) override {
    if (echoPending) { b=echo; echoPending=false; return true; }
    if (received.empty()) return false;
    b=received.front(); received.pop_front(); return true;
  }
  void reply(FrameKind k,uint8_t ecu,std::initializer_list<uint8_t> payload) {
    Frame f; CHECK(encodeFrame(k,k==FrameKind::Ks ? ecu : 0xf2,ecu,payload.begin(),payload.size(),false,f));
    received.insert(received.end(),f.bytes,f.bytes+f.size);
  }
};
TEST(ks_start_address_ident_echo_checksum_and_end_of_fault_list) {
  BenchFrames io; TraceLog log; FrameSession session(io,log);
  io.received={0x55,0x12,0x34};
  io.reply(FrameKind::Ks,0x30,{0x8f,1,2,3,4,5,6,7,8,9,10,11,12,13});
  CHECK(session.connectKs(0x30,9600,{500,200,4500,1})==FrameError::Ok);
  CHECK_EQ(session.size(),13); CHECK(io.sent.empty()); CHECK_EQ(io.levels.size(),10);
  unsigned packed=0;
  for (unsigned i=0;i<8;++i) if (!io.levels[i+1]) packed|=1u<<i;
  CHECK_EQ(packed,0xb0);
  io.reply(FrameKind::Ks,0x30,{0xd0,1}); uint8_t status[]={0x50,1};
  CHECK(session.request(status,2,0xd0,{30,4,1000,300})==FrameError::Ok);
  CHECK((io.sent==std::vector<uint8_t>{0x30,0x50,6,1,0x87,0}));
  uint8_t faults[]={0x10,0}; io.reply(FrameKind::Ks,0x30,{0x90,0x28});
  CHECK(session.request(faults,2,0x90,{30,4,1000,300})==FrameError::Ok);
  CHECK_EQ(session.data()[0],0x28);
  io.reply(FrameKind::Ks,0x30,{0x90,1,5}); io.received.back()^=1;
  CHECK(session.request(faults,2,0x90,{30,4,1000,300})==FrameError::Sum);
  CHECK(!session.ready());
}
TEST(iso_extended_header_pending_without_resend_busy_and_bad_peer) {
  BenchFrames io; TraceLog log; FrameSession session(io,log);
  io.reply(FrameKind::Iso,0x50,{0xc1,0x43,0}); io.reply(FrameKind::Iso,0x50,{0xc3,0,50,2,110,20,10});
  CHECK(session.connectIso(0x50,0xf2,false)==FrameError::Ok);
  CHECK_EQ(io.baud,10400); CHECK_EQ(session.isoTiming().first,50); CHECK_EQ(session.isoTiming().pause,60);
  CHECK((io.levels==std::vector<uint8_t>{1,0}));
  auto sent=io.sent.size(); uint8_t fault=0xa1;
  io.reply(FrameKind::Iso,0x50,{0x7f,0xa1,0x78}); io.reply(FrameKind::Iso,0x50,{0xe1,0xff,0xff});
  CHECK(session.request(&fault,1,0xe1,session.isoTiming())==FrameError::Ok);
  CHECK_EQ(io.sent.size()-sent,6); CHECK_EQ(io.sent[sent],0x80);
  io.reply(FrameKind::Iso,0x50,{0x7f,0xa1,0x21}); io.reply(FrameKind::Iso,0x50,{0x7f,0xa1,0x21});
  CHECK(session.request(&fault,1,0xe1,session.isoTiming())==FrameError::Busy);
  io.reply(FrameKind::Iso,0x51,{0xe1,0xff,0xff});
  CHECK(session.request(&fault,1,0xe1,session.isoTiming())==FrameError::Address);
  CHECK(!session.ready());
}
TEST(iso_and_ks_frame_boundary_and_truncation) {
  Frame f; uint8_t p[255]{}; size_t offset=0, n=0;
  CHECK(encodeFrame(FrameKind::Iso,0xf2,0x50,p,255,false,f)); CHECK_EQ(f.size,260);
  CHECK(checkFrame(FrameKind::Iso,f,0x50,0xf2,0,offset,n)==FrameError::Ok); CHECK_EQ(n,254);
  --f.size; CHECK(checkFrame(FrameKind::Iso,f,0x50,0xf2,0,offset,n)==FrameError::Length);
  CHECK(!encodeFrame(FrameKind::Ks,0x30,0xf2,p,252,false,f));
}

TEST(module_dtc_stream_is_received_once_and_history_requires_a_terminator) {
  BenchFrames io;TraceLog trace;ModuleDriver driver(io,trace);
  const Profile* p=nullptr;
  for(size_t i=0;i<routeCount()&&!p;++i) {const auto* r=route(i);if(!strcmp(r->module,"KLA") && !strcmp(r->variant,"GL_ISO2")) p=profile(*r);}
  CHECK(p!=nullptr);if(!p)return;
  io.reply(FrameKind::Iso,p->address,{0xc1,0xc2,0x00});io.reply(FrameKind::Iso,p->address,{0xc3,0,50,2,110,20,10});
  io.reply(FrameKind::Iso,p->address,{0x7c,0,'A','B'});
  CHECK(driver.connect(*p,9600)==FrameError::Ok);CHECK_EQ(driver.result().identLen,2);
  auto before=io.sent.size();
  io.reply(FrameKind::Iso,p->address,{0xe1,0x12,0x34,7});
  io.reply(FrameKind::Iso,p->address,{0xe1,0xff,0xff});
  CHECK(driver.faults(*p)==FrameError::Ok);CHECK_EQ(io.sent.size()-before,5);
  CHECK_EQ(driver.result().codes[0],0x1234); CHECK_EQ(driver.result().details[0],7);
  CHECK(driver.result().faultsValid);
  before=io.sent.size(); CHECK(driver.measurement(*p,90)==FrameError::Reply);CHECK_EQ(before,io.sent.size());
  io.reply(FrameKind::Iso,p->address,{0xe1,0x12,0x34,7});
  CHECK(driver.faults(*p)==FrameError::Timeout);CHECK(!driver.result().faultsValid);
}
TEST(ks_memory_read_requires_fixed_catalogue_plan_and_matching_returned_address) {
  BenchFrames io;TraceLog trace;ModuleDriver driver(io,trace);
  const Profile* p=nullptr;
  for(size_t i=0;i<routeCount()&&!p;++i){auto r=route(i);if(!strcmp(r->module,"GM"))p=profile(*r);}
  CHECK(p && p->measureCount);if(!p || !p->measureCount)return;
  io.received={0x55,1,2};io.reply(FrameKind::Ks,p->address,{0x8f,1,2,3,4,5,6,7,8,9,10,11,12,13});
  CHECK(driver.connect(*p,9600)==FrameError::Ok);
  const auto& m=p->measures[0];CHECK_EQ(m.bytes,1);
  io.reply(FrameKind::Ks,p->address,{0xb0,1,m.lo,m.hi,1,0x75});
  CHECK(driver.measurement(*p,0)==FrameError::Ok);CHECK_EQ(driver.result().raw[0],0x75);
  io.reply(FrameKind::Ks,p->address,{0xb0,1,uint8_t(m.lo+1),m.hi,1,0x50});
  CHECK(driver.measurement(*p,0)==FrameError::Reply);CHECK_EQ(driver.result().rawLen,0);
}

TEST(egs_immediate_address_and_functional_gl_start) {
  BenchFrames io; TraceLog log; FrameSession wire(io,log);
  io.received={0x55,0x12,0x34};
  io.reply(FrameKind::Ks,0x20,{0x8f,1,2,3,4,5,6,7,8,9,10,11,12,13});
  CHECK(wire.connectKs(0x20,9600,{300,10,1995,1000},true)==FrameError::Ok);
  CHECK(io.levels.empty()); CHECK((io.sent==std::vector<uint8_t>{0x20}));
  io.sent.clear();
  io.reply(FrameKind::Iso,0x22,{0xc1,0xc2,0}); io.reply(FrameKind::Iso,0x22,{0xc3,0,50,2,110,20,10});
  CHECK(wire.connectIso(0x7e,0xf2,false,0x22,true)==FrameError::Ok);
  CHECK((io.sent==std::vector<uint8_t>{0xc1,0x7e,0xf2,0x81,0xb2,0x82,0x22,0xf2,0x83,0,0x19}));
  io.sent.clear();io.reply(FrameKind::Iso,0x7e,{0xc1,0xc2,0});
  CHECK(wire.connectIso(0x7e,0xf2,false,0x22,true)==FrameError::Address);
  CHECK_EQ(io.sent.size(),5);
}
TEST(iso_slow_keys_and_inverted_address_are_required_before_session_timing) {
  BenchFrames io; TraceLog log; FrameSession wire(io,log);
  io.received={0x55,0xc2,0x01,0xc5};
  io.reply(FrameKind::Iso,0x3a,{0xc3,0,50,2,110,20,10});
  CHECK(wire.connectIso(0x3a,0xf2,true)==FrameError::Ok);
  CHECK_EQ(io.levels.size(),10);CHECK_EQ(io.sent[0],0xfe);
  io.received={0x55,0xc2,0x01,0xc4};
  CHECK(wire.connectIso(0x3a,0xf2,true)==FrameError::Sync);CHECK(!wire.ready());
}

TEST(comfort_fault_clear_checks_profile_and_never_retransmits_busy_write) {
  BenchFrames io;TraceLog log;ModuleDriver driver(io,log);
  const Profile* p=nullptr;
  for(size_t i=0;i<routeCount()&&!p;++i) {
    auto r=route(i);if(!strcmp(r->module,"KLA")) p=profile(*r);
  }
  CHECK(p && p->clearFaultMemory); if(!p || !p->clearFaultMemory)return;
  io.reply(FrameKind::Iso,p->address,{0xc1,0xc2,0});io.reply(FrameKind::Iso,p->address,{0xc3,0,50,2,110,20,10});
  io.reply(FrameKind::Iso,p->address,{0x7c,0,'A','B'});
  CHECK(driver.connect(*p,9600)==FrameError::Ok);
  auto before=io.sent.size();
  CHECK(driver.clearFaults(*p)==FrameError::Reply);CHECK_EQ(io.sent.size(),before);
  auto read=[&](){
    io.reply(FrameKind::Iso,p->address,{0xe1,0x12,0x34,1});
    io.reply(FrameKind::Iso,p->address,{0xe1,0xff,0xff});
    CHECK(driver.faults(*p)==FrameError::Ok);
  };
  read();
  Profile disabled=*p;disabled.clearFaultMemory=false;
  before=io.sent.size();CHECK(driver.clearFaults(disabled)==FrameError::Reply);CHECK_EQ(io.sent.size(),before);
  io.reply(FrameKind::Iso,p->address,{0x7c,0,'A','B'});CHECK(driver.verifyIdentity(*p)==FrameError::Ok);
  before=io.sent.size();
  io.reply(FrameKind::Iso,p->address,{0x7f,0x14,0x21});
  CHECK(driver.clearFaults(*p)==FrameError::Busy);CHECK_EQ(io.sent.size()-before,5);
  CHECK(!driver.result().faultsValid);
  read();before=io.sent.size();
  io.reply(FrameKind::Iso,p->address,{0x7f,0x14,0x78});io.reply(FrameKind::Iso,p->address,{0x54});
  CHECK(driver.clearFaults(*p)==FrameError::Ok);CHECK_EQ(io.sent.size()-before,5);
  io.reply(FrameKind::Iso,p->address,{0xe1,0xff,0xff});CHECK(driver.faults(*p)==FrameError::Ok);
  CHECK(driver.result().faultsValid);CHECK_EQ(driver.result().codeCount,0);
  io.reply(FrameKind::Iso,p->address,{0x7c,0,'Z','B'});CHECK(driver.verifyIdentity(*p)==FrameError::Address);
  CHECK(!driver.ready());CHECK(!driver.result().identityValid);
}
