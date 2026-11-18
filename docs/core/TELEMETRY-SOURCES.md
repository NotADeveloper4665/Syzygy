# Telemetry source registry

This file records the exact provenance and limits of every metric currently connected to Syzygy Core. A value may not be promoted to a more specific stage metric without a separately verified timing boundary.

## Active adapters

| Core field | Source | Unit / conversion | Timing boundary | Scope | Limits |
| --- | --- | --- | --- | --- | --- |
| `host_pipeline_ms` | `video::packet_t::frame_timestamp` carried into `stream::videoBroadcastThread()` | Existing Moonlight frame-processing field is in 0.1 ms units; Core stores milliseconds as `latency / 10.0` | Captured-frame timestamp to the broadcaster's observation point immediately before packet header construction | Owning `session_t` | Aggregate host-side processing only; does not isolate capture, conversion, encode, queueing, network transit, or client presentation |

## Known but not connected

| Candidate | Existing source | Why it is not mapped yet |
| --- | --- | --- |
| Packet loss | `IDX_LOSS_STATS` payload in `stream.cpp` | Payload reports a loss count and elapsed milliseconds. A verified packet denominator/window contract is not established, so no per-mille rate is inferred. |
| Encode latency | Encoder entry/exit paths in `video.cpp` | No stable, backend-independent timing boundary has been added for both FFmpeg and NVENC paths. |
| Capture latency | Platform capture callbacks | Capture backend timing boundaries vary and are not normalized yet. |
| Network RTT / jitter | Existing control ping and feedback paths | No verified per-session RTT/jitter sample adapter has been implemented for Core. |
| Client decode / presentation | Moonlight feedback path inspected for this milestone | Not currently supplied by the inspected protocol path. |

## Rules

- Missing is not zero.
- Never infer a packet-loss percentage from count/time without a packet denominator.
- Never label combined host-pipeline time as encode, capture, network, or client latency.
- Keep samples owned by the corresponding session.
- Preserve monotonic timestamps; reject stale and future-dated samples.
- New adapters require a source citation in code comments, explicit units, timing boundaries, tests, and an update to this registry.
