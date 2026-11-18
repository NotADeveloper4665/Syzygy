# Syzygy Core — Testing

## Unit tests

The Core model must be testable without a running streaming host.

Required cases:
- A measured zero differs from an unavailable measurement.
- Negative, out-of-range, NaN, and infinite values are rejected.
- Freshness boundary is deterministic using an injected monotonic timestamp.
- Stale and future-dated samples do not classify as healthy.
- Each supported host/network stage can be classified from sufficient evidence.
- Two elevated stages classify as mixed.
- Client-bound is never inferred from server-side timing.
- Assessments are deterministic and do not mutate samples or stream configuration.

## Build validation

- New source files must compile in the normal server target and test target.
- Run focused Core unit tests.
- Run the complete CTest suite.
- Build the Linux server and RPM package in CI.

## Hardware acceptance

On the Fedora 44 T1600 / GTX 1060 host:
- Capture/encode/send timings must be compared with independent profiling.
- Repeat tests at 1080p60 and at least one lower/higher load.
- Record encoder, codec, GPU load, CPU load, capture backend, and network conditions.
- Confirm stale or missing telemetry never triggers a change.
- No automatic tuning is permitted in this milestone.
