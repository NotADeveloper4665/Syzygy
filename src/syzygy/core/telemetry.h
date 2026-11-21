#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace syzygy::core {

using monotonic_clock = std::chrono::steady_clock;

// ENet's lastReceiveTime is a service-clock token updated with acknowledged
// packets. Do not refresh the Core sample timestamp when polling the same token.
inline bool accept_enet_rtt_sample_token(
    std::uint32_t sample_token,
    std::uint32_t& last_sample_token) noexcept {
  if (sample_token == 0 || sample_token == last_sample_token) return false;
  last_sample_token = sample_token;
  return true;
}

struct telemetry_sample {
  // Host measurements are refreshed when an encoded frame is observed by the
  // broadcaster. Network, loss, and client measurements have independent
  // timestamps so a fresh frame cannot make stale feedback look current.
  monotonic_clock::time_point observed_at {};
  monotonic_clock::time_point network_observed_at {};
  monotonic_clock::time_point media_jitter_observed_at {};
  monotonic_clock::time_point loss_observed_at {};
  monotonic_clock::time_point client_observed_at {};

  // All durations are milliseconds. Absence means the source is unavailable.
  // capture_ms is capture-timestamp to encoder consumption age and includes
  // mailbox wait; capture_backend_ms is a separately timed platform callback.
  std::optional<double> capture_ms;
  std::optional<double> capture_backend_ms;
  std::optional<double> conversion_ms;
  std::optional<double> encode_ms;
  std::optional<double> send_queue_ms;
  // Sender-stage timings; all are host-local monotonic durations in ms.
  std::optional<double> packetization_ms;
  std::optional<double> fec_encode_ms;
  std::optional<double> sender_pacing_wait_ms;
  std::optional<double> socket_submit_ms;
  std::optional<double> frame_first_packet_ms;

  // Combined capture-to-network-broadcast host pipeline time. This overlaps
  // detailed host stages and is never counted as an independent stage.
  std::optional<double> host_pipeline_ms;

  // ENet control-channel estimate. RTT variance is a path-variation proxy,
  // not one-way media-packet jitter.
  std::optional<double> network_rtt_ms;
  std::optional<double> network_rtt_variance_ms;
  // Client-reported RTP inter-arrival jitter (RFC 3550), not ENet RTT variance.
  std::optional<double> client_media_jitter_ms;

  // Per-mille data-shard loss from a client FEC recovery report. Reports are
  // emitted only for blocks requiring recovery; absence is unknown, not zero.
  std::optional<double> packet_loss_per_mille;

  // Legacy count/time feedback is retained without inventing a denominator.
  std::optional<std::uint64_t> packet_loss_count;
  // Present only for FEC recovery-block reports; legacy loss stats have no
  // verified packet denominator.
  std::optional<std::uint64_t> packet_loss_total_data_packets;
  std::optional<double> packet_loss_interval_ms;

  // Explicit client feedback, in milliseconds. Presentation is queue + render.
  std::optional<double> client_decode_ms;
  std::optional<double> client_presentation_queue_ms;
  std::optional<double> client_render_ms;
  std::optional<double> client_presentation_ms;
};

struct telemetry_policy {
  std::chrono::milliseconds freshness_window {3000};
  double capture_threshold_ms {8.0};
  double capture_backend_threshold_ms {8.0};
  double conversion_threshold_ms {8.0};
  double encode_threshold_ms {8.0};
  double send_queue_threshold_ms {8.0};
  double host_pipeline_threshold_ms {20.0};
  double network_rtt_threshold_ms {80.0};
  double network_rtt_variance_threshold_ms {15.0};
  double media_jitter_threshold_ms {5.0};
  double packet_loss_threshold_per_mille {20.0};
  double client_decode_threshold_ms {8.0};
  double client_presentation_threshold_ms {8.0};
  unsigned int minimum_observed_stages {2};
};

enum class bottleneck {
  healthy,
  capture_bound,
  conversion_bound,
  encode_bound,
  sender_queue_bound,
  network_bound,
  host_pipeline_pressure,
  client_bound,
  mixed,
  insufficient_data,
};

enum class confidence {
  low,
  medium,
  high,
};

struct pipeline_assessment {
  bottleneck classification {bottleneck::insufficient_data};
  confidence certainty {confidence::low};
  std::string explanation;
  std::vector<std::string> evidence;
  bool observe_only {true};
};

std::string_view to_string(bottleneck value) noexcept;
std::string_view to_string(confidence value) noexcept;

bool valid_sample(const telemetry_sample& sample) noexcept;
bool valid_policy(const telemetry_policy& policy) noexcept;

pipeline_assessment assess_pipeline(
    const telemetry_sample& sample,
    const telemetry_policy& policy,
    monotonic_clock::time_point now = monotonic_clock::now());

}  // namespace syzygy::core
