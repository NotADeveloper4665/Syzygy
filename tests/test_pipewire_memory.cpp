#include "platform/linux/pipewire_memory.h"
#include <array>
#include <cassert>
#include <iostream>
int main() {
  std::array<std::uint8_t, 24> producer {};
  for (std::size_t i = 0; i < producer.size(); ++i) producer[i] = i;
  std::vector<std::uint8_t> frame;
  assert(syzygy::pipewire::copy_memory_frame(frame, producer.data(), producer.size(), 4, 12, 2, 2));
  assert(frame.size() == 16 && frame[0] == 4 && frame[7] == 11 && frame[8] == 16 && frame[15] == 23);
  producer.fill(0); // The encoder's snapshot survives producer recycling.
  assert(frame[0] == 4 && frame[15] == 23);
  const auto saved = frame;
  assert(!syzygy::pipewire::copy_memory_frame(frame, producer.data(), 23, 4, 12, 2, 2));
  assert(!syzygy::pipewire::copy_memory_frame(frame, producer.data(), 24, 25, 12, 2, 2));
  assert(!syzygy::pipewire::copy_memory_frame(frame, producer.data(), 24, 0, 4, 2, 2));
  assert(!syzygy::pipewire::copy_memory_frame(frame, producer.data(), 24, 0, -12, 2, 2));
  assert(!syzygy::pipewire::copy_memory_frame(frame, nullptr, 24, 0, 12, 2, 2));
  assert(frame == saved);
  std::cout << "PASS: owned frame lifetime, padded stride, offsets and malformed buffer bounds\n";
}
