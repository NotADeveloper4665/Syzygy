# Syzygy Core

Syzygy Core is the host-side orchestration layer for remote gaming sessions. It coordinates session state, telemetry, policy decisions, device capabilities, recovery, and future session handoff while leaving the existing Sunshine-derived engine responsible for capture, encoding, transport, and protocol compatibility.

## First capability: Adaptive Pipeline Director

The Adaptive Pipeline Director (APD) observes the streaming pipeline and identifies the most likely bottleneck. Its first release is diagnostic and observe-only. It must not change encoder settings, display mode, frame rate, bitrate, FEC, process state, or client behavior.

The initial implementation is a pure, deterministic C++ model. Runtime instrumentation and control integration follow only after telemetry sources and their semantics are verified.

## Documents

- [Specification](SPEC.md)
- [Design](DESIGN.md)
- [Tasks and milestones](TASKS.md)
- [Testing and hardware validation](TESTING.md)

## Current status

- Telemetry value model, validation, and deterministic classification: implemented.
- Host timing adapters: capture-to-encoder age, Linux KMS/X11 callback duration, conversion, NVENC frame-index timing, FFmpeg PTS-correlated timing, and internal packet queue wait are wired per session.
- Network/loss adapters: ENet control RTT/variance, client-reported v2 media jitter, Moonlight recovery-triggered FEC block loss, and legacy count/interval feedback are parsed with independent freshness and explicit source limitations.
- Client feedback: strict host receiver for Syzygy telemetry v1/v2 is implemented; no compatible client sender is included in this repository, so decode/presentation and media jitter remain unavailable on ordinary clients.
- Diagnostics: structured per-session observe-only output is rate-limited to one report per five seconds.
- Local UI/diagnostics endpoint and recommendation-only output: not implemented.
- Physical T1600 / GTX 1060 acceptance: pending; CI software rendering does not satisfy hardware validation.
- Automatic bitrate/FEC/resolution/frame-rate/encoder/capture/pacing changes: disabled and out of scope for this milestone.


## Telemetry provenance

- [Telemetry source registry](TELEMETRY-SOURCES.md) documents connected and unavailable metrics, their units, timing boundaries, and interpretation limits.
