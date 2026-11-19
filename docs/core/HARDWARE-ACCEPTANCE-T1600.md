# T1600 Hardware Acceptance — Syzygy Core and Headless Streaming

Status: pending physical-host execution.

## Target host

- Fedora 44 KDE Plasma
- Dell Precision T1600
- NVIDIA GeForce GTX 1060 6 GB
- NVIDIA driver 580.178.04
- NVENC H.264 / HEVC paths
- Headless KDE/Wayland and audio/touch acceptance tracked separately from GPU encoding

CI software-rendered capture tests do not satisfy this hardware acceptance.

## Test preparation

1. Build/install the candidate Syzygy RPM from the tested PR commit.
2. Record `syzygy --version`, kernel, driver, GPU name, and capture backend.
3. Confirm NVENC is available and note the selected codec/preset.
4. Keep the client, network path, and game workload consistent across comparisons.
5. Confirm APD is observe-only; no automatic control action is permitted.

Record GPU telemetry in a separate terminal:

```bash
nvidia-smi --query-gpu=timestamp,name,driver_version,utilization.gpu,utilization.memory,memory.used,temperature.gpu,power.draw,clocks.gr,clocks.mem --format=csv -l 1 > ~/syzygy-t1600-gpu.csv
```

Capture Syzygy debug output for the same interval. The current diagnostics emit once per session every five seconds and include host pipeline, NVENC API-to-output, queue wait, classification, confidence, and explanation.

## Workload matrix

Run each case for at least five minutes after a one-minute warm-up:

| Resolution / rate | Codec | Capture mode | Purpose |
| --- | --- | --- | --- |
| 1280x720 @ 60 | H.264 | logged-in KDE | Lower-load baseline |
| 1920x1080 @ 60 | H.264 | logged-in KDE | Primary GTX 1060 baseline |
| 1920x1080 @ 60 | HEVC | logged-in KDE | NVENC HEVC path |
| 1920x1080 @ 60 | H.264 | headless session | Headless encoder/capture path |
| 1920x1080 @ 60 | HEVC | headless session | Headless HEVC path |

If a requested mode is unsupported, record the failure and actual negotiated mode; do not substitute silently.

## Record for each run

- Syzygy commit SHA and package version
- resolution, frame rate, codec, encoder preset, capture backend
- GPU utilization, memory use, temperature, power, and clocks
- CPU utilization and process load
- client-reported frame rate and visible stutter/latency observations
- APD host-pipeline, NVENC API-to-output, and send-queue measurements
- network type and observed conditions
- dropped frames, encoder errors, capture failures, and audio/input behavior

## Acceptance gates

- Per-session telemetry follows the correct client when two sessions are tested.
- NVENC timing is present only for frame-index-matched output and is not described as pure GPU execution time.
- Queue wait is distinguishable from socket/network delivery.
- No sample from one session affects another session.
- Missing/stale samples never become zero or produce a control action.
- Diagnostics broadly agree with independent GPU and client observations.
- No regression in stream stability, audio routing, touch isolation, or cleanup.

## Outstanding limitations

- Capture-stage timing is not yet wired.
- AVCodec/FFmpeg encode timing is not yet wired.
- Network RTT/jitter and a valid packet-loss rate are not yet wired.
- Client decode/presentation telemetry is not available from the inspected protocol path.
- The user must run this matrix on the physical T1600; CI cannot emulate the GTX 1060.
