#include "ScannerCatalog.h"
namespace scanner {
#include "ScannerRoutes.inc"
size_t routeCount() { return sizeof(routes) / sizeof(routes[0]); }
const Route* route(size_t id) { return id < routeCount() ? &routes[id] : nullptr; }
bool matchesContact(const Route& r, unsigned contact) {
  return r.contact != 0 && contact == r.contact;
}
const char* protocolName(Protocol p) {
  switch (p) {
    case Protocol::Ks1: return "KS-1";
    case Protocol::Ks3: return "KS-3";
    case Protocol::Ks6: return "KS-6";
    case Protocol::Cdb: return "CDB";
    default: return "—";
  }
}
}
