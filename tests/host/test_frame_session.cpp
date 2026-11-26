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

namespace {
// Each negative event is a real inter-frame/inter-request silence in the fixture.
class EngineBench : public BenchFrames {
 public:
  std::deque<int> input;
  bool capable=true, validDuty=true;
  uint8_t blink=0;
  bool frameRead(uint8_t& b,uint16_t timeout) override {
    if(echoPending)return BenchFrames::frameRead(b,timeout);
    if(input.empty())return false;
    int value=input.front();input.pop_front();
    if(value<0)return false;
    b=uint8_t(value);return true;
  }
  bool supportsDuty() const override {return capable;}
  bool supportsKeBlink() const override {return capable;}
  bool sampleDuty(DutySample& s) override {if(validDuty)s={700,690,710,32};return validDuty;}
  uint8_t readKeBlink() override {return blink;}
  void response(bool kwp,uint8_t peer,std::initializer_list<uint8_t> payload,bool corrupt=false) {
    std::vector<uint8_t> frame={uint8_t(kwp ? 0x80|payload.size() : 0x48),uint8_t(kwp ? 0xf1 : 0x6b),peer};
    frame.insert(frame.end(),payload.begin(),payload.end());
    uint8_t sum=0;for(uint8_t b:frame){sum+=b;input.push_back(b);}
    input.push_back(corrupt ? sum^1 : sum);
    if(!kwp && frame.size()+1<11)input.push_back(-1);
  }
  void slowStart(bool kwp=false) {input={0x55,kwp ? 0xe9 : 8,kwp ? 0x8f : 8,0xcc};}
  void caps(bool kwp,uint8_t peer=0x10,bool engine=true) {
    response(kwp,peer,{0x41,0,uint8_t(engine ? 0x88 : 0),uint8_t(engine ? 0x10 : 0),0,0});
  }
};
const Profile* engineRoute(Protocol protocol,const char* variant) {
  for(size_t id=0;id<routeCount();++id) {
    const auto& r=*route(id);
    if(r.protocol==protocol && !strcmp(r.variant,variant))return profile(r);
  }
  return nullptr;
}
}
TEST(me_slow_obd_handshake_requests_pid_and_verified_fault_count) {
  EngineBench io;TraceLog trace;ModuleDriver drv(io,trace);
  const auto* p=engineRoute(Protocol::Obd,"5 baud");CHECK(p);if(!p)return;
  io.slowStart();io.caps(false);
  CHECK_EQ(drv.connect(*p,9600),FrameError::Ok);
  CHECK((io.sent==std::vector<uint8_t>{0xf7,0x68,0x6a,0xf1,1,0,0xc4}));
  CHECK_EQ(io.baud,10400);CHECK_EQ(io.levels.size(),10);
  CHECK((io.levels==std::vector<uint8_t>{1,0,0,1,1,0,0,1,1,0}));
  CHECK(drv.result().obdResponding && !drv.result().identityValid);
  CHECK_EQ(drv.result().obdPeer,0x10);CHECK_EQ(drv.result().pidMask,0x88100000u);
  size_t prior=io.sent.size();CHECK_EQ(drv.measurement(*p,0),FrameError::Unsupported);CHECK_EQ(io.sent.size(),prior);
  io.response(false,0x10,{0x41,0x0c,0x1a,0xf8});
  CHECK_EQ(drv.measurement(*p,3),FrameError::Ok);CHECK_EQ(drv.result().rawLen,2);CHECK_EQ(drv.result().raw[0],0x1a);
  io.response(false,0x10,{0x41,1,0x82,0,0,0});io.input.push_back(-1);
  io.response(false,0x10,{0x43,3,2,1,0x15,0,0});
  CHECK_EQ(drv.faults(*p),FrameError::Ok);CHECK(drv.result().faultsValid);
  CHECK_EQ(drv.result().codes[0],0x0302);CHECK_EQ(drv.result().codes[1],0x0115);CHECK_EQ(meFaultSources(0x0302),7);
  io.response(false,0x10,{0x41,1,0x82,0,0,0});io.input.push_back(-1);
  io.response(false,0x10,{0x43,3,2,0,0,0,0});
  CHECK_EQ(drv.faults(*p),FrameError::Length);CHECK(!drv.ready());CHECK_EQ(drv.result().codeCount,0);
  CHECK(!drv.result().faultsValid && !drv.result().obdResponding);
}
TEST(me_kwp_slow_and_fast_source_selection_errors) {
  for(bool fast:{false,true}) {
    EngineBench io;TraceLog trace;FrameSession session(io,trace);uint32_t mask=0;
    if(fast){io.response(true,0x10,{0xc1,0xe9,0x8f});io.input.push_back(-1);}
    else io.slowStart(true);
    io.caps(true,0x18,false);io.caps(true,0x10);
    CHECK_EQ(session.connectObd(fast,mask),FrameError::Ok);CHECK(session.obdKwp());
    CHECK_EQ(session.obdPeer(),0x10);CHECK_EQ(mask,0x88100000u);
    if(fast)CHECK((io.sent==std::vector<uint8_t>{0xc1,0x33,0xf1,0x81,0x66,0xc2,0x33,0xf1,1,0,0xe7}));
    uint8_t bytes[2]={0x55,0xaa};
    io.response(true,0x18,{0x41,12,0xff,0xff});io.response(true,0x10,{0x41,12,0,0});
    CHECK_EQ(session.obdCurrent(12,bytes,2),FrameError::Ok);CHECK_EQ(bytes[0],0);CHECK_EQ(bytes[1],0);
    io.response(true,0x10,{0x7f,1,0x12});CHECK_EQ(session.obdCurrent(12,bytes,2),FrameError::Negative);
    io.response(true,0x10,{0x41,12,0xff,0xff},true);CHECK_EQ(session.obdCurrent(12,bytes,2),FrameError::Sum);
    CHECK(!session.ready());
  }
  EngineBench io;TraceLog trace;FrameSession s(io,trace);uint32_t mask=42;
  io.slowStart(true);io.caps(true,0x18);io.caps(true);
  CHECK_EQ(s.connectObd(false,mask),FrameError::Ambiguous);CHECK_EQ(mask,0);
  io.input={0x55,0x01,0x01};CHECK_EQ(s.connectObd(false,mask),FrameError::Unsupported);
  io.slowStart();io.caps(false);CHECK_EQ(s.connectObd(false,mask),FrameError::Ok);
  uint8_t data[2]{};io.input={0x48,0x6b,0x10,0x41};CHECK_EQ(s.obdCurrent(12,data,2),FrameError::Length);
}
TEST(ke_duty_and_blink_public_workflow_no_background_transmission) {
  EngineBench io;TraceLog trace;ModuleDriver drv(io,trace);
  auto* duty=engineRoute(Protocol::Duty,"X11 PWM");auto* n3=engineRoute(Protocol::Blink,"N3 M104/M119");
  CHECK(duty && n3);if(!duty || !n3)return;
  io.capable=false;CHECK_EQ(drv.connect(*duty,0),FrameError::Unsupported);
  io.capable=true;CHECK_EQ(drv.connect(*duty,0),FrameError::Ok);CHECK(!drv.result().identityValid);
  CHECK(!duty->canReadFaults());CHECK_EQ(drv.faults(*duty),FrameError::Unsupported);
  CHECK_EQ(drv.measurement(*duty,0),FrameError::Ok);CHECK_EQ(drv.result().duty.lowPermille,700);
  io.validDuty=false;CHECK_EQ(drv.measurement(*duty,0),FrameError::Reply);CHECK_EQ(drv.result().duty.cycles,0);
  CHECK_EQ(drv.connect(*n3,0),FrameError::Ok);
  for(uint8_t code:{3,9,3}){io.blink=code;CHECK_EQ(drv.faults(*n3),FrameError::Ok);}
  CHECK(drv.result().faultsValid);CHECK_EQ(drv.result().codeCount,2);
  io.blink=1;CHECK_EQ(drv.faults(*n3),FrameError::Ok);CHECK(drv.result().faultsValid);CHECK_EQ(drv.result().codeCount,0);
  io.blink=0;CHECK_EQ(drv.faults(*n3),FrameError::Timeout);CHECK(!drv.result().faultsValid);
  CHECK_EQ(drv.keepAlive(*n3),FrameError::Ok);CHECK(io.sent.empty() && io.levels.empty());
}
TEST(ke_sampled_signals_ignore_partial_cycles_and_reject_invalid_trains) {
  for(unsigned low:{16u,56u}) {
    DutyCounter counter;DutySample out{};
    for(unsigned t=17;t<4113;++t)counter.sample(t%80>=low);
    CHECK(counter.finish(out));CHECK_EQ(out.lowPermille,low*1000/80);CHECK_EQ(out.minimum,out.maximum);CHECK(out.cycles>=49);
    for(unsigned i=0;i<200;++i)counter.sample(true);
    CHECK(!counter.finish(out));CHECK_EQ(out.cycles,0);
  }
  DutyCounter empty;DutySample d;for(unsigned i=0;i<4096;++i)empty.sample(true);CHECK(!empty.finish(d));
  auto phase=[](BlinkCounter& b,bool high,unsigned ms){for(unsigned i=0;i<ms*8;++i)b.sample(high);};
  BlinkCounter blink;phase(blink,true,500);for(int i=0;i<3;++i){phase(blink,false,500);phase(blink,true,500);}
  CHECK(!blink.done());phase(blink,true,2000);CHECK_EQ(blink.result(),3);
  BlinkCounter stuck;phase(stuck,true,10);phase(stuck,false,2200);CHECK(stuck.done());CHECK_EQ(stuck.result(),0);
  BlinkCounter noise;phase(noise,true,10);phase(noise,false,5);phase(noise,true,2600);CHECK_EQ(noise.result(),0);
  BlinkCounter silent;phase(silent,true,6100);CHECK(silent.done());CHECK_EQ(silent.result(),0);
}
