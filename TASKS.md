# Syzygy Implementation Tasks

Specification: SPEC.md  
Design: DESIGN.md  
Verification: TESTING.md  
Tracking PR: https://github.com/NotADeveloper4665/Syzygy/pull/3

Legend: [x] automated implementation/evidence exists; [~] partially complete or awaiting host verification; [ ] outstanding.

## Requirements and design
- [x] Define feature scope, non-goals, requirements, and acceptance criteria.
- [x] Document architecture and isolation invariants.
- [x] Map requirements to implementation and test files.
- [ ] Review spec/design against target T1600 runtime.

## Private session lifecycle
- [x] Validate display dimensions.
- [x] Isolate XDG desktop environment and preserve persistent Syzygy config.
- [x] Check dependencies before spawning children.
- [x] Supervise owned processes and readiness.
- [x] Detect private document-portal mount through mountinfo.
- [x] Try normal and lazy FUSE unmount.
- [~] Verify partial-startup and interrupted-shutdown cleanup on Fedora host.

## Audio
- [x] Create/verify private Syzygy sink and monitor.
- [x] Export private sink to child applications.
- [x] Select private sink for headless server capture.
- [x] Preserve legacy Sunshine sink preference compatibility.
- [~] Verify real application playback and streamed audio on T1600.
- [ ] Verify repeated start/stop leaves no stale PipeWire nodes/processes.

## Touch and input isolation
- [x] Route supported touch/pointer/keyboard/hover through private KWin fake-input.
- [x] Keep touch mode opt-in.
- [x] Apply tablet mode only in private config.
- [x] Reject unsupported pen events explicitly.
- [x] Fail closed when private Wayland route is unavailable.
- [~] Verify real client touch on T1600.
- [ ] Verify physical desktop receives no injected events.
- [ ] Add protocol-level malformed/disconnect tests if existing coverage is insufficient.

## Packaging and CI
- [x] Add FUSE runtime dependency.
- [x] Build Linux package in CI.
- [x] Run headless KWin/PipeWire capture smoke test with virtual DRM/Mesa.
- [x] Replace prohibited render-node chmod with group-based access in CI.
- [x] Confirm successful Linux CI retry (workflow run #93).
- [~] Install RPM on Fedora 44 x86_64 T1600 and validate installed layout/permissions.
- [ ] Validate proprietary NVIDIA NVENC H.264 and HEVC.
- [ ] Validate RPM upgrade preserves config and pairing credentials.

## Release readiness
- [ ] Close all spec-to-code and test gaps.
- [ ] Complete target-hardware acceptance matrix.
- [ ] Update user documentation from verified behavior only.
- [ ] Run full CI on final commit.
- [ ] Review security boundaries and cleanup.
- [ ] Obtain explicit approval before marking PR ready or merging.

## Immediate next actions
1. Install the successful CI RPM on the T1600.
2. Record selected capture backend, encoder, codec, and relevant logs for a normal headless stream.
3. Verify application audio is captured while physical desktop audio defaults remain unchanged.
4. Verify touch routing and physical-desktop isolation from a compatible client.
5. Exercise Ctrl+C, managed stop, and forced child termination; inspect owned processes, mounts, and PipeWire nodes.
6. Turn every observed failure into a focused requirement/test before changing implementation.
