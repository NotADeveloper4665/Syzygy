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
  telemetry_sample sample;
  sample.observed_at = now;
  sample.host_pipeline_ms = 24.0;

  const auto pressure = syzygy::core::assess_pipeline(sample, telemetry_policy {}, now);
  assert(pressure.classification == bottleneck::host_pipeline_pressure);
  assert(pressure.certainty == confidence::low);
  assert(pressure.observe_only);
  assert(!pressure.evidence.empty());

  sample.host_pipeline_ms = std::numeric_limits<double>::quiet_NaN();
  assert(!syzygy::core::valid_sample(sample));

  sample.host_pipeline_ms.reset();
  sample.capture_ms = 1.0;
  const auto incomplete = syzygy::core::assess_pipeline(sample, telemetry_policy {}, now);
  assert(incomplete.classification == bottleneck::insufficient_data);

  sample.capture_ms = 1.0;
  sample.encode_ms = 2.0;
  sample.send_queue_ms = 1.0;
  sample.host_pipeline_ms = 2.0;
  const auto healthy = syzygy::core::assess_pipeline(sample, telemetry_policy {}, now);
  assert(healthy.classification == bottleneck::healthy);

  return 0;
}
