# Syzygy Core — Product and Feature Specification

Status: Draft implementation baseline  
Scope: Core platform foundation and Adaptive Pipeline Director (APD) milestones 0–1

## 1. Problem

A remote game stream is a pipeline, not a bitrate slider. Capture, frame scheduling, encoding, packetization, network delivery, client decoding, and presentation can each contribute latency or instability. A single symptom such as low FPS does not identify which stage is responsible.

Syzygy already contains streaming statistics, frame timestamps, per-session WAN state, packet-loss feedback logging, FEC handling, and bitrate/pacing behavior. Those mechanisms are distributed and do not yet provide one normalized, session-scoped diagnostic view.

## 2. Product direction

Syzygy Core is an orchestration layer above the streaming engine. It will eventually own session lifecycle and identity, per-session telemetry and device capability profiles, explainable policy decisions, recovery coordination, and display/audio/input ownership during session handoff.

The streaming engine remains the authority for capture, encoding, packet transport, protocol compatibility, and input delivery. Core must use explicit interfaces rather than duplicate those systems.

## 3. Goals for milestone 0

- Define a normalized telemetry model for one streaming session.
- Represent unknown or unavailable measurements explicitly; never substitute zero.
- Validate numeric ranges and reject malformed samples.
- Reject stale samples for live assessment.
- Classify the strongest evidenced bottleneck using deterministic rules.
- Produce an explanation and confidence level with every assessment.
- Default to observe-only behavior.
- Keep all state session-scoped; one client's telemetry must never affect another client's stream.
- Add deterministic unit tests without requiring a GPU, display server, network, or active stream.

## 4. Non-goals for milestone 0

- Changing bitrate, FEC, resolution, frame rate, encoder, capture backend, or packet pacing.
- Restarting or recovering processes.
- Reading client decode/presentation metrics that the current protocol does not provide.
- Claiming host-side send latency is equivalent to client display latency.
- Replacing or bypassing existing WAN pacing and wan_state behavior.
- Persisting telemetry or collecting personally identifying usage data.
- Introducing a new wire protocol or client extension.

## 5. Telemetry contract

A sample belongs to exactly one session and one monotonic observation time. Measurements are optional and use explicit units:

| Field | Unit | Valid range | Source status |
| --- | --- | --- | --- |
| Capture latency | milliseconds | 0–5000 | Adapter not yet wired |
| Encode latency | milliseconds | 0–5000 | Adapter not yet wired |
| Send queue latency | milliseconds | 0–5000 | Adapter not yet wired |
| Combined host pipeline latency | milliseconds | 0–5000 | Wired from captured-frame timestamp to broadcaster processing point; aggregate only, not stage-isolated |
| Network RTT | milliseconds | 0–5000 | No verified runtime adapter |
| Network jitter | milliseconds | 0–5000 | No verified runtime adapter |
| Packet loss | per mille | 0–1000 | Existing client loss packet is count/time, not a verified rate |
| Client decode latency | milliseconds | 0–5000 | Not currently available through inspected Moonlight feedback path |
| Client presentation latency | milliseconds | 0–5000 | Not currently available through inspected Moonlight feedback path |

A missing measurement means unavailable, not zero. All time values use steady_clock for age calculations. Samples older than the configured freshness window are stale and cannot drive an assessment.

## 6. Classification requirements

- capture_bound: capture latency is elevated while encode and send queue are not elevated.
- encode_bound: encode latency is elevated relative to its configured threshold.
- network_bound: send queue latency is elevated, or complete network measurements show congestion.
- client_bound: reserved for future client-side decode/presentation telemetry; must not be emitted until such telemetry is supplied and validated.
- healthy: enough fresh measurements exist and none cross a threshold.
- insufficient_data: measurements are missing, stale, or contradictory.
- mixed: two or more stages are elevated without enough evidence to distinguish them.

A classification must include the measurements that triggered it, an explanation, and a confidence level.

## 7. Safety requirements

- Observe-only is the only supported mode in milestone 0.
- No assessment may mutate streaming configuration or session state.
- No global/static controller state.
- No stale or malformed sample may trigger a recommendation.
- Thresholds are explicit policy inputs with validated bounds.
- Runtime integration must be opt-in and fail closed when telemetry is unavailable.
- Diagnostics must not include pairing phrases, keys, certificates, or credentials.

## 8. Acceptance criteria

| ID | Requirement | Evidence |
| --- | --- | --- |
| CORE-AC-01 | Missing values remain distinguishable from measured zero | Unit tests |
| CORE-AC-02 | Out-of-range and non-finite measurements are rejected | Unit tests |
| CORE-AC-03 | Stale samples produce insufficient-data assessment | Unit tests |
| CORE-AC-04 | Capture, encode, and network bottlenecks classify deterministically | Unit tests |
| CORE-AC-05 | Multiple elevated stages classify as mixed | Unit tests |
| CORE-AC-06 | Client-bound classification is impossible without client telemetry | Unit tests |
| CORE-AC-07 | Assessment contains explanation and confidence | Unit tests |
| CORE-AC-08 | Observe-only assessment cannot mutate stream settings | API/design review and tests |
| CORE-AC-09 | Telemetry state is session-scoped | API/design review and tests |
| CORE-AC-10 | New source files are included in normal and test builds | Linux build and CI |
| CORE-AC-11 | Runtime instrumentation source and units are documented | Adapter tests and code review |
| CORE-AC-12 | Target T1600 telemetry matches independent measurement | Hardware acceptance |
| CORE-AC-13 | Combined host-pipeline timing is recorded per session without claiming a specific substage | Adapter review and smoke test |

## 9. Rollout

1. Land the pure model and tests.
2. Add host-side adapters for measurements with verified semantics.
3. Add a read-only diagnostic surface.
4. Run observe-only on real sessions and compare against independent measurements.
5. Specify and review automatic actions separately. No automatic control is enabled by this document.

## 10. Verified runtime adapter (milestone 1)

The video broadcaster already computes `frame_processing_latency` from `packet->frame_timestamp` to the point immediately before packet preparation and transmission. It is quantized in 0.1 ms units for the Moonlight frame header. Core stores this as `host_pipeline_ms` on the owning `session_t`, converting the transmitted value back to milliseconds.

This is a combined host-side measurement. It includes work between the original frame timestamp and the broadcaster observation point; it does not separate capture, conversion, encoder execution, packet queue wait, or client display time. It must never be relabeled as encode latency, capture latency, or end-to-end latency. It is session-owned and observe-only.

The existing loss feedback packet remains count plus elapsed milliseconds and is not converted to a loss percentage. RTT, jitter, and client decode/presentation remain unavailable until a verified source exists.

## 11. Open questions

- Which client protocol extension, if any, can safely report decode and presentation timing?
- What host APIs provide reliable capture and encoder stage timings across NVENC, VAAPI, and software encoding?
- Can the existing WAN feedback controller be reused behind a Core policy interface, or should it remain an engine-owned actuator?
- What thresholds should be calibrated per encoder/backend rather than globally?
