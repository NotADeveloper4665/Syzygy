# Telemetry source registry

This file records the exact provenance, units, timing boundaries, freshness group, and limitations of every metric connected to Syzygy Core. A value may not be promoted to a more specific stage metric without a separately verified boundary.

## Active adapters

| Core field | Source | Unit / conversion | Timing boundary | Freshness group | Limits |
| --- | --- | --- | --- | --- | --- |
| `capture_ms` | `platf::img_t::frame_timestamp` carried through `video::encode()` | `steady_clock` milliseconds | Capture timestamp to encoder consumption; includes latest-slot/mailbox wait | Host | Timestamp may be unavailable on a backend; not pure capture-backend execution time |
| `capture_backend_ms` | `img_t::capture_duration_ms` set by Linux KMS and X11 snapshot callbacks | `steady_clock` milliseconds | Callback entry before refresh/readback through image/surface availability and cursor composition | Host | Wired for Linux KMS readback, KMS surface, X11 SHM and non-SHM paths; other backends remain unavailable |
| `conversion_ms` | `session->convert(*img)` in async and synchronized encode paths | `steady_clock` milliseconds | Immediately before conversion call to its return | Host | Includes any blocking synchronization inside the conversion call; does not include encoder submission |
| `encode_ms` | NVENC `encode_frame()` and AVCodec `avcodec_send_frame()` / matching `avcodec_receive_packet()` | `steady_clock` milliseconds | Successful submission attempt to matching output availability | Host | Includes driver/API wait and encoder buffering/reordering; not pure GPU execution time |
| `send_queue_ms` | `packet_raw_t::queued_at` to `videoBroadcastThread()` dequeue | `steady_clock` milliseconds | Packet publication immediately before `packets->raise()` to broadcaster dequeue | Host | Internal shared queue wait only; excludes FEC, socket submission, network transit, and client presentation |
| `host_pipeline_ms` | `packet::frame_timestamp` and existing Moonlight `frame_processing_latency` calculation | 0.1 ms wire units converted to milliseconds | Captured-frame timestamp to broadcaster observation immediately before packet header construction | Host | Aggregate host-side time; overlaps detailed host stages and is never counted as an independent stage |
| `network_rtt_ms` | Owning `ENetPeer::roundTripTime` | Milliseconds | Smoothed ENet control-channel RTT, sampled only when ENet's acknowledged-packet `lastReceiveTime` token advances | Network | Control-path RTT, not one-way media latency; polling an unchanged cached estimator does not refresh sample freshness |
| `network_rtt_variance_ms` | Owning `ENetPeer::roundTripTimeVariance` | Milliseconds | ENet's smoothed RTT-variance estimator, timestamped with the same new acknowledged-packet sample as RTT | Network | A control-path variation proxy; not RTP inter-arrival jitter or one-way media jitter |
| `packet_loss_per_mille` | Moonlight `SS_FRAME_FEC_STATUS` control message `0x5502` | `(totalDataPackets - receivedDataPackets) * 1000 / totalDataPackets` | One reported FEC block, before Reed-Solomon recovery | Loss | Client emits this report only for blocks requiring recovery; no report is unknown, not zero. It is not a complete stream-wide loss rate and is retained as conditional diagnostic evidence, not an independent network-stage classifier input |
| `packet_loss_count` | `SS_FRAME_FEC_STATUS` or legacy `IDX_LOSS_STATS` | Lost data shards or legacy packet count | One FEC block or legacy report interval | Loss | Raw count is retained separately from rate |
| `packet_loss_interval_ms` | First two little-endian 32-bit values in the legacy 32-byte `IDX_LOSS_STATS` payload | Milliseconds | Client-reported interval since the previous legacy report | Loss | No verified packet denominator; never derive a percentage from count/time |
| `client_decode_ms` | Syzygy client telemetry extension `0x5506`, v1 | Client average microseconds converted to milliseconds | Client-reported aggregate window | Client | Host parser is wired; no client sender is included in this repository |
| `client_presentation_queue_ms` | Syzygy client telemetry extension `0x5506`, v1 | Client average microseconds converted to milliseconds | Decode-ready to renderer submission queue | Client | Requires a compatible client sender |
| `client_render_ms` | Syzygy client telemetry extension `0x5506`, v1 | Client average microseconds converted to milliseconds | Client render/present submission duration | Client | Does not include physical panel scan-out or response |
| `client_presentation_ms` | Sum of client presentation-queue and render averages | Milliseconds | Client-reported queue plus render interval | Client | Not end-to-end or physical display latency |

