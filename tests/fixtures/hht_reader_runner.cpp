// Binary test fixture for tools/check_hht_screens.py. No hardware backend.
#include "HhtReader.h"
#include <cstdio>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
class Recorded : public ByteLink {
 public:
  std::vector<uint8_t> words; size_t cursor=0; unsigned ms=0; bool opened=false,prime=true;
  bool open() override {return opened=true;}
  void close() override {opened=false;}
  bool isOpen() const override {return opened;}
  LinkStatus wakeup(const LinkTiming&,uint8_t& echo,uint8_t& sync) override {echo=0xaa;sync=0x55;return LinkStatus::Ok;}
  LinkStatus exchange(uint8_t tx,Parity p,uint16_t,uint8_t& echo,uint8_t& reply) override {
    echo=tx;
    if(prime && tx==0xf0) {reply=0xf; prime=false; return LinkStatus::Ok;}
    if(cursor+3>words.size() || words[cursor]!=tx || bool(words[cursor+1])!=(p==Parity::Mark)) return LinkStatus::BadAck;
    reply=words[cursor+2]; cursor+=3;return LinkStatus::Ok;
  }
  void sleepMs(uint32_t n) override {ms+=n;}
  uint32_t nowMs() override {return ms;}
  uint32_t nowUs() override {return 1000*ms;}
  const char* name() const override {return "capture_replay";}
};
int main() {
#ifdef _WIN32
  _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
  uint8_t head[11];
  while(fread(head,1,sizeof head,stdin)==sizeof head) {
    Recorded line; TraceLog trace; HfmSession session(line,trace); hht::Reader reader; hht::Reading reading;
    EcuIdent id; id.vendorId=head[1] ? 1 : 2; id.diagIndexRaw=head[2];id.diagSubRaw=head[3];id.hhtCylinders=head[4];
    id.extPresent=true;id.romPage=head[5];id.descAddr=head[6];
    size_t n=head[7]|(unsigned(head[8])<<8);line.words.resize(n);
    if(fread(line.words.data(),1,n,stdin)!=n) return 2;
    if(session.connect(1)!=LinkStatus::Ok || session.prefix(0xf0)!=LinkStatus::Ok) return 3;
    auto st=head[9] ? reader.cranking(session,id,reading.words) : reader.read(session,id,head[0],reading);
    uint8_t result[20]={uint8_t(st),uint8_t(line.cursor==line.words.size()),uint8_t(reading.words.valid),uint8_t(reading.words.valid>>8)};
    for(unsigned i=0;i<8;++i){result[4+2*i]=uint8_t(reading.words.values[i]);result[5+2*i]=uint8_t(uint16_t(reading.words.values[i])>>8);}
    if(fwrite(result,1,sizeof result,stdout)!=sizeof result)return 4;
  }
}
