# Syzygy Core — Testing

## Unit tests

The Core model must be testable without a running streaming host.

Required cases:
- A measured zero differs from an unavailable measurement.
- Negative, out-of-range, NaN, and infinite values are rejected.
- Freshness boundaries are deterministic using injected monotonic timestamps.
- Stale host, network, loss, and client groups do not become fresh because another group updated.
- Each supported host/network/client stage can be classified from sufficient evidence.
- Two elevated stages classify as mixed.
- Client-bound is never inferred from server-side timing.
- Aggregate host timing is not counted as a second independent stage.
- Assessments are deterministic and do not mutate samples or stream configuration.

## Adapter and parser tests

- `core_frame_correlation_smoke.cpp`: delayed/reordered PTS output, missing PTS, duplicate PTS, bounded pending records, and eviction.
- `core_client_feedback_smoke.cpp`: exact FEC payload size/endian/count/denominator validation, legacy count/time parsing without a rate, strict Syzygy client telemetry v1/v2 validation, and wrap-safe frame-index windows.
- `core_telemetry_smoke.cpp`: independent source freshness, ENet sample-token freshness, capture/conversion/encode/sender/network/client classification, conservative FEC recovery-block evidence, mixed stages, and observe-only behavior.

The Linux workflow compiles and runs all three smoke tests with `-Wall -Wextra -Werror`. The full GoogleTest target remains a separate validation item; the RPM packaging job currently configures `BUILD_TESTS=OFF`.

## Runtime diagnostics

- Structured debug output is emitted per session no more often than every five seconds.
- Output includes classification, confidence, capture-to-encoder age, capture callback duration, conversion, encoder submission-to-output, sender queue wait, control RTT/variance, client media inter-arrival jitter, FEC recovery-block loss count/denominator when reported, legacy count/interval, and separate client decode/presentation-queue/render values when supplied.
- Missing values are rendered as unavailable; diagnostics never include pairing secrets or client identifiers.
- Control RTT/variance, loss feedback, and client feedback are timestamped independently from host frame measurements.

## Build validation

- New source files must compile in the normal server target and test target.
- Run focused Core unit tests.
- Run the complete CTest suite.
- Build the Linux server and RPM package in CI.
- Verify that AVCodec output timing is attached only by matching PTS and never by the most recently submitted frame.

## Hardware acceptance

On the physical Fedora 44 T1600 / GTX 1060 host:
- Compare capture, conversion, encode, and queue timings with independent profiling.
- Verify the selected capture backend before interpreting capture callback duration.
- Verify NVENC and FFmpeg paths separately where supported.
- Record control-channel RTT/variance separately from media-path jitter.
- Confirm FEC loss reports only appear for blocks requiring recovery; absence is unknown.
- Validate client decode/presentation only when a compatible client sender is present.
- Repeat tests at 1080p60 and at least one lower/higher load.
- Confirm stale or missing telemetry never triggers a change.
- No automatic tuning is permitted in this milestone.
