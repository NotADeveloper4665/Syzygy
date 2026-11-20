# Syzygy Core — Product and Feature Specification

Status: Draft implementation baseline; milestone 0 complete, milestone 1 instrumentation in progress  
Scope: Core platform foundation and Adaptive Pipeline Director (APD) milestones 0–1

## 1. Problem

A remote game stream is a pipeline, not a bitrate slider. Capture, frame scheduling, conversion, encoding, packetization, network delivery, client decoding, and presentation can each contribute latency or instability. A single symptom such as low FPS does not identify which stage is responsible.

Syzygy already contains streaming statistics, frame timestamps, per-session WAN state, packet-loss feedback, FEC handling, and bitrate/pacing behavior. Those mechanisms are distributed and do not yet provide one normalized, session-scoped diagnostic view.

## 2. Product direction

Syzygy Core is an orchestration layer above the streaming engine. It will eventually own session lifecycle and identity, per-session telemetry and device capability profiles, explainable policy decisions, recovery coordination, and display/audio/input ownership during session handoff.

The streaming engine remains the authority for capture, encoding, packet transport, protocol compatibility, and input delivery. Core must use explicit interfaces rather than duplicate those systems.

## 3. Goals for milestone 0

- Define a normalized telemetry model for one streaming session.
- Represent unknown or unavailable measurements explicitly; never substitute zero.
- Validate numeric ranges and reject malformed samples.
- Reject stale measurements independently by source group.
- Classify the strongest evidenced bottleneck using deterministic rules.
- Produce an explanation and confidence level with every assessment.
- Default to observe-only behavior.
- Keep all state session-scoped; one client's telemetry must never affect another client's stream.
- Add deterministic unit tests without requiring a GPU, display server, network, or active stream.

## 4. Non-goals for milestone 0

- Changing bitrate, FEC, resolution, frame rate, encoder, capture backend, or packet pacing.
- Restarting or recovering processes.
- Treating standard Moonlight feedback as a source of client decode/presentation latency when it does not provide those values.
- Claiming host-side send latency is equivalent to client display latency.
- Replacing or bypassing existing WAN pacing and `wan_state` behavior.
- Persisting telemetry or collecting personally identifying usage data.
- Enabling a new client extension or requiring a client update. A strict host-side parser contract may be present for a future optional Syzygy extension, but no client behavior is activated by this repository change.

## 5. Telemetry contract

Measurements are optional and use explicit units. Host, network, loss, and client groups carry independent monotonic observation timestamps so a fresh video frame cannot make old feedback appear current.

| Field | Unit | Valid range | Source status |
| --- | --- | --- | --- |
| Capture-to-encoder age | milliseconds | 0–5000 | Wired from image frame timestamp to encoder consumption; includes latest-slot/mailbox wait |
| Capture backend duration | milliseconds | 0–5000 | Wired for Linux KMS and X11 snapshot paths; other backends remain unavailable |
| Conversion duration | milliseconds | 0–5000 | Wired around `session->convert()` in async and synchronized paths |
| Encode latency | milliseconds | 0–5000 | NVENC API-to-matching-output and FFmpeg PTS-correlated submission-to-output wired |
| Send queue latency | milliseconds | 0–5000 | Internal packet queue wait from publication to broadcaster dequeue; excludes network send |
| Combined host pipeline latency | milliseconds | 0–5000 | Captured-frame timestamp to broadcaster observation; aggregate only |
| Control-channel RTT | milliseconds | 0–5000 | ENet peer smoothed RTT sampled with a low-rate transport ping |
| Control-channel RTT variance | milliseconds | 0–5000 | ENet RTT-variance estimator; path-variation proxy, not media packet jitter |
| Client media inter-arrival jitter | milliseconds | 0–5000 | Optional Syzygy `0x5506` v2; RFC 3550 RTP inter-arrival jitter converted using the negotiated RTP clock rate; no sender in this repository |
| FEC-reported data loss | per mille | 0–1000 | Per-block denominator from Moonlight `SS_FRAME_FEC_STATUS`; reports only when a block required recovery |
| Legacy loss count / interval | count / milliseconds | count >= 0; interval 1–60000 | Raw legacy feedback retained; no rate inferred |
| Client decode latency | milliseconds | 0–5000 | Optional Syzygy `0x5506` v1/v2 receiver; no client sender in this repository |
| Client presentation queue / render / total | milliseconds | 0–5000 | Optional Syzygy `0x5506` receiver; not physical display latency |

A missing measurement means unavailable, not zero. All durations use monotonic clocks at the source. Host, network, loss, and client timestamps are validated and aged independently.

## 6. Classification requirements

- `capture_bound`: capture backend duration or capture-to-encoder age is elevated, with enough fresh context to isolate the capture path.
- `conversion_bound`: image conversion duration is elevated with sufficient stage coverage.
- `encode_bound`: NVENC or FFmpeg submission-to-output latency is elevated with sufficient stage coverage.
- `sender_queue_bound`: internal encoded-packet queue wait is elevated; this is host sender pressure, not network transit.
- `network_bound`: fresh control-path RTT/variance or client FEC-reported block loss is elevated. RTT variance is not one-way media jitter.
- `client_bound`: reserved for validated client decode/presentation feedback from the optional Syzygy extension; no ordinary client currently supplies it.
- `host_pipeline_pressure`: aggregate host timing is elevated but detailed stages are unavailable; low confidence only.
- `healthy`: fresh capture, encode, sender-queue, and network measurements exist and none cross a threshold.
- `insufficient_data`: measurements are missing, stale, invalid, or do not provide enough independent context.
- `mixed`: two or more independently measured stages are elevated without enough evidence to isolate one.

