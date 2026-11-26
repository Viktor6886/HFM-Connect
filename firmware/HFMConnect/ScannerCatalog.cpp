#include "ScannerCatalog.h"
namespace scanner {
#include "ScannerRoutes.inc"
static constexpr Route engineRoutes[]={
  {"ME-SFI · OBD-II","ME-OBD","5 baud",Protocol::Obd,7,0},
  {"ME-SFI · OBD-II","ME-OBD","fast init",Protocol::Obd,7,0},
  {"KE-Jetronic","KE","X11 PWM",Protocol::Duty,3,0},
  {"KE-Jetronic","KE","N3 M104/M119",Protocol::Blink,3,0},
};
static constexpr size_t hhtCount=sizeof(routes)/sizeof(routes[0]);
size_t routeCount() {return hhtCount+sizeof(engineRoutes)/sizeof(engineRoutes[0]);}
const Route* route(size_t id) {
  if(id<hhtCount)return &routes[id];
  return id<routeCount() ? &engineRoutes[id-hhtCount] : nullptr;
}
bool matchesContact(const Route& r, unsigned contact) {
  return r.contact != 0 && contact == r.contact;
}
const char* protocolName(Protocol p) {
  switch (p) {
    case Protocol::Ks1: return "KS-1";
    case Protocol::Ks3: return "KS-3";
    case Protocol::Ks6: return "KS-6";
    case Protocol::Obd: return "OBD-II K-Line";
    case Protocol::Duty: return "X11 PWM";
    case Protocol::Blink: return "CFI N3";
    case Protocol::Cdb: return "CDB";
    default: return "—";
  }
}
}
