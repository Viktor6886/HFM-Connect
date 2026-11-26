#pragma once
#include <stddef.h>
#include <stdint.h>

namespace scanner {
enum class Protocol : uint8_t { Unresolved, Ks1, Ks3, Ks6, Cdb, Obd, Duty, Blink };
struct Route {
  const char* series;
  const char* module;
  const char* variant;
  Protocol protocol;
  uint8_t contact;  // zero = determined by the module at runtime
  uint8_t mux;      // zero = no fixed HHT channel in the source
};
size_t routeCount();
const Route* route(size_t id);
const char* protocolName(Protocol p);
bool matchesContact(const Route& route, unsigned contact);
}
