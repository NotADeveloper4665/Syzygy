#include "telemetry.h"

#include <cmath>

namespace syzygy::core {
namespace {

bool valid_duration(const std::optional<double>& value) {
  return !value || (std::isfinite(*value) && *value >= 0.0 && *value <= 5000.0);
}

bool valid_loss(const std::optional<double>& value) {
  return !value || (std::isfinite(*value) && *value >= 0.0 && *value <= 1000.0);
}

bool elevated(const std::optional<double>& value, double threshold) {
  return value && *value >= threshold;
}

}  // namespace

bool valid_sample(const telemetry_sample& sample) noexcept {
  return valid_duration(sample.capture_ms) &&
         valid_duration(sample.encode_ms) &&
         valid_duration(sample.send_queue_ms) &&
         valid_duration(sample.network_rtt_ms) &&
         valid_duration(sample.network_jitter_ms) &&
         valid_loss(sample.packet_loss_per_mille) &&
         valid_duration(sample.client_decode_ms) &&
         valid_duration(sample.client_presentation_ms);
}

bool valid_policy(const telemetry_policy& policy) noexcept {
  const auto valid_threshold = [](double value, double maximum = 5000.0) {
    return std::isfinite(value) && value > 0.0 && value <= maximum;
  };
  return policy.freshness_window > std::chrono::milliseconds::zero() &&
         policy.freshness_window <= std::chrono::minutes(5) &&
         valid_threshold(policy.capture_threshold_ms) &&
         valid_threshold(policy.encode_threshold_ms) &&
         valid_threshold(policy.send_queue_threshold_ms) &&
         valid_threshold(policy.network_rtt_threshold_ms) &&
         valid_threshold(policy.network_jitter_threshold_ms) &&
         valid_threshold(policy.packet_loss_threshold_per_mille, 1000.0) &&
         valid_threshold(policy.client_decode_threshold_ms) &&
         valid_threshold(policy.client_presentation_threshold_ms) &&
         policy.minimum_observed_stages >= 1 &&
         policy.minimum_observed_stages <= 8;
}

pipeline_assessment assess_pipeline(
    const telemetry_sample& sample,
    const telemetry_policy& policy,
    monotonic_clock::time_point now) {
  pipeline_assessment result;

  if (!valid_policy(policy)) {
    result.explanation = "Telemetry policy is invalid.";
    return result;
  }
  if (!valid_sample(sample)) {
    result.explanation = "Telemetry sample contains an invalid measurement.";
    return result;
  }
  if (sample.observed_at.time_since_epoch().count() == 0 ||
      now < sample.observed_at ||
      now - sample.observed_at > policy.freshness_window) {
    result.explanation = "Telemetry sample is missing a valid timestamp or is stale.";
    return result;
  }

  unsigned int observed = 0;
  bool capture = false;
  bool encode = false;
  bool network = false;
  bool client = false;

  const bool capture_observed = sample.capture_ms.has_value();
  const bool encode_observed = sample.encode_ms.has_value();
  const bool network_observed = sample.send_queue_ms.has_value() ||
                                sample.network_rtt_ms.has_value() ||
                                sample.network_jitter_ms.has_value() ||
                                sample.packet_loss_per_mille.has_value();
  const bool client_observed = sample.client_decode_ms.has_value() ||
                               sample.client_presentation_ms.has_value();
  observed = static_cast<unsigned int>(capture_observed) +
             static_cast<unsigned int>(encode_observed) +
             static_cast<unsigned int>(network_observed) +
             static_cast<unsigned int>(client_observed);

  capture = elevated(sample.capture_ms, policy.capture_threshold_ms);
  encode = elevated(sample.encode_ms, policy.encode_threshold_ms);
  network = elevated(sample.send_queue_ms, policy.send_queue_threshold_ms) ||
            elevated(sample.network_rtt_ms, policy.network_rtt_threshold_ms) ||
            elevated(sample.network_jitter_ms, policy.network_jitter_threshold_ms) ||
            elevated(sample.packet_loss_per_mille, policy.packet_loss_threshold_per_mille);
  client = elevated(sample.client_decode_ms, policy.client_decode_threshold_ms) ||
           elevated(sample.client_presentation_ms, policy.client_presentation_threshold_ms);

  const auto elevated_count = static_cast<unsigned int>(capture) +
                             static_cast<unsigned int>(encode) +
                             static_cast<unsigned int>(network) +
                             static_cast<unsigned int>(client);

  if (observed < policy.minimum_observed_stages) {
    result.explanation = "Not enough fresh measurements to classify the pipeline.";
    return result;
  }

  if (capture) result.evidence.emplace_back("Capture latency crossed its configured threshold.");
  if (encode) result.evidence.emplace_back("Encode latency crossed its configured threshold.");
  if (network) result.evidence.emplace_back("Send or network telemetry crossed its configured threshold.");
  if (client) result.evidence.emplace_back("Client decode or presentation telemetry crossed its configured threshold.");

  if (elevated_count > 1) {
    result.classification = bottleneck::mixed;
    result.certainty = confidence::medium;
    result.explanation = "Multiple pipeline stages are elevated; no single bottleneck is established.";
  } else if (capture) {
    result.classification = bottleneck::capture_bound;
    result.certainty = confidence::medium;
    result.explanation = "Capture latency is elevated.";
  } else if (encode) {
    result.classification = bottleneck::encode_bound;
    result.certainty = confidence::medium;
    result.explanation = "Encode latency is elevated.";
  } else if (network) {
    result.classification = bottleneck::network_bound;
    result.certainty = confidence::medium;
    result.explanation = "Send or network telemetry indicates congestion.";
  } else if (client) {
    result.classification = bottleneck::client_bound;
    result.certainty = confidence::medium;
    result.explanation = "Explicit client decode or presentation telemetry is elevated.";
  } else {
    result.classification = bottleneck::healthy;
    result.certainty = observed >= 4 ? confidence::high : confidence::medium;
    result.explanation = "No observed stage crossed its configured threshold.";
  }

  result.observe_only = true;
  return result;
}

}  // namespace syzygy::core
