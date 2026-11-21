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

bool valid_interval(const std::optional<double>& value) {
  return !value || (std::isfinite(*value) && *value >= 1.0 && *value <= 60000.0);
}

bool valid_loss_counts(const telemetry_sample& sample) {
  if (!sample.packet_loss_total_data_packets) return true;
  return *sample.packet_loss_total_data_packets > 0 &&
         sample.packet_loss_count.has_value() &&
         *sample.packet_loss_count <= *sample.packet_loss_total_data_packets;
}

bool elevated(const std::optional<double>& value, double threshold) {
  return value && *value >= threshold;
}

bool fresh(
    monotonic_clock::time_point observed_at,
    monotonic_clock::time_point now,
    std::chrono::milliseconds window) {
  return observed_at.time_since_epoch().count() != 0 &&
         now >= observed_at &&
         now - observed_at <= window;
}

}  // namespace

std::string_view to_string(bottleneck value) noexcept {
  switch (value) {
    case bottleneck::healthy: return "healthy";
    case bottleneck::capture_bound: return "capture_bound";
    case bottleneck::conversion_bound: return "conversion_bound";
    case bottleneck::encode_bound: return "encode_bound";
    case bottleneck::sender_queue_bound: return "sender_queue_bound";
    case bottleneck::network_bound: return "network_bound";
    case bottleneck::host_pipeline_pressure: return "host_pipeline_pressure";
    case bottleneck::client_bound: return "client_bound";
    case bottleneck::mixed: return "mixed";
    case bottleneck::insufficient_data: return "insufficient_data";
  }
  return "unknown";
}

std::string_view to_string(confidence value) noexcept {
  switch (value) {
    case confidence::low: return "low";
    case confidence::medium: return "medium";
    case confidence::high: return "high";
  }
  return "unknown";
}

bool valid_sample(const telemetry_sample& sample) noexcept {
  return valid_duration(sample.capture_ms) &&
         valid_duration(sample.capture_backend_ms) &&
         valid_duration(sample.conversion_ms) &&
         valid_duration(sample.encode_ms) &&
         valid_duration(sample.send_queue_ms) &&
         valid_duration(sample.packetization_ms) &&
         valid_duration(sample.fec_encode_ms) &&
         valid_duration(sample.encryption_ms) &&
         valid_duration(sample.sender_pacing_wait_ms) &&
         valid_duration(sample.socket_submit_ms) &&
         valid_duration(sample.frame_first_packet_ms) &&
         valid_duration(sample.host_pipeline_ms) &&
         valid_duration(sample.network_rtt_ms) &&
         valid_duration(sample.network_rtt_variance_ms) &&
         valid_duration(sample.client_media_jitter_ms) &&
         valid_loss(sample.packet_loss_per_mille) &&
         valid_loss_counts(sample) &&
         valid_interval(sample.packet_loss_interval_ms) &&
         valid_duration(sample.client_decode_ms) &&
         valid_duration(sample.client_presentation_queue_ms) &&
         valid_duration(sample.client_render_ms) &&
         valid_duration(sample.client_presentation_ms);
}

