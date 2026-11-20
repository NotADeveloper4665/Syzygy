#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace syzygy::core {

struct fec_frame_status {
  std::uint32_t frame_index {};
  std::uint16_t highest_received_sequence {};
  std::uint16_t next_contiguous_sequence {};
  std::uint16_t missing_before_highest {};
  std::uint16_t total_data_packets {};
  std::uint16_t total_parity_packets {};
  std::uint16_t received_data_packets {};
  std::uint16_t received_parity_packets {};
  std::uint8_t fec_percentage {};
  std::uint8_t block_index {};
  std::uint8_t block_count {};
  std::uint32_t lost_data_packets {};
  double loss_per_mille {};
};

struct legacy_loss_stats {
  std::uint32_t loss_count {};
  std::uint32_t interval_ms {};
};

// Frame indices are 32-bit serial numbers. Use half-range arithmetic so
// recent feedback remains valid across the uint32 wrap boundary.
inline bool frame_index_is_recent(
    std::uint32_t candidate,
    std::uint32_t latest,
    std::uint32_t max_age = 512) noexcept {
  if (candidate == 0 || latest == 0 || max_age >= 0x80000000u) return false;
  const auto age = latest - candidate;
  return age <= max_age && age < 0x80000000u;
}

inline bool frame_index_is_newer(
    std::uint32_t candidate,
    std::uint32_t previous) noexcept {
  if (candidate == 0 || previous == 0 || candidate == previous) return false;
  const auto distance = candidate - previous;
  return distance < 0x80000000u;
}

struct client_telemetry_report {
  std::uint8_t version {};
  std::uint32_t last_frame_index {};
  std::uint16_t sample_count {};
  std::uint16_t window_ms {};
  double decode_ms {};
  double presentation_queue_ms {};
  double render_ms {};
  double presentation_ms {};
  // Present only in v2: RFC 3550 RTP inter-arrival jitter, converted to ms
  // using the negotiated RTP clock rate. This is not ENet RTT variance.
  std::optional<double> media_interarrival_jitter_ms;
};

// Keep the v1 type name available for source compatibility with existing tests.
using client_telemetry_v1 = client_telemetry_report;

