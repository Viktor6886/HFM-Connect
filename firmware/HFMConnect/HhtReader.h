#pragma once
#include "HfmSession.h"
#include "HfmIdent.h"
#include "HhtValues.h"
namespace hht {
struct Reading { uint16_t screen=0; Words words; };
class Reader {
 public:
  void reset() { romReady_=false; }
  LinkStatus read(HfmSession& s,const EcuIdent& id,uint16_t screen,Reading& result);
  LinkStatus coding(HfmSession& s, const EcuIdent& id, uint8_t (&code)[3], uint8_t& flags, uint8_t& ack);
  static bool exists(uint16_t screen);
 private:
  LinkStatus readIndex(HfmSession& s,const EcuIdent& id,unsigned idx,uint8_t (&raw)[64],uint16_t& valid,uint8_t& first);
  LinkStatus word(HfmSession& s,uint8_t value,bool mark,uint8_t& data);
  LinkStatus loadRom(HfmSession& s,const EcuIdent& id);
  uint8_t rom_[780]{};
  bool romReady_=false;
};
}
