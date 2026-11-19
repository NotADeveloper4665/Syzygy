#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace syzygy::core {

using monotonic_clock = std::chrono::steady_clock;

struct telemetry_sample {
  monotonic_clock::time_point observed_at {};

  // All durations are milliseconds. Absence means the source is unavailable.
  std::optional<double> capture_ms;
  std::optional<double> encode_ms;
  std::optional<double> send_queue_ms;

  // Combined capture-to-network-broadcast host pipeline time. This cannot
  // distinguish capture, conversion, encoding, or queueing individually.
  std::optional<double> host_pipeline_ms;
  std::optional<double> network_rtt_ms;
  std::optional<double> network_jitter_ms;

  // Packet loss is per mille (0..1000), not a raw packet count.
  std::optional<double> packet_loss_per_mille;

  // Reserved for an explicitly supported client telemetry extension.
  std::optional<double> client_decode_ms;
  std::optional<double> client_presentation_ms;
};

struct telemetry_policy {
  std::chrono::milliseconds freshness_window {3000};
  double capture_threshold_ms {8.0};
  double encode_threshold_ms {8.0};
  double send_queue_threshold_ms {8.0};
  double host_pipeline_threshold_ms {20.0};
  double network_rtt_threshold_ms {80.0};
  double network_jitter_threshold_ms {15.0};
  double packet_loss_threshold_per_mille {20.0};
  double client_decode_threshold_ms {8.0};
  double client_presentation_threshold_ms {8.0};
  unsigned int minimum_observed_stages {2};
};

enum class bottleneck {
  healthy,
  capture_bound,
  encode_bound,
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
