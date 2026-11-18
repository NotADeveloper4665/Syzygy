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

- Telemetry value model and validation: implementation started.
- Pipeline classification: implementation started.
- Runtime telemetry adapters: not started.
- UI/diagnostic presentation: not started.
- Automatic tuning: out of scope for milestone 0.
