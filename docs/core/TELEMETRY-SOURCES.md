# Telemetry source registry

This file records the exact provenance and limits of every metric currently connected to Syzygy Core. A value may not be promoted to a more specific stage metric without a separately verified timing boundary.

## Active adapters

| Core field | Source | Unit / conversion | Timing boundary | Scope | Limits |
| --- | --- | --- | --- | --- | --- |
| `host_pipeline_ms` | `video::packet_t::frame_timestamp` carried into `stream::videoBroadcastThread()` | Existing Moonlight frame-processing field is in 0.1 ms units; Core stores milliseconds as `latency / 10.0` | Captured-frame timestamp to the broadcaster's observation point immediately before packet header construction | Owning `session_t` | Aggregate host-side processing only; does not isolate capture, conversion, encode, queueing, network transit, or client presentation |
| `encode_ms` | `nvenc_encode_session_t::encode_frame()` result | Milliseconds from NVENC API entry until matching frame-index output is returned | Submission-to-output availability; only recorded when output index matches submitted frame | Owning `session_t` via packet metadata | NVENC-only. Includes driver/API wait and output availability, not pure GPU-kernel time. AVCodec/FFmpeg timing remains unavailable pending safe frame correlation. |
| `send_queue_ms` | `packet_raw_t::queued_at` to `stream::videoBroadcastThread()` dequeue | `steady_clock` duration in milliseconds | Packet publication immediately before `packets->raise()` to broadcaster dequeue | Owning `session_t` via packet `channel_data` | Internal packet queue wait only; excludes FEC, socket send, network transit, and client presentation |

## Known but not connected

| Candidate | Existing source | Why it is not mapped yet |
| --- | --- | --- |
| Packet loss | `IDX_LOSS_STATS` payload in `stream.cpp` | Payload reports a loss count and elapsed milliseconds. A verified packet denominator/window contract is not established, so no per-mille rate is inferred. |
| AVCodec/FFmpeg encode latency | `encode_avcodec()` in `video.cpp` | Not connected: output may be delayed/reordered relative to submitted frames; a frame-index-correlated timing map is required. |
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
