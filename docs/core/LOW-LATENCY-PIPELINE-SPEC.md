# Rubylight Low-Latency Pipeline Adaptation

Status: Draft engineering specification  
Target: Syzygy Linux-first host pipeline, with NVIDIA NVENC on the T1600 as the first hardware acceptance target  
Reference implementation: https://github.com/RamazanKara/Rubylight  
Reference license: GPL-3.0; retain upstream copyright and attribution for any source-derived implementation

## 1. Objective

Reduce host-added frame age and input-to-picture delay by keeping the capture, conversion, encoder, packetizer, and sender stages bounded and overlapped. Optimize for fresh pictures, not merely maximum encoder throughput or average FPS.

This is an adaptation plan, not a claim that Rubylight's Windows/Radeon measurements will reproduce on Linux/NVIDIA. Every behavior change must be gated by a reproducible A/B measurement and preserve Moonlight wire compatibility unless a separately negotiated extension is designed.

## 2. Findings from Rubylight

The relevant design is a coordinated pipeline rather than one isolated encoder setting:

1. **Latest-frame capture publication.** Capture publishes into a latest-picture slot. Consumers wait on publication and can tell whether the picture they are processing is still current. This avoids an unbounded FIFO of stale desktop frames.
2. **Explicit encoder/sender admission budget.** The host tracks frames encoding, prepared, and being sent. Encoder depth and sender occupancy share a small bounded picture budget. The encoder's demonstrated output depth is learned rather than assumed; sender work remains separately accounted for.
3. **Overlapped packetizer and paced sender.** A packetizer prepares output while a separate sender handles pacing and socket delivery. A slow network does not hold the packetizer's work on the same thread.
4. **Early delivery.** For supported large frames, encoder slices are handed off before the complete frame exists. Where output is whole, FEC blocks can be built and handed to an idle sender one at a time. The first packets can leave while later blocks are still being prepared.
5. **GPU-resident frame preparation.** Capture textures are handed to GPU compute with explicit fence ownership; conversion/scaling remains on the GPU and feeds the hardware encoder without a CPU pixel readback.
6. **Backlog-aware frame claims.** New inputs are admitted only when encoder and sender capacity permit. Keyframes and recovery frames receive special bounded treatment so low-latency dropping does not break reference dependencies.
7. **Pacing informed by the actual path.** The sender uses a link estimate and standing-queue evidence; it avoids building a persistent network queue. Existing congestion/FEC ownership must remain singular.
8. **Timing is measured end to end by stage.** Capture age, conversion, encoder, packetization, sender queue and client decode/present are kept distinct. Aggregate host timing is not double-counted as another stage.

The published performance record reports, under specific RX 7900 XT / Windows test conditions, lower capture-to-present latency with link-aware PyroWave pacing, reduced encoder picture age with VRR-aware capture, earlier delivery for large frames, and fewer loss-induced freezes after scene cuts. These are workload-specific results, not portable performance guarantees.

## 3. Syzygy baseline audit

The following is verified in the inspected Syzygy branch `feature/syzygy-core-intelligence`:

| Area | Current behavior | Consequence |
| --- | --- | --- |
| Capture-to-encoder handoff | `img_event_t` is a single-slot `safe::event_t`; a new publication replaces an unread image | Already has latest-frame semantics; do not add another FIFO |
| Encoder loop | Waits for an image, converts it, then encodes it | Capture and encode are not yet exposed as separately measured APD stages |
| Encoded output | Published to shared `mail::video_packets` queue | Queue depth, age and cross-session behavior need explicit review before changing policy |
| Queue overflow | Generic `safe::queue_t` clears its contents when full | Potentially destructive to reference-frame continuity; do not treat this as a valid freshness policy without proving frame invalidation/recovery |
| Broadcast | One `videoBroadcastThread` performs packet preparation, FEC, pacing and send | Packetization and pacing are serialized; a slow paced send can delay subsequent frame work |
| FEC | Each frame is split into FEC blocks, but the current path builds/sends a block within the same broadcast loop | Block-granular work exists; independently scheduling block production and sending requires a bounded handoff |
| GPU path | NVENC and FFmpeg backends exist; platform encode-device conversion is backend-specific | Need verify Linux CUDA/NVENC interop and VAAPI/DMABUF paths before claiming zero-copy |
| WAN control | Existing per-session WAN state and pacing logic exist, with a multi-client fallback | APD must not introduce a competing bitrate/FEC controller |

