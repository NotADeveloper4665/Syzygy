#include "../tests_common.h"

#include <limits>

#include <src/syzygy/core/telemetry.h>

using namespace std::chrono_literals;
using syzygy::core::bottleneck;
using syzygy::core::monotonic_clock;
using syzygy::core::telemetry_policy;
using syzygy::core::telemetry_sample;

namespace {
telemetry_sample baseline_sample(monotonic_clock::time_point now) {
  telemetry_sample sample;
  sample.observed_at = now;
  sample.capture_ms = 2.0;
  sample.encode_ms = 3.0;
  sample.send_queue_ms = 2.0;
  sample.network_rtt_ms = 25.0;
  return sample;
}
}  // namespace

TEST(CoreTelemetry, MissingIsNotMeasuredZero) {
  const auto now = monotonic_clock::now();
  auto sample = baseline_sample(now);
  sample.capture_ms = 0.0;
  sample.encode_ms.reset();
  EXPECT_TRUE(syzygy::core::valid_sample(sample));
  EXPECT_EQ(syzygy::core::assess_pipeline(sample, telemetry_policy {}, now).classification, bottleneck::healthy);
}

TEST(CoreTelemetry, RejectsNonFiniteAndOutOfRangeMeasurements) {
  telemetry_sample sample;
  sample.capture_ms = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(syzygy::core::valid_sample(sample));
  sample.capture_ms = 5001.0;
  EXPECT_FALSE(syzygy::core::valid_sample(sample));
  sample.capture_ms = 1.0;
  sample.packet_loss_per_mille = 1001.0;
  EXPECT_FALSE(syzygy::core::valid_sample(sample));
}

TEST(CoreTelemetry, RejectsInvalidPolicy) {
  telemetry_policy policy;
  policy.encode_threshold_ms = 0.0;
  EXPECT_FALSE(syzygy::core::valid_policy(policy));
}

TEST(CoreTelemetry, StaleAndFutureSamplesAreInsufficient) {
  const auto now = monotonic_clock::now();
  auto sample = baseline_sample(now - 4s);
  EXPECT_EQ(syzygy::core::assess_pipeline(sample, telemetry_policy {}, now).classification, bottleneck::insufficient_data);
  sample.observed_at = now + 1s;
  EXPECT_EQ(syzygy::core::assess_pipeline(sample, telemetry_policy {}, now).classification, bottleneck::insufficient_data);
}

TEST(CoreTelemetry, IdentifiesCaptureBoundPipeline) {
  const auto now = monotonic_clock::now();
  auto sample = baseline_sample(now);
  sample.capture_ms = 12.0;
  const auto result = syzygy::core::assess_pipeline(sample, telemetry_policy {}, now);
  EXPECT_EQ(result.classification, bottleneck::capture_bound);
  EXPECT_TRUE(result.observe_only);
  EXPECT_FALSE(result.explanation.empty());
  EXPECT_FALSE(result.evidence.empty());
}

TEST(CoreTelemetry, IdentifiesEncodeBoundPipeline) {
  const auto now = monotonic_clock::now();
  auto sample = baseline_sample(now);
  sample.encode_ms = 14.0;
  EXPECT_EQ(syzygy::core::assess_pipeline(sample, telemetry_policy {}, now).classification, bottleneck::encode_bound);
}

TEST(CoreTelemetry, IdentifiesNetworkBoundPipeline) {
  const auto now = monotonic_clock::now();
  auto sample = baseline_sample(now);
  sample.network_rtt_ms = 120.0;
  EXPECT_EQ(syzygy::core::assess_pipeline(sample, telemetry_policy {}, now).classification, bottleneck::network_bound);
}

TEST(CoreTelemetry, MultipleElevatedStagesAreMixed) {
  const auto now = monotonic_clock::now();
  auto sample = baseline_sample(now);
  sample.capture_ms = 12.0;
  sample.encode_ms = 13.0;
  EXPECT_EQ(syzygy::core::assess_pipeline(sample, telemetry_policy {}, now).classification, bottleneck::mixed);
}

TEST(CoreTelemetry, ClientBoundRequiresExplicitClientTelemetry) {
  const auto now = monotonic_clock::now();
  auto sample = baseline_sample(now);
  sample.client_decode_ms = 15.0;
  EXPECT_EQ(syzygy::core::assess_pipeline(sample, telemetry_policy {}, now).classification, bottleneck::client_bound);
  sample.client_decode_ms.reset();
  EXPECT_EQ(syzygy::core::assess_pipeline(sample, telemetry_policy {}, now).classification, bottleneck::healthy);
}

TEST(CoreTelemetry, InsufficientMeasurementsDoNotReportHealthy) {
  const auto now = monotonic_clock::now();
  telemetry_sample sample;
  sample.observed_at = now;
  sample.capture_ms = 1.0;
  EXPECT_EQ(syzygy::core::assess_pipeline(sample, telemetry_policy {}, now).classification, bottleneck::insufficient_data);
}
