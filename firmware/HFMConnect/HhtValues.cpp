#include "HhtValues.h"
namespace hht {
CrankPhase CrankingWindow::step(uint32_t now,bool starter,int batteryDecivolts) {
  if(batteryDecivolts<90)return CrankPhase::LowVoltage;
  if(!started_) {
    if(now-waiting_>=15000)return CrankPhase::NoStarter;
    if(!starter)return CrankPhase::Wait;
    start_=now;started_=true;
  }
  if(now-start_>=10010)return CrankPhase::Done;
  if(!starter)return CrankPhase::Released;
  return withinSampleWindow(now) ? CrankPhase::Sample : CrankPhase::Settle;
}
Words crankingSpeeds(const uint8_t (&raw)[64],bool bosch,unsigned cylinders) {
  Words result;
  if(cylinders!=4 && cylinders!=6)return result;
  for(unsigned i=0;i<cylinders;++i) {
    const unsigned offset=bosch ? i*4 : i*6+2;
    uint32_t ticks=(uint32_t(raw[offset])<<8)|raw[offset+2];
    if(bosch && cylinders==4 && ticks<0x52e3)ticks+=65536;
    const unsigned period=bosch ? ticks*266/10000 : ticks*8/100;
    const uint16_t divisor=uint16_t(period*(cylinders/2));
    if(!divisor)break;
    result.put(i,uint16_t(600000/divisor));
  }
  return result;
}
void CrankingResult::accumulate(const Words& sample) {
  if(cylinders!=4 && cylinders!=6)return;
  const unsigned valid=sample.valid&((1u<<cylinders)-1);
  if(!valid)return;
  for(unsigned i=0;i<cylinders;++i)if(valid&(1u<<i)) {
    const auto speed=uint16_t(sample.values[i]);
    if(speed>rpm[i])rpm[i]=speed;
  }
  mask|=valid;
  if(samples<65535)++samples;
}
Words smoothness(const uint8_t (&raw)[64],bool bosch,unsigned cylinders) {
  Words result;
  if(cylinders!=4 && cylinders!=6) return result;
  static const uint8_t order4[]={0,2,3,1},order6[]={0,4,2,5,1,3};
  const auto* order=cylinders==4 ? order4 : order6;
  for(unsigned i=0;i<cylinders;++i) {
    unsigned start=bosch ? 4*order[i] : 6*order[i]+2;
    unsigned value=(unsigned(raw[start])<<8)|raw[start+2];
    unsigned period=bosch ? value*266/10000 : value*8/100;
    if(!period) return {};
    unsigned rpm=600000/(period*(cylinders/2));
    if(rpm<180 || rpm>7000) return {};
    result.put(i,rpm);
  }
  return result;
}
#include "HhtNumeric.inc"
static Words oxygenRange(const uint8_t* r,uint8_t d,uint8_t revision) {
  int64_t n=d<0x45 ? 4888LL*r[6]+1251220LL*r[8] : 4888LL*r[8]+1251226LL*r[6];
  if (d>=0x45 && (d<0x46 || (d==0x49 && revision>=0x50 && revision<0x60))) n-=160156900;
  const int q=int16_t(n/10000); const int ends[]={159,239,309,364,409,444,489};
  int result=128;
  if (q>=10) for (int i=0;i<7;++i) if (q<=ends[i]) {result=64>>i;break;}
  Words w; w.put(0,result); return w;
}
Words convert(uint16_t entry,const uint8_t (&r)[64],bool bosch,uint8_t diag,
              uint8_t revision,uint8_t cylinders,uint16_t parameter) {
  Words w;
  if (cylinders!=4 && cylinders!=6) return w;
  switch (entry) {
#include "HhtCases.inc"
    case 0x0042: {
      int a=bosch ? 25*r[diag<0x41 ? 2 : 6] : r[2]+256*r[6];
      int b=bosch ? 25*r[8] : diag>=0x45 ? r[10]+256*r[14] : 30*r[10];
      w.put(0,b ? a : 0); w.put(1,b); break;
    }
    case 0x0945:
      if (bosch) w.put(0,r[2]*25<100 ? 0 : (5500000-35157*r[0])/100000);
      else w.put(0,(diag<0x45 ? r[10]*30<100 : !r[14]) ? 0 : (550-r[6]*256-r[2])/10);
      break;
    case 0x2312: w.put(0,int16_t((4888LL*r[4]+1251226LL*r[2])/10000)>400 ? 2 : 1); break;
    case 0x1546: case 0x15c8: case 0x187b: case 0x18e8: case 0x1611: {
      int n=entry==0x1546 || entry==0x18e8 ? cylinders : cylinders/2;
      if (entry==0x18e8 && !(r[cylinders*2]&1)) {for(int i=0;i<6;++i) w.put(i,0);break;}
      for (int i=0;i<n;++i) {
        int x=r[2*i];
        if (entry==0x187b) x=(4*x+7)/10;
        if (entry==0x18e8) x=bosch ? -35157*x/100000 : -x/10;
        if (entry==0x1611) x=bosch ? 213*x/100 : diag>=0x49 ?
            ((r[4*i]<<3)+(r[4*i+2]<<11))/10 : (8681*r[4*i]+2222336*r[4*i+2])/10000;
        w.put(i,x);
      } break;
    }
    case 0x0cdd: {
      unsigned i=parameter&255; if(i>=32) break;
      bool old=diag<0x45;
      uint8_t mask=old ? (bosch ? maskd1c2 : maskd182)[i] : maskd202[i];
      bool value=r[bosch ? 0 : 2]&mask;
      if (old) w.put(0,1+(value!=bool((parameter>>8)&(bosch ? 2 : 1))));
      else w.put(0,value ? maskd242[i] : maskd242[i]==2 ? 1 : 2);
      break;
    }
    case 0x0f5a: {
      int s=bosch ? r[2] : r[2]|r[6]; int v=bosch ? r[6] : r[10]|r[14];
      for(int i=0;i<7;++i) w.put(i,0);
      if (!bosch) {
        if(v&1) w.put(0,s&1 ? 1 : 2);
        if(w.values[0]==1 && (v&16)) w.put(1,s&16 ? 1 : 2);
        if(v&2) w.put(2,s&2 ? 1 : 2);
        if(v&8) w.put(3,s&8 ? 1 : 0);
        if(v&32) w.put(4,s&32 ? 2 : 1);
        if(v&128) w.put(5,s&128 ? 2 : 1);
        w.put(6,r[18]&16 ? 2 : 1);
      } else {
        if(!(v&1)) w.put(0,s&1 ? 1 : 2);
        if(w.values[0]==1 && !(v&32)) w.put(1,s&16 ? 1 : 2);
        if(!(v&2)) w.put(2,s&2 ? 1 : 2);
        if(!(v&4)) w.put(3,s&8 ? 2 : 0);
        if(!(v&8)) { w.put(4,s&32 ? 2 : 1); w.put(5,s&128 ? 2 : 1); }
        w.put(6,v&16 ? 1 : 2);
      } break;
    }
  }
  return w;
}
}
