#include "syzygy/wan_pacing.h"
#include <cassert>
#include <iostream>

int main() {
  using namespace std::chrono;
  const auto origin = steady_clock::now();
  const int kbps = 5000;
  const uint64_t video_bytes = 10417; // 5 Mbps at 60 FPS, rounded upward.
  const uint64_t data = (video_bytes + 975) / 976;
  const uint64_t packets = data + syzygy::parity_shards(data, 5, 2);
  const uint64_t blocksize = 1008, wire_size = blocksize + 28;
  const auto legacy_frame_time = microseconds(packets * blocksize * 8000 / (kbps * 105 / 100));
  auto legacy_end = origin, fixed_end = origin;
  syzygy::wan_frame_pacer pacer(kbps, video_bytes, packets * wire_size, wire_size);
  assert(pacer.fits_frame(packets));
  for (int frame = 0; frame < 1200; ++frame) {
    auto arrival = origin + nanoseconds(int64_t(frame) * 1000000000 / 60);
    legacy_end = std::max(legacy_end, arrival) + legacy_frame_time;
    fixed_end = std::max(fixed_end, arrival) + pacer.offset(packets);
    assert(fixed_end - arrival < nanoseconds(1000000000 / 60));
    if (frame == 99) {
      auto backlog = duration_cast<milliseconds>(legacy_end - arrival);
      assert(backlog > milliseconds(300));
      std::cout << "Original 5 Mbps pacer backlog after 100 frames: " << backlog.count() << " ms\n";
    }
  }
  // Actual minimum FEC, encryption prefixes, IPv4/IPv6, padding and FPS.
  for (int fps : {30, 60, 120}) for (int fec : {0, 5, 10, 35}) for (int minimum : {0, 2, 10})
    for (int extra : {28, 48, 80}) {
      uint64_t bytes = (uint64_t(kbps) * 1000 + 8 * fps - 1) / (8 * fps);
      uint64_t shards = (bytes + 975) / 976;
      shards += syzygy::parity_shards(shards, fec, minimum);
      syzygy::wan_frame_pacer candidate(kbps, bytes, shards * (blocksize + extra), blocksize + extra);
      assert(candidate.offset(shards) < nanoseconds(1000000000 / fps));
      assert(candidate.fits_frame(shards));
    }
  // A jumbo IDR must not occupy the sender for several hundred milliseconds.
  syzygy::wan_frame_pacer keyframe(kbps, 300000, 360000, 1200);
  assert(!keyframe.fits_frame(300));
  assert(syzygy::wan_frame_pacer::backlog(origin, origin + milliseconds(101)));
  assert(!syzygy::wan_frame_pacer::backlog(origin, origin + milliseconds(100)));
  syzygy::wan_frame_pacer invalid(0, 0, 0, 1008);
  assert(!invalid.fits_frame(1));
  assert(syzygy::parity_shards(10, 0, 2) == 0);
  std::cout << "PASS: 5 Mbps pacing remains bounded with real wire overhead and FEC minima\n";
}
