# Syzygy Feature Specification

Status: implementation in progress; host-hardware acceptance pending.
Scope: Linux private headless desktop, touch routing, audio isolation, and lifecycle cleanup.
Tracking PR: https://github.com/NotADeveloper4665/Syzygy/pull/3

## Problem

Syzygy must stream from Linux without a logged-in graphical desktop or physical monitor. Its private desktop must isolate configuration, input, and audio from the user's normal desktop and must clean up only resources it owns.

## Goals

1. Start a private Wayland/KWin desktop as a regular user.
2. Capture its virtual output without physical-screen or portal fallback.
3. Route supported touch input only through the private compositor.
4. Route child application audio to a Syzygy-owned sink and capture its monitor.
5. Clean up child processes and private FUSE mounts after normal, partial, or interrupted startup.
6. Preserve persistent Syzygy configuration and pairing credentials.

## Non-goals

- Native pen pressure, tilt, eraser, or tool semantics through KWin fake-input.
- Modifying the user's persistent KDE configuration or physical desktop audio defaults.
- Arbitrary active-stream display resizing.
- Claiming hardware encoding based on software-rendered CI.
- Changing pairing or QUIC protocols.

## Functional requirements

### Session isolation
- Create a private runtime directory with restrictive permissions.
- Use disposable XDG config/data/cache directories for KDE.
- Preserve the user's persistent Syzygy configuration/key path.
- Remove inherited display, D-Bus, PipeWire, PulseAudio, and KWin device variables that could attach to the physical desktop.
- Fail before spawning children when required dependencies are missing.
- Never silently fall back to physical desktop capture.

### Desktop and lifecycle
- Supervise private D-Bus, KWin, PipeWire/WirePlumber, and required Plasma components.
- Validate initial display dimensions and reject malformed/out-of-range values.
- Track and terminate only launcher-owned children.
- Handle partial startup and repeated shutdown safely.
- Detect the private document-portal FUSE mount through mount metadata, including disconnected mounts.
- Attempt normal unmount, then lazy unmount; report failure and do not pretend cleanup succeeded.

### Touch
- Touch mode is opt-in and routes supported pointer, keyboard, touch, and hover events through private KWin fake-input.
- Input identifier ranges must not collide.
- Tablet mode may be forced only in disposable private KDE configuration.
- Standard headless mode must not alter tablet mode.
- Unsupported pen events must be rejected; no global uinput or physical-desktop fallback is permitted.
- Missing private Wayland access must fail closed.

### Audio
- Verify both the syzygy_headless sink and syzygy_headless.monitor source.
- Fail clearly if either endpoint is unavailable.
- Child applications and Plasma activation inherit the private audio environment.
- Syzygy selects the private sink in headless mode.
- Legacy Sunshine sink preferences remain compatible.
- The physical desktop's default sink must remain unchanged.

## Quality and security requirements

- Least-privilege permissions for private runtime/config state.
- No broad process-name cleanup.
- No input path may escape the private compositor boundary.
- Diagnostics identify failures without exposing pairing secrets.
- Automated tests must be deterministic and independent of the developer's active desktop.

## Acceptance criteria

| ID | Criterion | Evidence |
| --- | --- | --- |
| AC-01 | Missing dependencies fail before child creation | Unit test |
| AC-02 | Private environment does not inherit physical desktop endpoints | Unit test |
| AC-03 | Persistent Syzygy config/key survives disposable KDE session | Unit test |
| AC-04 | Invalid display dimensions are rejected | Unit test |
| AC-05 | Sink and monitor are both verified | Unit + host test |
| AC-06 | Child apps inherit private audio sink | Unit + host test |
| AC-07 | Standard mode leaves tablet config untouched | Unit test |
| AC-08 | Touch mode changes only private tablet config | Unit + host test |
| AC-09 | Touch/pointer/keyboard reach private desktop only | Host test |
| AC-10 | Pen is explicitly rejected without leakage | Unit + host test |
| AC-11 | Healthy, stale, absent, and unavailable-tool FUSE cases handled | Unit test |
| AC-12 | Partial startup/shutdown leaves no owned processes or mounts | Unit + host test |
| AC-13 | CI builds package and runs headless capture smoke test | CI |
| AC-14 | Fedora RPM runs on target T1600/GTX 1060 | Hardware test |
| AC-15 | NVIDIA NVENC H.264/HEVC, audio, and touch work on target | Hardware test |

## Known limitations

Native pen semantics are unsupported by the selected KWin fake-input interface. The current CI smoke test uses virtual DRM and Mesa software rendering; it does not validate proprietary NVIDIA encoding. Real hardware behavior remains unverified until target-host acceptance is recorded.

## Change control

Changes to requirements, security boundaries, CLI behavior, or acceptance criteria must update this file in the same change set. Document deviations and their test impact. Code written is not equivalent to a requirement passing.
