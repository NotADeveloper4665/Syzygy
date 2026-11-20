# Syzygy Core — Tasks

## Milestone 0: observable pipeline foundation

- [x] Establish Core product boundary and feature specification.
- [x] Record current telemetry-source limitations and safety rules.
- [x] Implement platform-neutral telemetry value model.
- [x] Validate units, ranges, finite values, and monotonic timestamps.
- [x] Implement deterministic bottleneck classifier.
- [x] Add tests for missing, stale, invalid, mixed, and healthy samples.
- [x] Add Core source files to the shared server/test CMake source list.
- [x] Add a standalone Core telemetry smoke test to the Linux CI workflow.
- [ ] Run focused GoogleTest tests and full Linux build.
- [ ] Confirm CI for the runtime adapter commits.
- [ ] Confirm CI on the feature branch.
- [ ] Review docs against implementation.

## Milestone 1: verified host adapters

- [x] Map existing frame timestamps and broadcaster processing-latency instrumentation to a per-session combined host-pipeline measurement.
- [x] Add per-session NVENC API-to-matching-output timing with explicit driver/API-wait semantics.
- [x] Add per-session internal packet queue-wait timing from publication to broadcaster dequeue.
- [ ] Add per-session capture timing only after platform callback boundaries are verified.
- [ ] Add AVCodec/FFmpeg encode timing after delayed/reordered outputs are frame-correlated safely.
- [ ] Parse existing client feedback without treating loss counts as a loss rate.
- [x] Document source, units, timing boundary, and freshness for the combined host-pipeline adapter.
- [ ] Document sampling interval and source for each future adapter.
- [ ] Add adapter-level tests, including session-isolation and missing-timestamp behavior.
- [x] Prevent aggregate host-pipeline timing from being counted as a second independent bottleneck alongside detailed stage metrics.

## Milestone 2: observe-only diagnostics

- [x] Add structured per-session diagnostic output with a five-second per-session rate limit.
- [ ] Expose classification and evidence in the host UI or a local diagnostics endpoint.
- [ ] Add rate limiting and privacy review.
- [ ] Compare reports against independent host/client measurements on T1600.

## Milestone 3: reviewed control policy

- [x] Specify evidence gates, actuator ownership, and rollout stages in APD-CONTROL-POLICY.md.
- [ ] Implement recommendation-only output after telemetry coverage is sufficient.
- [ ] Design per-session actuator interfaces; do not call engine controls yet.
- [ ] Calibrate thresholds, hysteresis, dwell time, rollback, and manual override on T1600.
- [ ] Specify control objectives and constraints separately.
- [ ] Define per-session actuator interfaces.
- [ ] Define hysteresis, minimum dwell time, rollback, and manual override.
- [ ] Reuse existing engine controls where safe; do not duplicate WAN bitrate/FEC logic.
- [ ] Keep automatic actions disabled by default until hardware validation.

## Milestone 4: low-latency pipeline adaptation (Rubylight study)

Specification: [LOW-LATENCY-PIPELINE-SPEC.md](LOW-LATENCY-PIPELINE-SPEC.md)

### Phase A — instrument before changing scheduling
- [ ] Measure capture publication-to-encoder-consumption age per session.
- [ ] Measure conversion duration separately from encoder submission/output.
- [ ] Measure encoder in-flight count and output age.
- [ ] Measure encoded packet queue depth, oldest packet age and per-session occupancy.
- [ ] Measure packetization/FEC, sender wait, socket submission and first-packet time.
- [ ] Record dropped/replaced frames by reason, frame type and recovery action.
- [ ] Add deterministic tests for session isolation and monotonic frame/packet numbers.

### Phase B — explicit bounded frame admission
- [ ] Replace implicit global queue-overflow clearing with explicit admission outcomes.
- [ ] Track encoder, packetizer and sender occupancy per session.
- [ ] Establish a small bounded picture budget based on measured backend depth.
- [ ] Apply freshness dropping only before encode or after protocol-safe reference invalidation.
- [ ] Reserve capacity for IDR and recovery frames.
- [ ] Prove one congested session cannot consume another session's budget.

### Phase C — split packetization from paced sending
- [ ] Design a per-session bounded packetizer-to-sender handoff.
- [ ] Hand off FEC blocks as soon as safe to the paced sender.
- [ ] Keep socket pacing off the capture/encoder worker.
- [ ] Preserve frame/block order, FEC indices, packet sequence and encryption IV counters.
- [ ] Add fair scheduling across sessions.
- [ ] Define recovery for partially delivered frames.

### Phase D — early slice/block delivery
- [ ] Verify Linux NVENC and FFmpeg slice-output support and buffer lifetimes.
- [ ] Implement early slice delivery only where the backend supports safe ownership.
- [ ] Otherwise use early FEC-block delivery from complete encoded frames.
- [ ] Keep small frames whole when early handoff overhead is not worthwhile.
- [ ] Benchmark first-packet time, full-frame time, CPU cost and loss recovery.

### Phase E — GPU-resident capture and conversion
- [ ] Trace Linux NVIDIA NVENC and VAAPI surface paths.
- [ ] Verify DMA-BUF/CUDA/VAAPI interop and synchronization.
- [ ] Remove only proven redundant readbacks/copies.
- [ ] Retain tested fallbacks for unsupported formats and devices.
- [ ] Validate HDR, 10-bit, chroma formats and device-loss teardown.

### Phase F — pacing and APD integration
- [ ] Feed verified queue age, socket outcomes, RTT/jitter and loss rate into Core.
- [ ] Reuse existing WAN pacing and FEC control; do not add a second controller.
- [ ] Add recommendation-only APD output after telemetry coverage is sufficient.
- [ ] Define hysteresis, dwell, override and rollback before any actuator.
- [ ] Keep automatic actions disabled by default.

## Future Core capabilities

- [ ] Session Doctor: explain likely faults and suggest reversible remediation.
- [ ] Device Intelligence: client capability profiles with explicit provenance.
- [ ] Session Recovery: recover failed stream components without killing the game where possible.
- [ ] Session Handoff: transfer display, audio, and input ownership between devices.
