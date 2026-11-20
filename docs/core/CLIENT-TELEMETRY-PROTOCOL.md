# Syzygy Client Telemetry Extension — v1 and v2

Status: host receiver/parser implemented for v1 and v2; no client sender is included in this repository.

## Purpose

Provide explicit client-side decode, presentation, and media-packet arrival measurements without treating host send time as end-to-end latency. Durations are measured on the client; no host/client clock synchronization is required.

## Control message

- Type: `0x5506`
- Direction: client to host
- Transport: authenticated/encrypted ENet control channel
- Delivery: best-effort, unsequenced
- Magic: ASCII `SZY1`
- Multi-byte integers: big-endian
- Flags: must be zero
- Supported versions: v1 (28 bytes), v2 (32 bytes)

### Common fields

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | magic | `SZY1` |
| 4 | 1 | version | `1` or `2` |
| 5 | 1 | flags | Must be `0` |
| 6 | 2 | payload_size | Exact version length: 28 or 32 |
| 8 | 4 | last_frame_index | Latest host frame included in the aggregate window |
| 12 | 2 | sample_count | Number of frames included; 1–240 |
| 14 | 2 | window_ms | Aggregate window; 1–5000 ms |
| 16 | 4 | decode_average_us | Average decoder time until the frame is ready for presentation |
| 20 | 4 | presentation_queue_average_us | Average time decoded frames wait before renderer submission |
| 24 | 4 | render_average_us | Average client render/present submission duration |

### V2 extension

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 28 | 4 | media_interarrival_jitter_us | Current smoothed RFC 3550 RTP inter-arrival jitter estimate, converted to microseconds using the negotiated RTP clock rate |

V1 is unchanged and remains accepted. V2 contains every v1 field plus the jitter field. Each duration and the jitter estimate must be no greater than 5,000,000 microseconds. Presentation queue plus render time must also remain within that limit.

## Sampling contract

- Send one aggregate report approximately every 250 ms while streaming.
- `last_frame_index` must refer to the host's frame index, not a client-local counter.
- Decode, presentation-queue, and render averages cover the stated `window_ms` and `sample_count`.
- V2 jitter is the current smoothed RFC 3550 inter-arrival jitter estimate at report time; it is not an average of decoder time and is not derived from ENet RTT variance.
- `presentation_ms` at the host is derived as queue average plus render average.
- Do not include compositor scan-out, physical panel response, or input-device latency unless separately measurable and explicitly added in a future version.
- Do not send host-clock timestamps or infer one-way network latency from unsynchronized clocks.

## Host validation

The host parser rejects wrong size, magic, version, flags, declared size, invalid sample/window counts, zero frame index, and out-of-range durations. V2 additionally rejects jitter above five seconds.

The control handler rejects feedback whose frame index is ahead of the latest frame whose complete host send path has finished, more than 512 frames behind, or not newer than the last accepted report. Comparisons use 32-bit serial-number arithmetic so the bounded window remains valid across frame-index wrap. Decode/presentation and media-jitter freshness timestamps are stored independently so a v1 report cannot refresh an older v2 jitter value.

The host stores decode, presentation-queue, render, and combined presentation values in the owning session's client telemetry group. V2 media jitter is stored separately as client-reported network-path evidence. All values are observe-only and cannot change bitrate, FEC, frame rate, resolution, encoder, capture, or pacing.

## Compatibility and current limitation

The standard Moonlight `0x0204` Frame Stats message is marked unused in moonlight-common-c, so it is not treated as a source of decode/presentation timings. Existing Moonlight clients do not send this Syzygy-specific type. The host receiver is inert unless a client explicitly sends a valid v1 or v2 message. No client-side sender or negotiation change is included in this repository, so decode/presentation feedback and media jitter remain unavailable on ordinary clients until a compatible sender is implemented and validated.

The standard Moonlight `SS_FRAME_FEC_STATUS` message at `0x5502` remains separate and supplies per-FEC-block data-shard loss for blocks that required recovery. It is not a continuous stream-wide loss estimate.
