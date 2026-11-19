# Changelog

Notable repository changes are recorded here; entries do not imply unverified runtime behavior.

## Unreleased

### APD planning
- Added a separate control-policy specification with evidence gates and a staged observe-only-to-opt-in rollout.
- Added a GTX 1060 / T1600 hardware acceptance matrix and independent telemetry collection procedure.

### Syzygy Core / Adaptive Pipeline Director
- Added rate-limited per-session structured pipeline diagnostics.
- Added stable string names for bottleneck classifications and confidence levels.

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