The capture mailbox already solves one major Rubylight problem. The largest structural opportunity is separating packetization from paced sending while introducing explicit per-session admission and reference-safe recovery.

## 4. Syzygy adaptation principles

- Keep Moonlight packet format, frame numbering, RTP sequencing, encryption counters, FEC metadata, IDR requests and reference-frame invalidation correct.
- Never drop an encoded reference-dependent frame silently. If a frame must be skipped, use the existing protocol-supported invalidation/recovery path and preserve monotonic wire counters.
- Use bounded queues with explicit overload outcomes; never clear a mixed-session queue wholesale.
- Maintain per-session fairness. One client's slow network must not stall another client's stream.
- Preserve existing WAN pacing as the only bitrate/FEC controller. APD observes and later recommends; it does not duplicate that controller.
- Preserve frame freshness, not just FIFO ordering. Keyframes, reference invalidation and stream restart boundaries need stronger guarantees than ordinary P-frames.
- Keep hardware resources alive until their GPU/encoder fences or completion callbacks prove ownership has returned.
- Keep observe-only behavior until stage telemetry and hardware A/B evidence are complete.

## 5. Implementation phases

### Phase A — Instrument before changing scheduling

- [ ] Measure capture publication-to-encoder-consumption age per session.
- [ ] Measure conversion duration separately from encoder submission/output.
- [ ] Measure encoder in-flight count and output age, including frame-index correlation.
- [ ] Measure encoded packet queue depth, oldest packet age and per-session occupancy.
- [ ] Measure packetization/FEC time, sender wait time, socket submission time and frame first-packet time.
- [ ] Record dropped/replaced frames by reason, frame type and recovery action.
- [ ] Add counters and deterministic tests for session isolation and monotonic frame/packet numbers.

Acceptance: diagnostics identify where host time accumulates without changing stream behavior.

### Phase B — Explicit bounded frame admission

- [ ] Replace implicit global queue-overflow clearing with an explicit admission contract.
- [ ] Track encoder-in-flight, packetizer-ready and sender-in-flight occupancy per session.
- [ ] Set a small configurable default budget and derive safe encoder depth from observed backend behavior.
- [ ] Apply freshness dropping only before encode, or after a protocol-safe reference invalidation decision.
- [ ] Reserve capacity for IDR and recovery frames.
- [ ] Ensure a congested session cannot consume another session's budget.

Acceptance: overload produces bounded frame age, no silent reference-chain corruption, and no regression in loss recovery.

### Phase C — Split packetization from paced sending

- [ ] Create a per-session bounded handoff between packetizer and paced sender.
- [ ] Packetizer prepares FEC/encryption metadata and hands complete blocks to the sender as soon as safe.
- [ ] Sender owns socket pacing and packet order; it never blocks the capture/encoder worker.
- [ ] Keep frame/block order, FEC block indices, packet sequence numbers and encryption IV counters deterministic.
- [ ] Apply fair scheduling across sessions.
- [ ] Ensure partial-frame aborts trigger a defined recovery path.

Acceptance: packetization overlaps network pacing, queue occupancy remains bounded, and frame first-packet time improves in a repeatable A/B test.

### Phase D — Early slice/block delivery

- [ ] Investigate whether the active Linux NVENC and FFmpeg encoders expose independently consumable slices.
- [ ] If supported, hand off encoded slices with explicit lifetime/ownership guarantees.
- [ ] Otherwise, implement early FEC-block delivery from complete encoded frames.
- [ ] Keep smaller frames whole when block handoff overhead exceeds measured benefit.
- [ ] Benchmark first-packet time, full-frame completion time, CPU cost and loss recovery.

Acceptance: enable only for sizes/backends where measured first-packet and end-to-end latency improve without correctness or CPU regressions.

### Phase E — GPU-resident capture and conversion

- [ ] Trace the Linux capture-to-encoder path for NVIDIA NVENC and VAAPI.
- [ ] Prefer DMA-BUF/CUDA/VAAPI surface sharing and explicit synchronization where supported.
- [ ] Remove avoidable CPU pixel readbacks and redundant GPU copies.
- [ ] Keep a tested fallback for incompatible drivers, formats or cross-device capture.
- [ ] Verify HDR, 10-bit, chroma formats and resource teardown under device loss.

