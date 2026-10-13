#include "syzygy/wan.h"
#include <cassert>
#include <limits>
int main() {
  syzygy::wan_state a, b;
  a.enabled = true;
  a.ceiling_kbps = 10000;
  a.bitrate_kbps = 10000;
  a.runtime_bitrate = true;
  assert(a.current_fec() == 10);
  assert(a.feedback(30, 2, 0));
  assert(a.current_fec() == 5);
  assert(!a.feedback(30, 2, 0)); // Rate limit.
  a.last = {};
  assert(!a.feedback(-1, 0, 0));
  assert(!a.feedback(30, 0, 1001));
  assert(a.feedback(30, 2, 60));
  assert(a.current_fec() == 35);
  assert(a.bitrate_kbps <= 10000*110/135);
  a.last = {};
  assert(a.feedback(30, 2, 0));
  assert(a.current_fec() == 35); // No redundancy oscillation after one recovered sample.
  a.runtime_bitrate = false;
  assert(a.current_fec() <= 10); // Encoder failure immediately bounds redundancy.
  a.runtime_bitrate = true;
  assert(b.bitrate_kbps == 0); // Isolated sessions.
  a.last = {};
  assert(a.feedback(150, 25, 60));
  assert(a.current_fec() == 5); // Congestion must not inflate FEC.
  a.runtime_bitrate = false;
  int bitrate = a.bitrate_kbps;
  a.last = {};
  assert(a.feedback(30, 0, 100));
  assert(a.current_fec() <= 10);
  assert(a.bitrate_kbps == bitrate); // Unsupported encoder retains its budget.
  a.last = std::chrono::steady_clock::now()-std::chrono::seconds(4);
  assert(a.current_fec() == 10);
  // Disabled and uninitialized sessions cannot accept control feedback.
  assert(!b.feedback(30, 0, 0));
  b.enabled = true;
  assert(!b.feedback(30, 0, 0));
  assert(b.last.time_since_epoch().count() == 0);

  // A telemetry outage must not count pre-outage samples toward recovery.
  a.runtime_bitrate = true;
  a.bitrate_kbps = 4000;
  a.fec = 35;
  a.stable = 4;
  a.healthy_fec = 2;
  a.last = std::chrono::steady_clock::now()-std::chrono::seconds(4);
  assert(a.feedback(30, 0, 0));
  assert(a.fec == 35);
  assert(a.bitrate_kbps == 4000);

  // Rate-control arithmetic must not overflow before enforcing the ceiling.
  b.ceiling_kbps = std::numeric_limits<int>::max();
  b.bitrate_kbps = b.ceiling_kbps;
  b.runtime_bitrate = true;
  auto now = std::chrono::steady_clock::now();
  assert(b.feedback(30, 0, 60, now));
  assert(b.bitrate_kbps > 0 && b.bitrate_kbps <= b.ceiling_kbps);
  for (int i = 1; i <= 30; ++i) {
    assert(b.feedback(30, 0, 0, now + std::chrono::seconds(i)));
    assert(b.bitrate_kbps > 0 && b.bitrate_kbps <= b.ceiling_kbps);
  }
}
