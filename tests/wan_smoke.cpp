#include "syzygy/wan.h"
#include <cassert>
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
}
