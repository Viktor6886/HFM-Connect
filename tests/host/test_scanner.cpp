#include "ModuleDriver.h"
#include "ScannerCatalog.h"
#include "minitest.h"
TEST(scanner_route_uses_vehicle_contact_and_separate_mux) {
  CHECK(scanner::route(scanner::routeCount()) == nullptr);
  bool found = false;
  for (size_t i = 0; i < scanner::routeCount(); ++i) {
    const auto& r = *scanner::route(i);
    if (strcmp(r.series, "124") || strcmp(r.module, "HFM")) continue;
    CHECK_EQ(r.contact, 8);
    CHECK(r.protocol == scanner::Protocol::Ks3);
    CHECK(scanner::matchesContact(r, 8));
    CHECK(!scanner::matchesContact(r, 1));
    found = true;
  }
  CHECK(found);
  scanner::Route missing{};
  CHECK(!scanner::matchesContact(missing, 0));
}

TEST(asr_chassis_dispatch_has_only_original_address_candidates) {
  unsigned w124=0,w124e=0,w140=0,w210=0;
  for(size_t i=0;i<scanner::routeCount();++i) {
    const auto& r=*scanner::route(i);
    if(strcmp(r.module,"ASR") || strncmp(r.variant,"GL_ISO2@",8))continue;
    const auto* p=scanner::profile(r);CHECK(p && p->iso); if(!p)continue;
    unsigned bit=p->address==0x30 ? 1 : p->address==0x34 ? 2 : p->address==0x35 ? 4 : 0;
    if(!strcmp(r.series,"124"))w124|=bit;
    if(!strcmp(r.series,"124E"))w124e|=bit;
    if(!strcmp(r.series,"140"))w140|=bit;
    if(!strcmp(r.series,"210"))w210|=bit;
  }
  CHECK_EQ(w124,0);CHECK_EQ(w124e,2);CHECK_EQ(w140,6);CHECK_EQ(w210,7);
}
