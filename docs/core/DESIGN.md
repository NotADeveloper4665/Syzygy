# Syzygy Core — Design

Status: Draft  
Applies to: Core milestone 0 / Adaptive Pipeline Director

## 1. Boundaries

syzygy::core is a policy and diagnostic namespace. It must not own sockets, FFmpeg contexts, GPU resources, display objects, or process handles.

The existing streaming engine remains responsible for frames, encoding, packetization, and transport. Core receives normalized measurements through adapters and returns assessments. A later actuator layer may translate reviewed decisions into engine operations, but that interface is not part of milestone 0.

## 2. Data flow

    Streaming engine / verified host adapters
                    |
                    v
           TelemetrySample (session scoped)
                    |
             validation + freshness
                    |
                    v
             PipelineAssessment
                    |
             diagnostic consumer
                    |
               logs / future UI

The first implementation is a pure value model and deterministic evaluator. No background thread, global registry, database, network endpoint, or settings mutation is introduced.

## 3. Core data types

- telemetry_sample: optional measurements and monotonic observation time.
- telemetry_policy: freshness window and explicit stage thresholds.
- bottleneck: healthy, capture-bound, encode-bound, network-bound, client-bound, mixed, insufficient-data.
- pipeline_assessment: classification, confidence, explanation, and stage evidence.

All numeric measurements use explicit units and are validated before classification. Missing values remain absent.

## 4. Classification algorithm

1. Validate policy bounds and sample values.
2. Reject samples whose age is negative or exceeds the freshness window.
3. Determine which stages have sufficient evidence.
4. Mark each available stage elevated when its measurement crosses its policy threshold.
5. If multiple independent stages are elevated, return mixed.
6. If exactly one stage is elevated, return that stage.
7. Return healthy only when the minimum required evidence is present and no stage is elevated.
8. Otherwise return insufficient-data.

Client-bound is reserved until client decode/presentation measurements exist. It is not inferred from server send completion.

## 5. Threading and ownership

Milestone 0 has no shared mutable state. Samples and assessments are values. Any future per-session store must be owned by the session lifecycle and must not share controller state between clients.

## 6. Failure behavior

- Invalid policy: return an explicit insufficient-data result; never silently clamp thresholds.
- Invalid measurement: reject the sample.
- Stale measurement: report insufficient data.
- Missing measurements: preserve absence and report insufficient data unless remaining evidence supports a narrower classification.
- Unsupported client telemetry: remain unavailable; do not synthesize it.

## 7. Compatibility

New source files must be added to the explicit SUNSHINE_TARGET_FILES list so both server and test executable compile the same implementation. Tests are discovered recursively from tests/.

The module must remain platform-neutral and avoid Linux-only APIs. Runtime adapters may be platform-specific and must normalize into this interface.

## 8. Security and privacy

Telemetry is operational data scoped to a streaming session. Do not include client identifiers beyond the existing opaque session boundary. Never log secrets or raw pairing material. No telemetry is sent to third parties.

## 9. Decisions

- Core is a layer above the streaming engine, not a replacement for it.
- APD begins observe-only.
- Missing and stale data are first-class states.
- The initial classifier does not make automatic changes.
- Client decode/presentation metrics are unsupported until an explicit, tested protocol source exists.