A classification must include the measurements that triggered it, an explanation, and a confidence level. Aggregate host timing overlaps detailed host stages and is never counted as a second independent bottleneck.

## 7. Safety requirements

- Observe-only is the only supported runtime mode in these milestones.
- No assessment may mutate streaming configuration or session state.
- No global/static controller state.
- No stale or malformed sample may trigger a recommendation.
- Thresholds are explicit policy inputs with validated bounds.
- Runtime integration must fail closed when telemetry is unavailable.
- Diagnostics must not include pairing phrases, keys, certificates, or credentials.
- Existing WAN pacing/FEC remains the only transport controller.

## 8. Acceptance criteria

| ID | Requirement | Evidence |
| --- | --- | --- |
| CORE-AC-01 | Missing values remain distinguishable from measured zero | Unit tests |
| CORE-AC-02 | Out-of-range and non-finite measurements are rejected | Unit tests |
| CORE-AC-03 | Stale source groups produce insufficient-data assessment | Unit tests |
| CORE-AC-04 | Capture, conversion, encode, sender-queue, and network bottlenecks classify deterministically | Unit tests |
| CORE-AC-05 | Multiple elevated stages classify as mixed | Unit tests |
| CORE-AC-06 | Client-bound classification requires explicit validated client telemetry | Unit tests |
| CORE-AC-07 | Assessment contains explanation and confidence | Unit tests |
| CORE-AC-08 | Observe-only assessment cannot mutate stream settings | API/design review and tests |
| CORE-AC-09 | Telemetry state is session-scoped | API/design review and tests |
| CORE-AC-10 | New source files are included in normal and test builds | Linux build and CI |
| CORE-AC-11 | Runtime instrumentation source, units, and timing boundaries are documented | Adapter tests and code review |
| CORE-AC-12 | Target T1600 telemetry matches independent measurement | Hardware acceptance |
| CORE-AC-13 | Combined host-pipeline timing is recorded without claiming a specific substage | Adapter review and smoke test |
| CORE-AC-14 | NVENC timing is attached only to a matching frame index | Packet adapter review and tests |
| CORE-AC-15 | Capture-to-encoder age is attached only to a frame-matched output and remains unavailable without a valid source timestamp | Packet adapter review and tests |
| CORE-AC-16 | Internal packet queue wait is recorded separately from network transit | Packet adapter review and tests |
| CORE-AC-17 | Linux KMS/X11 capture callback and conversion durations have verified boundaries | Adapter review and smoke test |
| CORE-AC-18 | FFmpeg output timing is correlated by PTS with bounded pending state and no current-frame fallback | Correlator tests and code review |
| CORE-AC-19 | RTT/variance provenance is explicit and not mislabeled as media jitter | Source registry and code review |
| CORE-AC-20 | FEC loss rate uses a verified per-block denominator; legacy count/time remains raw | Parser tests |
| CORE-AC-21 | Client feedback parser rejects malformed, stale, and out-of-window reports | Parser tests and code review |

## 9. Rollout

1. Land the pure model and tests.
2. Add host-side adapters for measurements with verified semantics.
3. Add a read-only diagnostic surface.
4. Run observe-only on real sessions and compare against independent measurements.
5. Specify and review automatic actions separately. No automatic control is enabled by this document.

## 10. Verified runtime adapters

The video broadcaster computes the existing Moonlight `frame_processing_latency` from `packet->frame_timestamp` to the point immediately before packet header construction. It is quantized in 0.1 ms units for the Moonlight frame header. Core stores this as `host_pipeline_ms` on the owning `session_t`.

The capture-to-encoder age is attached to the encoded frame only when NVENC frame index or FFmpeg PTS correlation succeeds. Linux KMS and X11 snapshot callbacks separately measure their own callback duration. Conversion is timed around the actual conversion call.

NVENC timing is API-entry to matching output availability. FFmpeg timing is successful `avcodec_send_frame()` submission to the matching PTS output from `avcodec_receive_packet()`. Both include backend buffering/driver wait and are not pure GPU execution time. FFmpeg pending correlation is bounded to 256 frames; missing or evicted PTS metadata remains unavailable.

The shared packet queue wait is measured from publication to broadcaster dequeue. It is not network transit. Network RTT and RTT variance come from the owning ENet control peer; RTT variance is a control-path variation proxy, not media-packet jitter. Client FEC status `0x5502` supplies a per-block loss denominator for blocks requiring recovery. Legacy loss count and interval are retained without a derived rate.

The optional Syzygy client telemetry receiver `0x5506` accepts documented v1/v2 payloads and a recent host frame index. V2 adds client-reported RFC 3550 media inter-arrival jitter. It is host-side only in this repository; no client sender is included, so ordinary sessions still report client decode/presentation and media jitter as unavailable.

## 11. Open questions

- Which approved client implementation will send the optional decode/presentation feedback extension, and can its decoder/render APIs provide stable duration boundaries?
- Which compatible client sender will provide the v2 RFC 3550 media inter-arrival jitter estimate separately from ENet control RTT variance?
- How should packetizer/FEC duration and socket submission/error telemetry be separated from sender queue wait?
- Which host APIs provide comparable capture and encoder stage timings across KMS, X11, NVENC, VAAPI, and software encoding?
- Can the existing WAN feedback controller be reused behind a Core policy interface, or should it remain an engine-owned actuator?
- What thresholds should be calibrated per encoder/backend rather than globally?