Acceptance: GPU trace confirms no unnecessary CPU readback on the supported path; A/B tests show lower conversion time or host CPU without image regressions.

### Phase F — Pacing and APD integration

- [ ] Feed verified queue age, socket outcomes, RTT/jitter and loss-rate measurements into Core telemetry.
- [ ] Reuse the existing WAN controller and expose its decisions as evidence.
- [ ] Add APD recommendation-only output only after telemetry coverage is adequate.
- [ ] Add hysteresis, dwell, manual override and rollback before any future actuator.
- [ ] Keep automatic actions disabled by default.

Acceptance: no second controller; recommendations explain the evidence and cannot mutate stream state.

## 6. Platform transferability

| Rubylight technique | Syzygy transfer |
| --- | --- |
| Latest-frame slot | Already present in Syzygy's single-slot capture event; validate and preserve |
| Bounded encoder/sender budget | High priority; adapt to per-session C++ engine and multi-client queues |
| Packetizer + sender workers | High potential; largest structural change |
| Early FEC blocks | Potentially portable without client changes if Moonlight block ordering/flags remain exact |
| Encoder slice delivery | Backend-dependent; investigate NVENC/FFmpeg APIs on Linux before implementation |
| D3D12 shared fence / IddCx virtual display | Windows-only; do not port literally |
| CUDA/NVENC or VAAPI/DMABUF surface handoff | Linux-specific equivalent; verify current platform implementation first |
| VRR-aware capture | Windows method is not portable; use Linux DRM/KMS presentation timestamps only where reliable |
| Frame Warp | Requires coordinated client rendering and input prediction; not a host-only optimization |
| PyroWave codec | Optional future client/protocol negotiation, not a prerequisite for host pipeline work |

## 7. Measurement protocol

Use the same scene, resolution, codec, FPS, bitrate, FEC, client and network path for A/B runs. Alternate baseline and candidate order. Run at least five 60-second trials per condition after warm-up, then a 10-minute soak.

Record:
- capture-to-encode age (median, p95, p99)
- conversion and encode time (median, p95, p99)
- encoded-output-to-first-packet and full-frame send time
- packetizer and sender queue depth/age
- host CPU, GPU utilization, encoder utilization and memory bandwidth
- decoded frame count, lost/recovered frames, IDR requests and freezes
- client decode/presentation timing when the client exposes it

Do not accept a change on median latency alone if p95/p99, frame loss, image correctness, input responsiveness or multi-session fairness regresses.

## 8. Source and license handling

Rubylight is GPL-3.0. Syzygy is also GPL-3.0. Any directly adapted code must retain required copyright and license notices, identify the upstream file and revision, and be reviewed for dependency/API assumptions. Prefer an independently written Syzygy implementation of the documented algorithm where the source structure is platform-specific.

## 9. Initial implementation decision

Start with Phase A instrumentation and a review of the current generic packet queue. Do not immediately port Rubylight's Windows D3D12/IddCx code or enable frame dropping in the encoded packet queue. Once queue semantics and telemetry are tested, implement the per-session packetizer/sender handoff as the first behavior-changing optimization.


## 10. Scheduler implementation status (2026-11-20)

A platform-neutral `syzygy::core::session_scheduler<T>` primitive now exists in
`src/syzygy/core/session_scheduler.h`, with deterministic unit coverage in
`tests/unit/test_session_scheduler.cpp`.

Implemented in this foundation:

- Per-session FIFO queues with global and per-session byte/frame caps.
- Deficit-round-robin service accounting by encoded bytes, with bounded weights.
- Separate global and per-session recovery reserves for keyframes.
- Explicit admission outcomes; an overflow marks that session as requiring a
  recovery keyframe, and subsequent dependent frames are rejected until an
  IDR is admitted.
- Queue frame count, queued bytes, oldest-item age and recovery-required state.
- Session generation tokens; unregister clears queued ownership and rejects
  late enqueues from a previous connection generation.
- No queue eviction or silent frame reordering; FIFO order is retained within
  each session.

This is a tested scheduler primitive, **not yet the active runtime video queue**.
The existing encoder still publishes to the shared `mail::video_packets`
queue, and `videoBroadcastThread` still combines packetization, FEC,
encryption, pacing and socket submission. The next integration change must
move encoded-frame publication to the per-session scheduler, then split the
packetizer from the paced sender with a bounded block handoff. Keep this
distinction explicit in status reports until the runtime path and its teardown
tests are complete.


