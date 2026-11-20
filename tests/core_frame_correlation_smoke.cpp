#include <cassert>
#include <chrono>

#include "src/syzygy/core/frame_correlation.h"

int main() {
  using clock = std::chrono::steady_clock;
  using syzygy::core::frame_timing_record;
  using syzygy::core::pts_timing_correlator;

  const auto now = clock::now();
  pts_timing_correlator correlator(4);

  frame_timing_record first;
  first.pts = 100;
  first.submitted_at = now;
  first.capture_ms = 3.0;
  first.capture_backend_ms = 1.0;
  assert(correlator.remember(first));

  frame_timing_record second;
  second.pts = 101;
  second.submitted_at = now + std::chrono::milliseconds(1);
  second.capture_ms = 4.0;
  second.conversion_ms = 2.0;
  assert(correlator.remember(second));

  frame_timing_record third;
  third.pts = 102;
  third.submitted_at = now + std::chrono::milliseconds(2);
  third.capture_ms = 5.0;
  assert(correlator.remember(third));

  // Encoders may emit a delayed/reordered packet. PTS, not the most recently
  // submitted frame, selects the metadata.
  auto reordered = correlator.take(101);
  assert(reordered && reordered->pts == 101);
  assert(reordered->capture_ms && *reordered->capture_ms == 4.0);
  assert(reordered->conversion_ms && *reordered->conversion_ms == 2.0);

  auto delayed = correlator.take(100);
  assert(delayed && delayed->capture_backend_ms && *delayed->capture_backend_ms == 1.0);
  assert(!correlator.take(999));
  assert(correlator.size() == 1);

  // Duplicate PTS must never overwrite the first submitted record.
  assert(!correlator.remember(third));
  // A duplicate PTS invalidates the association instead of returning stale metadata.
  assert(!correlator.take(102));
  assert(correlator.size() == 0);

  pts_timing_correlator bounded(2);
  assert(bounded.remember(first));
  assert(bounded.remember(second));
  assert(bounded.remember(third));
  assert(bounded.evictions() == 1);
  assert(!bounded.take(100));
  assert(bounded.take(101));
  assert(bounded.take(102));
  assert(bounded.size() == 0);

  pts_timing_correlator disabled(0);
  assert(!disabled.remember(first));
  return 0;
}
