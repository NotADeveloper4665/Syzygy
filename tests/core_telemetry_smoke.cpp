#include <cassert>
#include <chrono>
#include <limits>

#include "src/syzygy/core/telemetry.h"

int main() {
  using namespace std::chrono_literals;
  using syzygy::core::bottleneck;
  using syzygy::core::confidence;
  using syzygy::core::monotonic_clock;
  using syzygy::core::telemetry_policy;
  using syzygy::core::telemetry_sample;

  const auto now = monotonic_clock::now();
  telemetry_policy policy;
  telemetry_sample sample;
  sample.observed_at = now;
  sample.host_pipeline_ms = 24.0;

  auto pressure = syzygy::core::assess_pipeline(sample, policy, now);
  assert(pressure.classification == bottleneck::host_pipeline_pressure);
  assert(pressure.certainty == confidence::low);
  assert(pressure.observe_only);
  assert(!pressure.evidence.empty());

  sample.host_pipeline_ms = std::numeric_limits<double>::quiet_NaN();
  assert(!syzygy::core::valid_sample(sample));

  sample = {};
  sample.observed_at = now;
  sample.capture_ms = 1.0;
  auto incomplete = syzygy::core::assess_pipeline(sample, policy, now);
  assert(incomplete.classification == bottleneck::insufficient_data);

  sample.encode_ms = 2.0;
  sample.send_queue_ms = 1.0;
  sample.network_rtt_ms = 10.0;
  sample.network_rtt_variance_ms = 1.0;
  sample.network_observed_at = now;
  auto healthy = syzygy::core::assess_pipeline(sample, policy, now);
  assert(healthy.classification == bottleneck::healthy);

  sample.capture_backend_ms = 12.0;
  auto capture = syzygy::core::assess_pipeline(sample, policy, now);
  assert(capture.classification == bottleneck::capture_bound);

  sample.capture_backend_ms = 1.0;
  sample.conversion_ms = 12.0;
  auto conversion = syzygy::core::assess_pipeline(sample, policy, now);
  assert(conversion.classification == bottleneck::conversion_bound);

  sample.conversion_ms = 1.0;
  sample.encode_ms = 12.0;
  auto encode = syzygy::core::assess_pipeline(sample, policy, now);
  assert(encode.classification == bottleneck::encode_bound);

  sample.encode_ms = 1.0;
  sample.send_queue_ms = 12.0;
  auto sender_queue = syzygy::core::assess_pipeline(sample, policy, now);
  assert(sender_queue.classification == bottleneck::sender_queue_bound);

  sample.send_queue_ms = 1.0;
  sample.network_rtt_variance_ms = 20.0;
  auto network = syzygy::core::assess_pipeline(sample, policy, now);
  assert(network.classification == bottleneck::network_bound);

  sample.network_rtt_variance_ms = 1.0;
  sample.packet_loss_per_mille = 50.0;
  sample.loss_observed_at = now;
  auto loss = syzygy::core::assess_pipeline(sample, policy, now);
  assert(loss.classification == bottleneck::network_bound);

  sample.packet_loss_per_mille.reset();
  sample.client_decode_ms = 12.0;
  sample.client_presentation_ms = 2.0;
  sample.client_observed_at = now;
  auto client = syzygy::core::assess_pipeline(sample, policy, now);
  assert(client.classification == bottleneck::client_bound);

  sample.client_decode_ms = 12.0;
  sample.network_rtt_ms = 100.0;
  auto mixed = syzygy::core::assess_pipeline(sample, policy, now);
  assert(mixed.classification == bottleneck::mixed);

  sample.client_decode_ms.reset();
  sample.client_presentation_ms.reset();
  sample.client_observed_at = {};
  sample.network_rtt_ms.reset();
  sample.network_rtt_variance_ms.reset();
  sample.network_observed_at = {};
  sample.observed_at = now - 4s;
  auto stale_host = syzygy::core::assess_pipeline(sample, policy, now);
  assert(stale_host.classification == bottleneck::insufficient_data);

  sample.observed_at = now;
  sample.network_rtt_ms = 10.0;
  sample.network_observed_at = now - 4s;
  auto stale_network = syzygy::core::assess_pipeline(sample, policy, now);
  assert(stale_network.classification == bottleneck::insufficient_data);

  sample = {};
  sample.observed_at = now;
  sample.capture_ms = 1.0;
  sample.encode_ms = 1.0;
  sample.send_queue_ms = 1.0;
  sample.network_rtt_ms = 1.0;
  sample.network_observed_at = now;
  sample.packet_loss_count = 3;
  sample.packet_loss_interval_ms = 50.0;
  sample.loss_observed_at = now;
  auto raw_loss = syzygy::core::assess_pipeline(sample, policy, now);
  assert(raw_loss.classification == bottleneck::healthy);
  assert(raw_loss.evidence.size() >= 1);

  return 0;
}
