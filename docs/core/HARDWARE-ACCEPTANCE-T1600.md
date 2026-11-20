# T1600 Hardware Acceptance — Syzygy Core and Headless Streaming

Status: pending physical-host execution. No physical T1600 measurements were produced by CI or this automation run.

## Target host

- Fedora 44 KDE Plasma
- Dell Precision T1600
- NVIDIA GeForce GTX 1060 6 GB
- NVIDIA driver 580.178.04
- NVENC H.264 / HEVC paths
- Headless KDE/Wayland and audio/touch acceptance tracked separately from GPU encoding

CI software-rendered capture tests do not satisfy this hardware acceptance.

## Test preparation

1. Build/install the candidate Syzygy RPM from the exact tested PR commit.
2. Record `syzygy --version`, package version, kernel, driver, GPU name, and capture backend.
3. Confirm NVENC is available and note the selected codec/preset.
4. Keep client, network path, scene, bitrate, FEC, and workload consistent across comparisons.
5. Confirm APD is observe-only; no automatic control action is permitted.
6. Start the stream, then run the collector from the repository root:

```bash
bash scripts/collect-t1600-apd-telemetry.sh 300 ~/syzygy-t1600-run-01
# Optional: pin NIC counters to the interface carrying the stream
SYZYGY_T1600_INTERFACE=YOUR_STREAM_INTERFACE bash scripts/collect-t1600-apd-telemetry.sh 300 ~/syzygy-t1600-run-01-nic
```

The collector records host/GPU information, one-second GPU and encoder utilization, process CPU/RSS, network-interface counters and qdisc statistics, the user and kernel journals, and the known Syzygy service log when present. It does not generate network traffic or change system settings. By default it samples the interface selected by a route lookup to `1.1.1.1`; set `SYZYGY_T1600_INTERFACE` to the stream-facing interface if the stream uses a different NIC. It creates a compressed bundle next to the output directory. It does not start a stream or change system settings.

For a longer soak:

```bash
bash scripts/collect-t1600-apd-telemetry.sh 600 ~/syzygy-t1600-soak
```

## Workload matrix

Run each supported case for at least five minutes after a one-minute warm-up. Repeat the primary case at least five times and perform a ten-minute soak after the baseline is stable.

| Resolution / rate | Codec | Capture mode | Purpose |
| --- | --- | --- | --- |
| 1280x720 @ 60 | H.264 | logged-in KDE | Lower-load baseline |
| 1920x1080 @ 60 | H.264 | logged-in KDE | Primary GTX 1060 baseline |
| 1920x1080 @ 60 | HEVC | logged-in KDE | NVENC HEVC path |
| 1920x1080 @ 60 | H.264 | headless session | Headless encoder/capture path |
| 1920x1080 @ 60 | HEVC | headless session | Headless HEVC path |
| 1920x1080 @ 60 | H.264 | two clients, if stable | Per-session isolation and shared-sender contention |

If a requested mode is unsupported, record the failure and actual negotiated mode; do not substitute silently.

## Record for each run

- Syzygy commit SHA and package version
- resolution, frame rate, codec, encoder preset, capture backend
- capture-to-encoder age, capture callback duration, conversion duration
- NVENC or FFmpeg submission-to-matching-output duration
- internal packet queue wait and combined host pipeline time
- control-channel RTT and RTT variance; do not label these as media-path jitter
- client-reported RFC 3550 media inter-arrival jitter only when a compatible v2 sender is present
- FEC-reported per-block loss and recovery counts when the client sends them
- client decode/presentation values only if a compatible `0x5506` v1/v2 sender is present
- client media inter-arrival jitter only if that sender supports v2
- GPU utilization, memory use, temperature, power, and clocks
- CPU utilization and process load
- client-reported frame rate and visible stutter/latency observations
- dropped frames, encoder errors, capture failures, audio/input behavior
- whether the session used KMS, X11 SHM/non-SHM, or another backend

## Acceptance gates

- Per-session telemetry follows the correct client when two sessions are tested.
- Capture backend duration is present only for a backend with a verified callback boundary.
- Conversion is distinguishable from capture and encoder submission/output.
- NVENC and FFmpeg timing is present only for frame-index/PTS-matched output.
- FFmpeg delayed/reordered output never receives the most recently submitted frame's metadata.
- Queue wait is distinguishable from socket/network delivery.
- Control RTT/variance is labeled as ENet control-path telemetry, not one-way media latency.
- FEC loss uses the reported block denominator; no report is not treated as zero.
- Client decode/presentation is unavailable unless an explicit compatible sender is used.
- No sample from one session affects another session.
- Missing/stale samples never become zero or produce a control action.
- Diagnostics broadly agree with independent GPU and client observations.
- No regression in stream stability, audio routing, touch isolation, or cleanup.

## Outstanding limitations

- The physical T1600 workload matrix remains unrun.
- Media-path inter-arrival jitter has a strict v2 host receiver, but is not measured by ordinary clients because no sender is included in this repository; ENet RTT variance remains only a control-path proxy.
- Packetizer/FEC duration, socket submission duration, and send errors are not yet separate telemetry stages.
- Encoder in-flight depth and GPU surface completion are not yet connected to runtime telemetry.
- The `0x5506` client feedback receiver exists, but no client sender is included in this repository.
- CI software rendering cannot emulate the GTX 1060, its NVENC driver path, or the physical display capture stack.