## 11. Follow-up gaps and new reusable primitives (2026-11-20)

A second implementation pass adds platform-neutral primitives in
`src/syzygy/core/pipeline_primitives.h`:

- **Wire-cost admission:** `estimate_wire_cost()` estimates data shards,
  parity shards, encryption prefix and IPv4/IPv6 network overhead. The DRR
  scheduler's byte field must be populated with this wire-cost estimate before
  packetization, then reconciled to actual shard bytes after FEC generation.
- **Socket pressure classification:** `classify_socket_error()` separates
  host-local backpressure (`EAGAIN`, `EWOULDBLOCK`, `ENOBUFS`, `EINTR`),
  MTU/packet-size failure (`EMSGSIZE`), route/peer failures and unknown errors.
  Local backpressure is not a remote-congestion signal and must not independently
  trigger bitrate/FEC adaptation.
- **Unified frame budget:** move-only `pipeline_budget::reservation` tokens
  reserve frame and byte capacity once, then travel with the same frame through
  encoder-in-flight, packetizer-ready and sender-in-flight stages. Capacity is
  released only when the final owner drops the token. This prevents a bounded
  queue from simply shifting backlog into the next stage.
- **GPU completion gate:** `completion_gate` represents explicit backend
  completion. A surface lease must remain owned until the CUDA/NVENC, VAAPI or
  DMA-BUF fence/callback confirms the resource can be reused; dequeue alone is
  not completion.

The new `tests/core_pipeline_primitives_smoke.cpp` covers wire overhead,
socket error classes, aggregate reservation movement between stages, and
explicit completion. The Linux workflow now compiles and runs this smoke test.

### Required runtime composition

The intended runtime flow is:

1. At encoder admission, acquire one aggregate `pipeline_budget::reservation`
   sized for the frame's conservative wire cost and encoder surface cost.
2. Keep the GPU surface lease and completion gate with the frame owner. The
   reservation and surface lease must not be released merely because encoded
   output has been dequeued.
3. After encode, enqueue the encoded frame into the per-session scheduler,
   charging estimated wire bytes. Keep the same aggregate reservation attached.
4. Packetize in bounded FEC blocks. Each block is a scheduling unit, preserving
   FIFO within a session and allowing DRR to alternate sessions at FEC-block
   boundaries. Do not interleave packets inside a block or reorder blocks within
   a frame.
5. The sender owns each session's packet sequence and encryption IV counter.
   The sender reports actual socket outcomes and actual wire bytes; the budget
   is released only after send completion or an explicit abort/recovery path.
6. On teardown, invalidate the session generation, stop new admission, cancel
   queued work, and wait for in-flight frame/block owners to release their
   reservations and session-state references before destroying ciphers or
   session buffers.

### Runtime work still outstanding

These primitives are deliberately not described as runtime features yet.
Current encoders still publish through `mail::video_packets`, and the
single `videoBroadcastThread` still owns packetization, FEC, encryption,
pacing and socket submission. The following are still required:

- Replace the raw `session_t*` packet channel reference with a lifetime-safe
  session handle/generation that keeps the required immutable session state
  alive across worker handoff.
- Wire the aggregate reservation into encoder admission before conversion or
  encode work is committed, not only after an encoded packet exists.
- Move encoded output into the per-session DRR scheduler.
- Split packetization from socket pacing with a bounded FEC-block handoff.
- Keep packet sequence allocation, GCM IV allocation and encryption under one
  serialized per-session sender owner. Do not parallelize these stateful steps.
- Record partial batch sends and local kernel pressure separately from remote
  loss/RTT signals.
- Add teardown/reconnect stress tests with work held in every pipeline stage.

The scheduler can prevent cross-session head-of-line blocking when its items
are complete FEC blocks rather than whole frames. It cannot safely interleave
blocks until the packetizer has produced a complete block and the protocol
ordering/recovery rules are verified. The runtime must not split or reorder
within a block.


The pipeline primitives now also include a **session lifetime drain fence**:
`session_lifetime` issues generation-bound leases for every queued/in-flight owner.
Teardown closes admission first, then waits for outstanding leases to drain
before releasing session-owned cipher and buffer state. The smoke test holds
an in-flight lease across teardown and verifies new leases are rejected until
the original owner releases. This is still an integration primitive; encoder,
FEC-block and sender jobs must actually carry leases for the fence to protect
the runtime.
