#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace syzygy {
inline size_t parity_shards(size_t data, size_t percentage, size_t minimum) {
  return percentage ? std::max((data * percentage + 99) / 100, minimum) : 0;
}

// Video kbps describes encoded payload, not the padded/FEC/encrypted wire bytes.
// Freeze the rate for each frame so telemetry cannot stretch an in-flight frame.
struct wan_frame_pacer {
  uint64_t packet_bytes;
  int64_t wire_kbps;
  wan_frame_pacer(int video_kbps, uint64_t video_bytes, uint64_t wire_bytes, uint64_t packet_size):
      packet_bytes(packet_size), wire_kbps(video_kbps > 0 && video_bytes && wire_bytes ?
        static_cast<int64_t>(std::ceil(static_cast<long double>(video_kbps) * wire_bytes * 1.10L / video_bytes)) : 0) {}
  std::chrono::microseconds offset(uint64_t packets) const {
    return wire_kbps > 0 ? std::chrono::microseconds((packets * packet_bytes * 8000 + wire_kbps - 1) / wire_kbps) :
                          std::chrono::microseconds::max();
  }
  bool fits_frame(uint64_t packets) const { return wire_kbps > 0 && offset(packets) <= std::chrono::milliseconds(50); }
  static bool backlog(std::chrono::steady_clock::time_point captured, std::chrono::steady_clock::time_point now) {
    return now - captured > std::chrono::milliseconds(100);
  }
};
}
