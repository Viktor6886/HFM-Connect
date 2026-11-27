// Emit what the firmware's actual registry exposes, including unavailable candidates.
#include "ModuleDriver.h"
#include <cstdio>
#include <cstring>
int main() {
  for(size_t i=0;i<scanner::routeCount();++i) {
    const auto& route=*scanner::route(i);
    const auto* p=scanner::profile(route);
    const bool hfm=std::strcmp(route.module,"HFM")==0;
    std::printf("%zu\t%s\t%s\t%s\t%s\t%u\t%u\t%d\t%d\t%d\t%u\t",
        i,route.series,route.module,route.variant,p && p->iso ? p->isoName() : scanner::protocolName(route.protocol),
        unsigned(route.contact),unsigned(route.mux),hfm || p,hfm || (p && p->canReadFaults()),
        bool(p && p->clearFaultMemory),p ? p->classicIdBytes : 0);
    if(p) for(unsigned j=0;j<p->measureCount;++j) std::printf("%s%u",j ? "," : "",p->measures[j].at);
    std::putchar('\n');
  }
}
