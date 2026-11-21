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
- [x] Add per-session capture-to-encoder age and Linux KMS/X11 capture callback duration.
- [x] Preserve the unadjusted source timestamp separately from Moonlight's cadence-adjusted frame-header timestamp so APD host-pipeline latency remains capture-to-broadcast.
- [x] Add conversion duration around async and synchronized `session->convert()` calls.
- [x] Add NVENC and AVCodec/FFmpeg encode timing; FFmpeg outputs are correlated through a bounded PTS registry.
- [x] Parse Moonlight FEC status and legacy loss feedback; preserve legacy count/time without inventing a rate.
- [x] Add per-session ENet control RTT and RTT-variance sampling with a one-second transport ping.
- [x] Refresh ENet RTT/variance freshness only when a new acknowledged-packet sample advances; do not refresh cached estimates by polling.
- [x] Keep recovery-triggered FEC block loss as conditional diagnostic evidence, not an independent stream-wide network classification input.
- [x] Add an independent freshness group and classifier input for client-reported RFC 3550 media jitter; never alias it to ENet RTT variance.
- [x] Add strict host receiver/parser for optional Syzygy client telemetry v1/v2; v2 carries client-reported RFC 3550 media inter-arrival jitter, while a client sender remains outside this repository.
- [x] Document source, units, timing boundary, freshness, and limitations for each wired adapter.
- [x] Add PTS-correlation and feedback-parser smoke tests; session-isolation stress and full hardware validation remain pending.
- [x] Validate recent FEC and client-feedback frame indices with wrap-safe 32-bit serial arithmetic.
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
- [x] Measure capture timestamp-to-encoder-consumption age per session, matched to encoded output by PTS/frame index; it includes mailbox wait, not capture-backend execution.
- [x] Measure Linux KMS/X11 capture callback duration separately; other backends remain unavailable.
- [x] Measure conversion duration separately from encoder submission/output.
- [ ] Measure encoder in-flight count and output age.
- [ ] Measure encoded packet queue depth, oldest packet age and per-session occupancy.
- [x] Add standalone wire-cost estimator including parity, encryption and IP overhead; connect it to DRR item byte costs during runtime integration.
- [x] Measure packetization/header preparation, Reed-Solomon FEC encode duration, sender pacing wait, socket submission-call duration and dequeue-to-first-submission age. These are local host timings, not remote packet arrival.
- [x] Add standalone socket error classification for local backpressure, MTU failure and route/peer failure.
- [ ] Record dropped/replaced frames by reason, frame type and recovery action.
- [ ] Add deterministic tests for session isolation and monotonic frame/packet numbers.

### Phase B — explicit bounded frame admission
- [x] Implement a standalone bounded per-session DRR scheduler primitive with byte/frame limits, queue-age accounting, recovery reserves, FIFO preservation and generation-safe teardown; unit tests added.
- [ ] Wire the scheduler into the encoder publication path and replace the shared overflow-clearing video queue.
- [x] Add move-only aggregate frame/byte reservation primitive spanning pipeline stages.
- [ ] Wire aggregate reservations into encoder, packetizer and sender ownership.
- [ ] Establish a small bounded picture budget based on measured backend depth.
- [ ] Apply freshness dropping only before encode or after protocol-safe reference invalidation.
- [ ] Reserve capacity for IDR and recovery frames.
- [ ] Prove one congested session cannot consume another session's budget.

### Phase C — split packetization from paced sending
- [ ] Design a per-session bounded packetizer-to-sender handoff.
- [x] Specify FEC-block scheduling units so DRR can switch sessions only at complete block boundaries.
- [ ] Hand off FEC blocks as soon as safe to the paced sender.
- [ ] Keep socket pacing off the capture/encoder worker.
- [ ] Preserve frame/block order, FEC indices, packet sequence and encryption IV counters.
- [ ] Add fair scheduling across sessions.
- [ ] Define recovery for partially delivered frames.
- [x] Add a generation-bound session lifetime lease/drain primitive and test teardown waiting for in-flight owners.
- [x] Replace raw session pointer media-packet channel data with generation-bound session handles and carry leases through video and audio broadcaster completion.
- [x] Make AVCodec header replacements packet-owned so queued IDR packets do not reference a destroyed encoder session.
- [x] Close session lease admission on stop and wait for packet owners to drain before session teardown.
- [x] Clear queued packet owners when broadcast shuts down so stopped consumers cannot strand session leases.

### Phase D — early slice/block delivery
- [ ] Verify Linux NVENC and FFmpeg slice-output support and buffer lifetimes.
- [ ] Implement early slice delivery only where the backend supports safe ownership.
- [ ] Otherwise use early FEC-block delivery from complete encoded frames.
- [ ] Keep small frames whole when early handoff overhead is not worthwhile.
- [ ] Benchmark first-packet time, full-frame time, CPU cost and loss recovery.

### Phase E — GPU-resident capture and conversion
- [ ] Trace Linux NVIDIA NVENC and VAAPI surface paths.
- [x] Add explicit GPU completion-gate ownership primitive; backend fence/callback wiring remains outstanding.
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


## Sender telemetry continuation (2026-11-21)

- [x] Instrument packetization/header preparation independently from Reed-Solomon FEC encoding.
- [x] Accumulate per-frame FEC encode duration across complete blocks.
- [x] Accumulate per-frame AES-GCM encrypt-call duration separately from FEC and socket submission.
- [x] Record intentional sender pacing sleep separately from socket submission.
- [x] Record platform send submission-call duration and dequeue-to-first-submission age.
- [ ] Replace the shared broadcaster with bounded per-session packetizer and paced-sender workers; the current instrumentation intentionally does not change send ordering or scheduling.
- [ ] Capture platform send errors/partial batch outcomes with verified platform-specific semantics; current send wrappers do not expose a uniform result.
- [ ] Add encoder in-flight/output-age and per-session packet occupancy measurements.
