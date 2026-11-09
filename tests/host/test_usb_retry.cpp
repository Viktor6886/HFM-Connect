#include "UsbRetry.h"
#include "minitest.h"

TEST(usb_retry_silent_failure_and_replug) {
  UsbRetry retry;
  CHECK(!retry.due(90000));
  retry.failed(100);
  retry.failed(1200);
  CHECK(!retry.due(1599));
  CHECK(retry.due(1600));
  uint32_t now = 1600;
  for (unsigned attempt = 1; attempt <= 7; attempt++) {
    retry.attempted(now, attempt);
    uint32_t delay = attempt < 5 ? 6000 : 30000;
    CHECK(!retry.due(now + delay - 1));
    now += delay;
    CHECK(retry.due(now));
  }
  retry = {};
  CHECK(!retry.due(now + 60000));
  retry.failed(0xfffffff0U);
  CHECK(!retry.due(100));
  CHECK(retry.due(1484));
}

TEST(usb_address_scan_boot_late_address_and_hotplug) {
  UsbAddressScan scan;
  CHECK(scan.due(0));  // Already connected at boot.
  scan.started(0);
  CHECK(!scan.due(999));
  CHECK(scan.due(1000));  // Same device count, address became ready without an event.
  scan.started(1000);
  scan.requested = true;  // Hot plug or completion of old handle teardown.
  CHECK(scan.due(1001));
  scan.started(1001);
  CHECK(!scan.due(1002));
  CHECK(scan.due(2001));  // A missed event cannot permanently stop discovery.
}
