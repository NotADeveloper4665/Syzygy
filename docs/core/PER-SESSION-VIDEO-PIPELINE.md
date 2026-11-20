# Per-session video admission and sender isolation

Status: implementation foundation; runtime wiring is the next required stage.

## Why this is separate from the existing global queue

The current video encoder publishes every session into one shared `video_packets` queue. That queue clears all queued packets when it reaches capacity. A single slow session can therefore erase another session's work, and a generic queue has no codec-reference awareness.

The new `bounded_session_video_queue<T>` is a per-session admission primitive for complete encoded frames. It enforces both a frame-count cap and a byte cap. It never blocks the encoder indefinitely and never silently treats queue overflow as normal operation.

## Overflow and decoder continuity

- A normal enqueue preserves FIFO order.
- If the queue fills on a dependent frame, only that session's queued frames are discarded, the overflow counter increments, and the queue enters `awaiting_keyframe`.
- Dependent P-frames are rejected while awaiting recovery.
- The next keyframe clears the recovery gate and is admitted as the first frame of the new queue epoch.
- An individual frame larger than the byte budget is rejected and also requires keyframe recovery.
- Queue closure wakes blocked consumers and rejects future producers.

The producer must translate `overflow_requires_keyframe`, `awaiting_keyframe`, and `item_too_large` into the existing per-session IDR request event. It must not reset frame numbering, RTP sequence numbers, or the AES-GCM IV counter.

## Initial bounds

Defaults are three complete frames and 8 MiB per session. These are conservative starting limits, not tuned production defaults. The byte cap is authoritative for large keyframes; the frame cap prevents a high-fps stream from accumulating too many small frames. Hardware acceptance must tune these against codec, resolution, frame rate, and client recovery behavior.

## Required runtime integration

1. Give each streaming session a distinct video queue identity and lifetime.
2. Route its encoder output to that session's queue rather than the global `video_packets` queue.
3. Replace the single global FIFO consumer with a fair per-session ready scheduler; do not let one empty queue block polling other sessions.
4. Preserve one ordered packetization/encryption stream per session. All RTP sequence allocation, FEC block numbering, frame indices and GCM IV allocation remain session-owned and monotonic.
5. Split packet preparation from paced socket submission only after the queue routing is tested. The prepared-packet handoff must have independent frame and byte limits.
6. Ensure shutdown closes queues before joining workers; no worker may retain a raw `session_t*` after session teardown.
7. Keep WAN pacing/FEC as the sole transport controller. Per-session sender clocks are allowed; a second adaptive bitrate or congestion controller is not.

## Rubylight improvements to carry forward

- Encoder admission must account for both encoder-owned in-flight frames and sender-queued frames; queue capacity alone does not bound GPU/driver work.
- Keyframes need reserved admission capacity and a bounded recovery path. A keyframe must not be starved behind stale P-frames.
- Track queue age as well as depth and bytes; two queues with the same depth can have very different latency.
- Keep packetizer and sender timing separate. Report packetization/FEC/encryption time, prepared-queue wait, pacing wait, socket submission time, and send errors independently.
- Use round-robin or deficit-round-robin session scheduling with a byte budget per turn. A packet-count scheduler is unfair when MTUs and FEC overhead differ.
- Detect host-local socket backpressure separately from network congestion. A local send error must not train WAN bitrate.
- Make queue budgets adaptive only after measurement; use hysteresis and dwell time, never a second competing controller.
- Keep GPU surface ownership and completion fences bounded separately from encoded-frame queues.
- Add deterministic multi-session stress tests: one deliberately stalled sender, one healthy sender, mixed MTUs, oversized IDRs, loss/recovery, encryption enabled, session shutdown while queued, and repeated reconnects.

## Current limitation

This commit adds and tests the admission primitive only. The existing runtime still uses the global video queue until the session routing, IDR event wiring, fair scheduler, and lifetime-safe worker shutdown are integrated. Do not enable or describe the runtime as per-session isolated until those steps land and multi-client tests pass.
