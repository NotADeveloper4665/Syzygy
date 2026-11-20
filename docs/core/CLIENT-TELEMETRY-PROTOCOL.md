# Syzygy Client Telemetry Extension — v1

Status: host receiver and parser implemented; client sender is not included in this repository.

## Purpose

Provide explicit client-side decode and presentation measurements without pretending host send time is end-to-end latency. The values are client-measured durations; no host/client clock synchronization is required.

## Control message

- Type: `0x5506`
- Direction: client to host
- Transport: authenticated/encrypted ENet control channel
- Delivery: best-effort, unsequenced
- Payload length: exactly 28 bytes
- Multi-byte integers: big-endian
- Version: 1
- Magic: ASCII `SZY1`

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | magic | `SZY1` |
| 4 | 1 | version | `1` |
| 5 | 1 | flags | Must be `0` in v1 |
| 6 | 2 | payload_size | Big-endian `28` |
| 8 | 4 | last_frame_index | Latest host frame included in the aggregate window |
| 12 | 2 | sample_count | Number of frames included; 1–240 |
| 14 | 2 | window_ms | Aggregate window; 1–5000 ms |
| 16 | 4 | decode_average_us | Average decoder time until the frame is ready for presentation |
| 20 | 4 | presentation_queue_average_us | Average time decoded frames wait before renderer submission |
| 24 | 4 | render_average_us | Average client render/present submission duration |

Each duration must be no greater than 5,000,000 microseconds. Queue plus render time must also remain within that limit.

## Sampling contract

- Send one aggregate report approximately every 250 ms while streaming.
- `last_frame_index` must refer to the host's frame index, not a client-local counter.
- Each average covers the stated `window_ms` and `sample_count`.
- `presentation_ms` at the host is derived as queue average plus render average.
- Do not include compositor scan-out, physical panel response, or input-device latency unless the client can measure those separately and the protocol is extended with explicit fields.
- Do not send host-clock timestamps or infer one-way network latency from unsynchronized clocks.

## Host validation

The host parser rejects wrong size, magic, version, non-zero flags, invalid sample/window counts, zero frame index, and out-of-range durations. The control handler also rejects feedback whose frame index is ahead of the latest sent frame, more than 512 frames behind, or not newer than the last accepted report.

The host stores decode, presentation-queue, render, and combined presentation values in the owning session's telemetry sample. They are observe-only and cannot change bitrate, FEC, frame rate, resolution, encoder, or pacing.

## Compatibility and current limitation

Existing Moonlight clients do not send this Syzygy-specific type. The parser is inert unless a client explicitly sends a valid v1 message. No client-side sender or negotiation change is part of this repository work, so client decode/presentation fields remain unavailable on ordinary clients until a compatible sender is implemented and validated.

The standard Moonlight `SS_FRAME_FEC_STATUS` message at `0x5502` remains separate and is used for per-FEC-block packet-loss feedback.
