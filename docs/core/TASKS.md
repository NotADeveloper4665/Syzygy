# Syzygy Core — Tasks

## Milestone 0: observable pipeline foundation

- [x] Establish Core product boundary and feature specification.
- [x] Record current telemetry-source limitations and safety rules.
- [x] Implement platform-neutral telemetry value model.
- [x] Validate units, ranges, finite values, and monotonic timestamps.
- [x] Implement deterministic bottleneck classifier.
- [x] Add tests for missing, stale, invalid, mixed, and healthy samples.
- [x] Add Core source files to the shared server/test CMake source list.
- [ ] Run focused tests and full Linux build.
- [ ] Confirm CI on the feature branch.
- [ ] Review docs against implementation.

## Milestone 1: verified host adapters

- [ ] Map existing frame timestamps and processing-latency instrumentation to normalized telemetry.
- [ ] Add per-session capture/encode/send queue measurements only where timing boundaries are verified.
- [ ] Parse existing client feedback without treating loss counts as a loss rate.
- [ ] Document source, units, sampling interval, and freshness for each adapter.
- [ ] Add adapter-level tests.

## Milestone 2: observe-only diagnostics

- [ ] Add structured per-session diagnostic output.
- [ ] Expose classification and evidence in the host UI or a local diagnostics endpoint.
- [ ] Add rate limiting and privacy review.
- [ ] Compare reports against independent host/client measurements on T1600.

## Milestone 3: reviewed control policy

- [ ] Specify control objectives and constraints separately.
- [ ] Define per-session actuator interfaces.
- [ ] Define hysteresis, minimum dwell time, rollback, and manual override.
- [ ] Reuse existing engine controls where safe; do not duplicate WAN bitrate/FEC logic.
- [ ] Keep automatic actions disabled by default until hardware validation.

## Future Core capabilities

- [ ] Session Doctor: explain likely faults and suggest reversible remediation.
- [ ] Device Intelligence: client capability profiles with explicit provenance.
- [ ] Session Recovery: recover failed stream components without killing the game where possible.
- [ ] Session Handoff: transfer display, audio, and input ownership between devices.
