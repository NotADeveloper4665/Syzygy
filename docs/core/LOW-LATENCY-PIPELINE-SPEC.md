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