## Safe FFmpeg frame correlation

AVCodec output can be delayed or reordered relative to the frame most recently submitted. Syzygy records timing metadata in a bounded, session-owned PTS registry when `avcodec_send_frame()` succeeds. The registry is keyed by the submitted frame PTS and is consumed only when an output packet has the same PTS.

- Missing or unknown PTS produces no stage metadata; current-frame metadata is never substituted.
- Pending records are capped at 256 per encoder session.
- Oldest pending records are evicted when the cap is reached; later output for an evicted PTS remains unmeasured.
- Duplicate PTS registration is rejected.
- FFmpeg encode time is successful submission-to-matching-output availability, including encoder buffering/reordering and driver wait.

## Feedback protocol provenance

### Moonlight FEC status

`SS_FRAME_FEC_STATUS` is the client-to-host `0x5502` message defined by moonlight-common-c. Its multi-byte fields are network byte order. The client populates the frame index, FEC block index/count, FEC percentage, total data shards, and directly received data shards before recovery. Syzygy validates the exact 21-byte payload, rejects impossible counters and FEC percentages outside 0–100, accepts only frames for which at least one FEC block has entered the host send path and which are within 512 frames of that bound, and deduplicates the last 64 frame/block keys per session.

The denominator is the reported block's `totalDataPackets`. Since the client reports blocks that needed recovery, the result is a recovery-triggered block sample, not a continuous loss estimate. See upstream definitions in [Video.h](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/Video.h) and population in [RtpVideoQueue.c](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/RtpVideoQueue.c).

### Legacy loss statistics

Legacy `IDX_LOSS_STATS` is accepted only at its exact 32-byte size. Syzygy reads the little-endian count and interval fields safely and stores them as raw observations. No per-mille value is inferred.

### Client decode and presentation

The optional Syzygy `0x5506` extension is specified in [CLIENT-TELEMETRY-PROTOCOL.md](CLIENT-TELEMETRY-PROTOCOL.md). The host parser validates v1/v2 magic, version, exact length, duration bounds, v2 jitter bounds, and recent frame index. V1 supplies decode/presentation; v2 additionally supplies RFC 3550 media inter-arrival jitter. This repository contains no client sender, so these values remain unavailable on ordinary sessions.

## Known but not connected

| Candidate | Why it is not mapped yet |
| --- | --- |
| Media-path packet jitter | A strict v2 host parser exists, but no compatible client sender is included in this repository; ordinary sessions still have no media inter-arrival jitter sample |
| Packetizer/FEC duration | The shared broadcaster still combines packetization and FEC work; a dedicated stage boundary is required |
| Socket submission duration and send errors | Must be measured separately from queue wait and remote network congestion |
| Encoder in-flight count and output age | No backend-independent in-flight observation is connected yet |
| Client decode/presentation values | Host receiver exists, but a compatible client sender is not present in this repository |

## Rules

- Missing is not zero.
- Do not infer a packet-loss percentage from legacy count/time without a denominator.
- Do not treat an absent FEC report as zero loss.
- Do not classify a single recovery-triggered FEC block report as a stream-wide network bottleneck; use it as conditional diagnostic evidence only.
- Do not label ENet RTT variance as one-way or media-packet jitter.
- Do not label internal sender queue wait as network transit.
- Never label combined host-pipeline time as encode, capture, network, or client latency.
- Keep samples and freshness timestamps owned by the corresponding session.
- Preserve monotonic timestamps; stale or future-dated groups are ignored independently.
- Keep APD observe-only; telemetry cannot mutate stream configuration.
