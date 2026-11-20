# Changelog

Notable repository changes are recorded here; entries do not imply unverified runtime behavior.

## Unreleased

### APD planning
- Added a separate control-policy specification with evidence gates and a staged observe-only-to-opt-in rollout.
- Added a GTX 1060 / T1600 hardware acceptance matrix and independent telemetry collection procedure.

### Syzygy Core / Adaptive Pipeline Director
- Added rate-limited per-session structured pipeline diagnostics.
- Added stable string names for bottleneck classifications and confidence levels.
- Added optional client telemetry v2 parsing for RFC 3550 RTP inter-arrival jitter, separate from ENet RTT variance.
- Kept missing capture timestamps unavailable, reset reused capture-duration metadata, and timestamped X11 SHM frames after the capture reply.
- Carried the unadjusted source timestamp through NVENC and bounded FFmpeg PTS correlation separately from the cadence-adjusted Moonlight latency-header timestamp.
- Tightened client feedback frame bounds to the latest frame whose host send path has completed.
- Expanded the T1600 collector with encoder utilization, NIC/qdisc counters, and kernel GPU/display logs; physical acceptance remains pending.

### Development process
- Introduced specification-led development artifacts: requirements, design, task tracking, and verification.

### Linux headless session
- Added explicit requirements and acceptance criteria for private Wayland/KWin startup, isolated touch routing, private audio, and owned-resource cleanup.
- Expanded FUSE cleanup tests for missing utilities, mounts that survive both unmount attempts, and escaped mountinfo paths.
- Existing implementation remains subject to target-host acceptance; see TESTING.md.


## Syzygy Core — Adaptive Pipeline Director

- Added a per-session adapter for the existing captured-frame-to-broadcaster processing latency.
- Added a separate combined host-pipeline pressure classification that does not mislabel the aggregate as capture or encode latency.
- Added a telemetry provenance registry and a standalone Core telemetry smoke test to Linux CI.
- Automatic tuning remains disabled; packet-loss count/time feedback is not treated as a loss rate.
- ENet RTT/variance freshness now advances only on a new acknowledged-packet sample, preventing repeated polling of cached values from masking stale network telemetry.
- Recovery-triggered FEC block loss remains conditional diagnostic evidence and cannot independently classify a session as network-bound.
- Structured diagnostics now include client media jitter, FEC recovery-block loss, legacy loss count/interval, and separate client decode, presentation-queue, and render measurements.
- FEC and client-feedback frame validation now uses wrap-safe 32-bit serial arithmetic.
- FEC recovery diagnostics retain the reported data-shard denominator alongside the lost-shard count; legacy loss reports remain denominator-free.
