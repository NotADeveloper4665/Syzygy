#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace syzygy::pipewire {
  // Copy while the PipeWire buffer is held. Encoders must never retain a pointer
  // to memory that the producer can recycle on the next frame.
  inline bool copy_memory_frame(std::vector<std::uint8_t> &destination,
      const void *source, std::size_t size, std::size_t offset,
      int stride, int width, int height) {
    if (!source || width <= 0 || height <= 0 || stride <= 0 || offset > size) return false;
    const auto row_size = static_cast<std::size_t>(width) * 4;
    const auto pitch = static_cast<std::size_t>(stride);
    const auto rows = static_cast<std::size_t>(height);
    const auto available = size - offset;
    if (row_size > pitch || row_size > available ||
        rows - 1 > (available - row_size) / pitch ||
        rows > destination.max_size() / row_size) return false;
    destination.resize(rows * row_size);
    const auto *pixels = static_cast<const std::uint8_t *>(source) + offset;
    for (std::size_t row = 0; row < rows; ++row) {
      std::memcpy(destination.data() + row * row_size, pixels + row * pitch, row_size);
    }
    return true;
  }
}
