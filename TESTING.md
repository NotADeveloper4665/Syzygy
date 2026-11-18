# Syzygy Verification Plan

## Principles

A successful build is not proof of runtime correctness. Verification layers are: focused unit tests, integration tests, CI package/smoke tests, target-hardware acceptance, and regression checks. Record commit SHA, package version, environment, command, expected/actual result, and log evidence.

## Automated verification matrix

| Area | Check | Expected result |
| --- | --- | --- |
| CLI | Headless/headless-touch parsing | Correct mode and forwarded arguments |
| CLI | Incompatible portal/KMS/admin combinations | Rejected |
| Environment | Strip inherited desktop/audio variables | Private session cannot attach to caller desktop |
| Environment | Preserve persistent config | Pairing/config remains persistent |
| Dependencies | Missing runtime command | Fail before child process creation |
| Audio | Sink and monitor exist | Continue only when both exist |
| Audio | Monitor missing | Clear startup failure |
| Touch | Tablet config scope | Only disposable private KDE config changes |
| Mounts | Healthy/stale/absent FUSE mount | Normal/lazy/no-op cleanup as appropriate |
| Lifecycle | Child ownership | Only owned children are terminated |
| Package | Linux RPM build | RPM and installed-layout artifact produced |
| Capture | Headless KWin to PipeWire smoke test | Capture works without physical monitor |

## Target hardware acceptance

Target: Dell Precision T1600, Fedora 44 KDE, GTX 1060 6 GB. Record exact versions at test time.

### Preflight
- [ ] Record commit SHA and RPM NEVRA.
- [ ] Confirm binary path is /usr/bin/syzygy.
- [ ] Record Fedora release, kernel, NVIDIA driver, KWin, and PipeWire versions.
- [ ] Confirm regular-user render-node access.
- [ ] Confirm no managed Syzygy session is already running.

### Startup and capture
- [ ] Start standard headless mode with automatic backend selection.
- [ ] Confirm KWin/PipeWire readiness.
- [ ] Record selected capture backend and encoder.
- [ ] Stream for at least 10 minutes.
- [ ] Confirm no physical monitor is required and physical desktop is not captured.
- [ ] Test explicit NVENC selection.
- [ ] Verify H.264 and HEVC separately; record negotiated codec and GPU encoder utilization.

### Audio
- [ ] Confirm syzygy_headless and syzygy_headless.monitor exist in private session.
- [ ] Launch an application and generate audio.
- [ ] Confirm streamed audio contains application output.
- [ ] Confirm physical desktop default sink and streams are unchanged.
- [ ] Repeat start/stop and confirm no stale nodes accumulate.

### Touch and isolation
- [ ] Start --headless-touch.
- [ ] Confirm tablet mode changed only in private KDE config.
- [ ] Test touch, pointer, keyboard, and hover from a compatible client.
- [ ] Confirm input reaches private desktop/application.
- [ ] Confirm physical desktop receives no injected events.
- [ ] Send pen events and confirm explicit rejection without side effects.
- [ ] Test disconnect/reconnect and safe failure behavior.

### Cleanup and recovery
- [ ] Stop normally with Ctrl+C.
- [ ] Stop through managed -stop.
- [ ] Interrupt startup after one or more children launch.
- [ ] Simulate a child exiting during readiness.
- [ ] Confirm all owned processes exit.
- [ ] Confirm private document-portal mount is gone.
- [ ] Confirm runtime directory is removed only after unmount.
- [ ] Confirm private PipeWire sink/monitor do not remain.
- [ ] Confirm normal desktop and user config remain usable.

## Regression checks
- [ ] Normal non-headless startup works.
- [ ] Existing pairing key remains unchanged.
- [ ] Existing configuration remains unchanged except documented compatibility mapping.
- [ ] Physical desktop audio defaults remain unchanged.
- [ ] Normal mouse/keyboard/gamepad behavior is unaffected.
- [ ] Portal virtual-display mode remains independent of private headless mode.

## CI baseline

Linux workflow run #93 passed: https://github.com/NotADeveloper4665/Syzygy/actions/runs/44304892318
This verifies the Linux build/test/package path and software-rendered headless smoke test. It does not verify proprietary NVIDIA NVENC/HEVC or physical-device touch/audio isolation.

## Results log

| Date | Commit | Environment | Test | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 2026-11-18 | e420a94eb7bd91d4e19cf90c3227660d9f00f689 | GitHub Actions Linux runner | Build, tests, RPM, software-rendered headless smoke test | PASS | Workflow run #93 |