bool valid_policy(const telemetry_policy& policy) noexcept {
  const auto valid_threshold = [](double value, double maximum = 5000.0) {
    return std::isfinite(value) && value > 0.0 && value <= maximum;
  };
  return policy.freshness_window > std::chrono::milliseconds::zero() &&
         policy.freshness_window <= std::chrono::minutes(5) &&
         valid_threshold(policy.capture_threshold_ms) &&
         valid_threshold(policy.capture_backend_threshold_ms) &&
         valid_threshold(policy.conversion_threshold_ms) &&
         valid_threshold(policy.encode_threshold_ms) &&
         valid_threshold(policy.send_queue_threshold_ms) &&
         valid_threshold(policy.host_pipeline_threshold_ms) &&
         valid_threshold(policy.network_rtt_threshold_ms) &&
         valid_threshold(policy.network_rtt_variance_threshold_ms) &&
         valid_threshold(policy.media_jitter_threshold_ms) &&
         valid_threshold(policy.packet_loss_threshold_per_mille, 1000.0) &&
         valid_threshold(policy.client_decode_threshold_ms) &&
         valid_threshold(policy.client_presentation_threshold_ms) &&
         policy.minimum_observed_stages >= 1 &&
         policy.minimum_observed_stages <= 6;
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

  const bool host_has_data = sample.capture_ms || sample.capture_backend_ms ||
                             sample.conversion_ms || sample.encode_ms ||
                             sample.send_queue_ms || sample.host_pipeline_ms;
  const bool network_has_data = sample.network_rtt_ms || sample.network_rtt_variance_ms;
  const bool loss_has_data = sample.packet_loss_per_mille ||
                             sample.packet_loss_count || sample.packet_loss_interval_ms;
  const bool client_has_data = sample.client_decode_ms ||
                               sample.client_presentation_queue_ms ||
                               sample.client_render_ms || sample.client_presentation_ms;

  const bool host_fresh = host_has_data && fresh(sample.observed_at, now, policy.freshness_window);
  const bool network_fresh = network_has_data && fresh(sample.network_observed_at, now, policy.freshness_window);
  const bool media_jitter_fresh = sample.client_media_jitter_ms.has_value() &&
      fresh(sample.media_jitter_observed_at, now, policy.freshness_window);
  const bool loss_fresh = loss_has_data && fresh(sample.loss_observed_at, now, policy.freshness_window);
  const bool client_fresh = client_has_data && fresh(sample.client_observed_at, now, policy.freshness_window);

  const bool capture_observed = host_fresh && (sample.capture_ms || sample.capture_backend_ms);
  const bool conversion_observed = host_fresh && sample.conversion_ms.has_value();
  const bool encode_observed = host_fresh && sample.encode_ms.has_value();
  const bool sender_queue_observed = host_fresh && sample.send_queue_ms.has_value();
  // FEC status is emitted only for blocks that needed recovery. It is
  // useful diagnostic evidence, but it is not an unbiased stream-wide loss
  // sample and must not independently satisfy or elevate the network stage.
  const bool network_observed = (network_fresh && (sample.network_rtt_ms || sample.network_rtt_variance_ms)) ||
                                (media_jitter_fresh && sample.client_media_jitter_ms.has_value());
  const bool client_observed = client_fresh && client_has_data;

  const unsigned int observed = static_cast<unsigned int>(capture_observed) +
                                static_cast<unsigned int>(conversion_observed) +
                                static_cast<unsigned int>(encode_observed) +
                                static_cast<unsigned int>(sender_queue_observed) +
                                static_cast<unsigned int>(network_observed) +
                                static_cast<unsigned int>(client_observed);

  const bool capture = capture_observed &&
      (elevated(sample.capture_backend_ms, policy.capture_backend_threshold_ms) ||
       elevated(sample.capture_ms, policy.capture_threshold_ms));
  const bool conversion = conversion_observed && elevated(sample.conversion_ms, policy.conversion_threshold_ms);
  const bool encode = encode_observed && elevated(sample.encode_ms, policy.encode_threshold_ms);
  const bool sender_queue = sender_queue_observed && elevated(sample.send_queue_ms, policy.send_queue_threshold_ms);
  const bool network =
      (network_fresh && (elevated(sample.network_rtt_ms, policy.network_rtt_threshold_ms) ||
                         elevated(sample.network_rtt_variance_ms, policy.network_rtt_variance_threshold_ms))) ||
      (media_jitter_fresh && elevated(sample.client_media_jitter_ms, policy.media_jitter_threshold_ms));
  const bool host_pipeline_elevated = host_fresh && elevated(sample.host_pipeline_ms, policy.host_pipeline_threshold_ms);
  const bool client = client_observed &&
      (elevated(sample.client_decode_ms, policy.client_decode_threshold_ms) ||
       elevated(sample.client_presentation_ms, policy.client_presentation_threshold_ms) ||
       elevated(sample.client_presentation_queue_ms, policy.client_presentation_threshold_ms) ||
       elevated(sample.client_render_ms, policy.client_presentation_threshold_ms));

  // The aggregate host measurement overlaps capture/conversion/encode/queue;
  // use it as fallback or corroboration, never as a separate stage.
  const unsigned int detailed_elevated_count =
      static_cast<unsigned int>(capture) +
      static_cast<unsigned int>(conversion) +
      static_cast<unsigned int>(encode) +
      static_cast<unsigned int>(sender_queue) +
      static_cast<unsigned int>(network) +
      static_cast<unsigned int>(client);
  const bool host_pipeline_fallback = host_pipeline_elevated && detailed_elevated_count == 0;
  const unsigned int elevated_count = detailed_elevated_count +
                                      static_cast<unsigned int>(host_pipeline_fallback);

  if (observed < policy.minimum_observed_stages && !host_pipeline_fallback) {
    result.explanation = "Not enough fresh measurements to classify the pipeline.";
    return result;
  }

  // A single stage is isolated only when capture, encoder, sender queue, and
  // network feedback are all fresh. Otherwise report insufficient evidence.
  const bool complete_pipeline_evidence = capture_observed && encode_observed &&
                                          sender_queue_observed && network_observed;
  if (detailed_elevated_count == 1 && !complete_pipeline_evidence) {
    result.explanation = "A single elevated stage cannot be isolated because capture, encode, sender-queue, or network measurements are incomplete.";
    return result;
  }
  if (detailed_elevated_count == 0 && !host_pipeline_fallback && !complete_pipeline_evidence) {
    result.explanation = "Healthy status requires fresh capture, encode, sender-queue, and network measurements.";
    return result;
  }

  if (capture) {
    if (elevated(sample.capture_backend_ms, policy.capture_backend_threshold_ms)) {
      result.evidence.emplace_back("Capture backend callback duration crossed its configured threshold.");
    } else {
      result.evidence.emplace_back("Capture-to-encoder age crossed its configured threshold; this includes mailbox wait and is not pure backend execution time.");
    }
  }
  if (conversion) result.evidence.emplace_back("Image conversion duration crossed its configured threshold.");
  if (encode) result.evidence.emplace_back("Encoder submission-to-output latency crossed its configured threshold.");
  if (sender_queue) result.evidence.emplace_back("Internal encoded-packet queue wait crossed its configured threshold; this is host sender pressure, not network transit.");
  if (network_fresh && (sample.network_rtt_ms || sample.network_rtt_variance_ms)) {
    result.evidence.emplace_back("ENet control-channel RTT/variance telemetry is fresh; variance is a path-variation proxy, not media-packet jitter.");
  }
  if (media_jitter_fresh && sample.client_media_jitter_ms) {
    result.evidence.emplace_back("Client-reported RTP inter-arrival jitter (RFC 3550) is fresh; it is distinct from ENet control RTT variance.");
  }
  if (loss_fresh && sample.packet_loss_per_mille) {
    result.evidence.emplace_back("Client FEC report measured data-shard loss before recovery for a reported block; no report means unknown, not zero.");
  }
  if (loss_fresh && sample.packet_loss_count) {
    result.evidence.emplace_back("Legacy client loss count is available without a verified denominator; no loss percentage was inferred.");
  }
  if (host_pipeline_elevated) {
    result.evidence.emplace_back("Combined capture-to-broadcast host time crossed its threshold; it overlaps detailed host stages and is not counted twice.");
  }
  if (client) result.evidence.emplace_back("Explicit client decode or presentation feedback crossed its configured threshold.");

  if (elevated_count > 1) {
    result.classification = bottleneck::mixed;
    result.certainty = confidence::medium;
    result.explanation = "Multiple independently measured pipeline stages are elevated; no single bottleneck is established.";
  } else if (capture) {
    result.classification = bottleneck::capture_bound;
    result.certainty = confidence::medium;
    result.explanation = "Capture backend duration or capture-to-encoder age is elevated.";
  } else if (conversion) {
    result.classification = bottleneck::conversion_bound;
    result.certainty = confidence::medium;
    result.explanation = "Image conversion duration is elevated.";
  } else if (encode) {
    result.classification = bottleneck::encode_bound;
    result.certainty = confidence::medium;
    result.explanation = "Encoder submission-to-output latency is elevated.";
  } else if (sender_queue) {
    result.classification = bottleneck::sender_queue_bound;
    result.certainty = confidence::medium;
    result.explanation = "Internal encoded-packet queue wait is elevated.";
  } else if (network) {
    result.classification = bottleneck::network_bound;
    result.certainty = confidence::medium;
    result.explanation = "Control-path RTT/variance or client-reported media inter-arrival jitter indicates network pressure.";
  } else if (host_pipeline_fallback) {
    result.classification = bottleneck::host_pipeline_pressure;
    result.certainty = confidence::low;
    result.explanation = "Combined host-side frame processing time is elevated; detailed stages cannot be distinguished from this measurement.";
  } else if (client) {
    result.classification = bottleneck::client_bound;
    result.certainty = confidence::medium;
    result.explanation = "Explicit client decode or presentation telemetry is elevated.";
  } else {
    result.classification = bottleneck::healthy;
    result.certainty = observed >= 5 ? confidence::high : confidence::medium;
    result.explanation = "No independently observed stage crossed its configured threshold.";
  }

  result.observe_only = true;
  return result;
}

}  // namespace syzygy::core