namespace detail {
inline std::uint16_t read_be16(const unsigned char* p) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

inline std::uint32_t read_be32(const unsigned char* p) noexcept {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

inline std::uint32_t read_le32(const unsigned char* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}
}  // namespace detail

// Moonlight SS_FRAME_FEC_STATUS (0x5502). Multi-byte fields are network
// byte order. The report is emitted for FEC blocks that required recovery;
// absence of a report is unknown, not a zero-loss sample.
inline std::optional<fec_frame_status> parse_fec_frame_status(std::string_view payload) noexcept {
  constexpr std::size_t expected_size = 21;
  if (payload.size() != expected_size) return std::nullopt;

  const auto* p = reinterpret_cast<const unsigned char*>(payload.data());
  fec_frame_status result;
  result.frame_index = detail::read_be32(p + 0);
  result.highest_received_sequence = detail::read_be16(p + 4);
  result.next_contiguous_sequence = detail::read_be16(p + 6);
  result.missing_before_highest = detail::read_be16(p + 8);
  result.total_data_packets = detail::read_be16(p + 10);
  result.total_parity_packets = detail::read_be16(p + 12);
  result.received_data_packets = detail::read_be16(p + 14);
  result.received_parity_packets = detail::read_be16(p + 16);
  result.fec_percentage = p[18];
  result.block_index = p[19];
  result.block_count = p[20];

  if (result.frame_index == 0 ||
      result.total_data_packets == 0 ||
      result.received_data_packets > result.total_data_packets ||
      result.received_parity_packets > result.total_parity_packets ||
      result.fec_percentage > 100 ||
      result.block_count == 0 || result.block_count > 4 ||
      result.block_index >= result.block_count) {
    return std::nullopt;
  }

  result.lost_data_packets = result.total_data_packets - result.received_data_packets;
  result.loss_per_mille =
      (static_cast<double>(result.lost_data_packets) * 1000.0) /
      static_cast<double>(result.total_data_packets);
  return result;
}

// Legacy 32-byte IDX_LOSS_STATS payload. The first two fields are little-endian
// count and elapsed milliseconds. They are preserved as raw observations;
// no rate is inferred because the payload has no verified packet denominator.
inline std::optional<legacy_loss_stats> parse_legacy_loss_stats(std::string_view payload) noexcept {
  constexpr std::size_t expected_size = 32;
  if (payload.size() != expected_size) return std::nullopt;

  const auto* p = reinterpret_cast<const unsigned char*>(payload.data());
  legacy_loss_stats result {
    detail::read_le32(p + 0),
    detail::read_le32(p + 4),
  };
  if (result.interval_ms == 0 || result.interval_ms > 60000) {
    return std::nullopt;
  }
  return result;
}

// Syzygy client telemetry extension 0x5506. Both versions use big-endian
// fields and the "SZY1" family magic. V1 is 28 bytes; V2 appends a 32-bit
// RFC 3550 media inter-arrival jitter average in microseconds (32 bytes total).
// All durations are client-measured intervals; no cross-device clock sync is used.
inline std::optional<client_telemetry_report> parse_client_telemetry(std::string_view payload) noexcept {
  if (payload.size() < 8) return std::nullopt;

  const auto* p = reinterpret_cast<const unsigned char*>(payload.data());
  const auto version = p[4];
  const std::size_t expected_size = version == 1 ? 28 : version == 2 ? 32 : 0;
  if (expected_size == 0 || payload.size() != expected_size ||
      p[0] != 'S' || p[1] != 'Z' || p[2] != 'Y' || p[3] != '1' ||
      p[5] != 0 || detail::read_be16(p + 6) != expected_size) {
    return std::nullopt;
  }

  client_telemetry_report result;
  result.version = version;
  result.last_frame_index = detail::read_be32(p + 8);
  result.sample_count = detail::read_be16(p + 12);
  result.window_ms = detail::read_be16(p + 14);
  const auto decode_us = detail::read_be32(p + 16);
  const auto queue_us = detail::read_be32(p + 20);
  const auto render_us = detail::read_be32(p + 24);

  constexpr std::uint32_t max_duration_us = 5000000;
  if (result.last_frame_index == 0 ||
      result.sample_count == 0 || result.sample_count > 240 ||
      result.window_ms == 0 || result.window_ms > 5000 ||
      decode_us > max_duration_us ||
      queue_us > max_duration_us ||
      render_us > max_duration_us ||
      queue_us + static_cast<std::uint64_t>(render_us) > max_duration_us) {
    return std::nullopt;
  }

  result.decode_ms = static_cast<double>(decode_us) / 1000.0;
  result.presentation_queue_ms = static_cast<double>(queue_us) / 1000.0;
  result.render_ms = static_cast<double>(render_us) / 1000.0;
  result.presentation_ms = result.presentation_queue_ms + result.render_ms;

  if (version == 2) {
    const auto jitter_us = detail::read_be32(p + 28);
    if (jitter_us > max_duration_us) return std::nullopt;
    result.media_interarrival_jitter_ms = static_cast<double>(jitter_us) / 1000.0;
  }

  return result;
}

inline std::optional<client_telemetry_v1> parse_client_telemetry_v1(std::string_view payload) noexcept {
  auto result = parse_client_telemetry(payload);
  if (!result || result->version != 1) return std::nullopt;
  return result;
}

inline std::optional<client_telemetry_report> parse_client_telemetry_v2(std::string_view payload) noexcept {
  auto result = parse_client_telemetry(payload);
  if (!result || result->version != 2) return std::nullopt;
  return result;
}

}  // namespace syzygy::core
