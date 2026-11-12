// Automatic re-read of the fault memory (DtcRefresh, D-033): when the engine
// reads by itself and when it must stop. Software tests of the rule only; the
// engine task and a real ECU are not covered here.
#include "DtcRefresh.h"
#include "minitest.h"

TEST(dtc_refresh_nothing_wanted_before_a_connection) {
  DtcRefresh r;
  CHECK(!r.wanted());
  CHECK(!r.due(0));
  CHECK(!r.due(100000));
  r.connected();  // automatic connection: read once
  CHECK(r.due(5));
  r.readOk(900, true);
  CHECK(!r.wanted());
  CHECK(!r.due(1000));
  CHECK(!r.due(600000));  // a current readout is never repeated by itself
}

TEST(dtc_refresh_reread_once_after_link_loss) {
  DtcRefresh r;
  r.connected();
  r.readOk(1000, true);
  r.linkLost(60000);  // long after the read: an ordinary loss
  CHECK(r.wanted());
  CHECK(r.due(60000));  // at once when the link is back
  r.readOk(61000, true);
  CHECK(!r.due(61001));
  CHECK(!r.suspended());
  // A user's read makes the readout current as well.
  r.linkLost(120000);
  CHECK(r.due(120500));
  r.readOk(121000, false);
  CHECK(!r.wanted());
}

TEST(dtc_refresh_failed_read_is_retried_with_a_pause_and_gives_up) {
  DtcRefresh r;
  r.connected();
  r.readOk(1000, true);
  r.linkLost(50000);
  r.readFailed(51000, true);
  CHECK(r.wanted());
  CHECK(!r.due(51000 + DtcRefresh::kRetryMs - 1));  // not hammering the line
  CHECK(r.due(51000 + DtcRefresh::kRetryMs));
  r.readFailed(55000, true);
  CHECK(r.due(55000 + DtcRefresh::kRetryMs));
  r.readFailed(59000, true);  // third failure in a row: stop
  CHECK(r.suspended());
  CHECK(!r.due(59000 + DtcRefresh::kRetryMs));
  CHECK(!r.due(900000));
  r.linkLost(910000);  // further losses do not start it again
  CHECK(!r.due(920000));
  // The user's failed read is not counted; a successful one starts over.
  r.readFailed(930000, false);
  CHECK(r.suspended());
  r.readOk(940000, false);
  CHECK(!r.suspended());
  r.linkLost(950000);
  CHECK(r.due(950100));
}

TEST(dtc_refresh_one_failure_then_success_resets_the_count) {
  DtcRefresh r;
  r.connected();
  r.readFailed(1000, true);
  r.readFailed(5000, true);
  r.readOk(9000, true);
  r.linkLost(100000);
  r.readFailed(101000, true);
  r.readFailed(105000, true);
  CHECK(!r.suspended());  // two in a row again, not four
  CHECK(r.due(105000 + DtcRefresh::kRetryMs));
}

TEST(dtc_refresh_stops_when_the_link_drops_right_after_its_own_read) {
  DtcRefresh r;
  r.connected();
  r.readOk(1000, true);
  // Loss within kSuspectMs of the automatic read, twice in a row: the read
  // itself may be what breaks the session - no third attempt.
  r.linkLost(1000 + DtcRefresh::kSuspectMs - 1);
  CHECK(r.due(5000));  // first time: still read again
  r.readOk(6000, true);
  r.linkLost(6500);
  CHECK(r.suspended());
  CHECK(!r.due(10000));
  r.linkLost(500000);  // stays off after later, unrelated losses
  CHECK(!r.due(500100));
  // "Connect" pressed by the user: one more read, counters cleared.
  r.userConnected();
  CHECK(r.due(600000));
  r.readOk(600500, false);
  r.linkLost(700000);
  CHECK(r.due(700100));
}

TEST(dtc_refresh_single_quick_loss_is_forgiven_by_a_normal_one) {
  DtcRefresh r;
  r.connected();
  r.readOk(1000, true);
  r.linkLost(1500);  // quick loss after the automatic read (1 of 2)
  r.readOk(4000, true);
  r.linkLost(4000 + DtcRefresh::kSuspectMs);  // not quick: the count starts again
  CHECK(!r.suspended());
  r.readOk(20000, true);
  r.linkLost(20500);
  CHECK(!r.suspended());  // 1 of 2 again
  CHECK(r.due(21000));
  // A loss is judged against an automatic read only once.
  DtcRefresh q;
  q.connected();
  q.readOk(1000, true);
  q.linkLost(1200);
  q.linkLost(1400);  // same outage reported twice: no automatic read in between
  CHECK(!q.suspended());
  // Works across the 32-bit millisecond wrap.
  DtcRefresh w;
  w.connected();
  w.readFailed(0xFFFFFF00u, true);
  CHECK(!w.due(0xFFFFFFF0u));
  CHECK(w.due(0xFFFFFF00u + DtcRefresh::kRetryMs));
}
