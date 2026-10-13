#pragma once
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <mutex>

namespace syzygy {
// Session-local control. An unavailable or stale telemetry source never changes
// another client's stream or persistent host settings.
struct wan_state {
  bool enabled = false;
  int ceiling_kbps = 0;
  std::atomic<int> bitrate_kbps{0};
  std::atomic<int> fec{10};
  std::atomic<bool> runtime_bitrate{false};
  std::atomic<bool> encoder_overrides{false};
  std::mutex lock;
  int baseline_rtt = 0;
  int stable = 0;
  int healthy_fec = 0;
  int floor_stable = 0, previous_rtt = 0;
  std::chrono::steady_clock::time_point last{};

  bool feedback(int rtt, int jitter, int loss_per_mille) {
    if (rtt < 0 || rtt > 5000 || jitter < 0 || jitter > 5000 ||
        loss_per_mille < 0 || loss_per_mille > 1000) return false;
    std::lock_guard guard(lock);
    auto now = std::chrono::steady_clock::now();
    if (last.time_since_epoch().count() && now-last < std::chrono::milliseconds(200)) return false;
    last = now;
    if (!baseline_rtt || rtt < baseline_rtt) baseline_rtt = std::max(1, rtt);
    if (jitter < 8 && std::abs(rtt-previous_rtt) < 5) {
      if (++floor_stable >= 10) { baseline_rtt = std::max(1, rtt); floor_stable = 0; }
    } else floor_stable = 0;
    previous_rtt = rtt;
    bool congestion = rtt > baseline_rtt + 25 || jitter > 15;
    int target = bitrate_kbps.load();
    if (congestion || loss_per_mille > 20) {
      stable = 0;
      target = std::max(std::min(1000, ceiling_kbps), target * 80 / 100);
    } else if (++stable >= 5) {
      stable = 0;
      target = std::min(ceiling_kbps, target + std::max(250, ceiling_kbps / 20));
    }
    // Redundancy has a cost: never raise it while queueing delay is rising.
    int redundancy = congestion ? 5 : loss_per_mille >= 50 ? 35 : loss_per_mille >= 10 ? 20 : 5;
    if (!congestion && loss_per_mille < 10 && fec.load() > 10) {
      if (++healthy_fec < 3) redundancy = fec.load();
      else { redundancy = std::max(5, fec.load()-10); healthy_fec = 0; }
    } else healthy_fec = 0;
    if (!runtime_bitrate.load()) redundancy = std::min(10, redundancy);
    target = std::min(target, ceiling_kbps * 110 / (100 + redundancy));
    fec.store(redundancy);
    if (runtime_bitrate.load()) bitrate_kbps.store(target);
    return true;
  }
  int current_fec() {
    std::lock_guard guard(lock);
    // Conservative fallback when telemetry stops; no permanent optimistic FEC.
    if (!last.time_since_epoch().count() || std::chrono::steady_clock::now()-last > std::chrono::seconds(3)) return 10;
    return runtime_bitrate.load() ? fec.load() : std::min(10, fec.load());
  }
};
}
