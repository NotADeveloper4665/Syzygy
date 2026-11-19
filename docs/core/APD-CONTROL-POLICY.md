# Adaptive Pipeline Director — Control Policy Specification

Status: design baseline; all runtime behavior remains observe-only.

## 1. Objective

Use fresh, per-session evidence to recommend reversible streaming adjustments without fighting the existing streaming engine or its WAN controller. The APD must explain why it recommends an action and what evidence would invalidate that recommendation.

## 2. Current release boundary

The current implementation may:
- collect verified host and packet timing measurements;
- classify pipeline pressure;
- emit rate-limited per-session diagnostics.

The current implementation must not:
- change bitrate, FEC, resolution, frame rate, encoder, capture backend, or packet pacing;
- restart or recover a stream automatically;
- use telemetry from one session to affect another session;
- infer client decode, display latency, packet-loss percentage, RTT, or jitter from unsupported sources.

## 3. Candidate actuator boundary

The streaming engine remains the owner of all actuators. Core may eventually request a change through a narrow, typed per-session interface. It must not write encoder contexts, WAN atomics, capture objects, or transport internals directly.

Initial candidate:
- **Bitrate recommendation only**, routed through the existing per-session WAN bitrate mechanism where supported.
- Existing WAN pacing/FEC behavior remains authoritative; APD must not implement a competing controller.
- No automatic action is enabled by this specification.

Deferred:
- FEC changes: require validated packet-loss denominator/window and recovery evidence.
- Resolution/frame-rate changes: require a supported renegotiation contract and client capability evidence.
- Encoder/capture backend changes: require explicit lifecycle-safe reinitialization and rollback support.
- Process/session recovery: separate fault-recovery specification.

## 4. Evidence gates

A recommendation is eligible only when:
1. The sample is valid, monotonic, fresh, and belongs to the target session.
2. The relevant stage has a verified source and unit.
3. The classifier has enough evidence to distinguish that stage from overlapping aggregate measurements.
4. The confidence level is at least medium; low-confidence aggregate-only pressure may explain but never recommend a control action.
5. The session is not under a manual override or another active control transition.

Missing, stale, invalid, contradictory, or unsupported measurements produce no recommendation.

## 5. Control stability requirements

Before any actuator is implemented, its policy must specify:
- a calibrated target range per encoder/backend and workload;
- separate enter and exit thresholds (hysteresis);
- a minimum dwell time between actions;
- a bounded adjustment step and a maximum total deviation;
- a cooldown after failure;
- a rollback condition and a known-good prior value;
- manual override precedence and an explicit disable switch;
- multi-client isolation and conflict handling;
- audit output recording evidence, decision, old value, requested value, and result without secrets.

Initial design targets for review, not runtime defaults:
- require at least 10 seconds of consistent qualifying evidence;
- minimum 30 seconds between automatic adjustments;
- one bounded bitrate step per decision;
- revert or stop adjusting when telemetry becomes stale or the observed result worsens.

These values must be calibrated and approved before implementation.

## 6. Rollout gates

1. Observe-only diagnostics and source validation.
2. Recommendation-only mode; no engine calls.
3. Shadow mode comparing proposed actions with observed outcomes.
4. Explicit opt-in for one reversible actuator after hardware acceptance.
5. Broader automation only after per-backend and multi-client validation.

Each stage requires tests, updated specifications, and recorded hardware evidence. No stage may silently enable the next one.

## 7. Acceptance criteria

- APD never mutates stream configuration in observe-only or recommendation-only mode.
- Low-confidence or incomplete data never generates an actionable recommendation.
- Stale data, manual override, or a concurrent transition blocks action.
- Every future action is per-session, bounded, reversible, and logged.
- Existing WAN pacing/FEC logic remains the sole owner of transport control.
- T1600 results are compared against independent GPU, encoder, and client observations before enabling any action.
