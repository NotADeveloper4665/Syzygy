# Syzygy Technical Design

Scope: implementation design for the Linux headless/touch/audio/lifecycle feature in SPEC.md.

## Architecture

### CLI and launcher
src/main.cpp parses options and dispatches headless requests to the installed syzygy-headless.py launcher. The launcher owns the private desktop process tree and starts the Syzygy binary inside its private environment.

### Session isolation
The launcher creates disposable XDG runtime/config/data/cache paths, strips inherited desktop/audio endpoint variables, and separately preserves the user's persistent Syzygy configuration directory. The private session must never attach to the caller's physical desktop.

### Desktop and capture
KWin provides the private Wayland compositor and virtual output. PipeWire/WirePlumber provide the private media session. Xwayland supports legacy applications. Capture must remain scoped to the private compositor.

### Touch
Touch mode uses KWin's private fake-input protocol for supported pointer, keyboard, touch, and hover events. Tablet mode is written only into disposable private KDE configuration. Pen is rejected because the selected protocol does not expose required pen semantics.

### Audio
The private audio stack creates/verifies syzygy_headless and syzygy_headless.monitor. Child apps inherit the private sink environment, and the server selects the same sink for capture. The physical desktop default sink is not changed.

### Mount cleanup
The document portal may mount FUSE at the private runtime's doc path. Mount presence is checked through /proc/self/mountinfo. Cleanup attempts fusermount3/fusermount normal unmount, then lazy unmount. Runtime deletion must wait until the owned mount is gone.

## Invariants

| Boundary | Invariant |
| --- | --- |
| CLI to launcher | Preserve unrelated options and arguments |
| Launcher to child | Private environment; no caller desktop endpoints |
| Input to KWin | Requests target only private Wayland socket |
| Audio setup | Both sink and monitor verified before server startup |
| Server audio | Headless capture selects private sink |
| Process cleanup | Only launcher-owned child handles are terminated |
| Mount cleanup | Only exact private runtime doc mount is unmounted |

## Failure handling

- Missing dependencies fail before process creation.
- Child exit during readiness aborts startup and triggers cleanup.
- Audio endpoint validation fails closed.
- Missing private Wayland routing never falls back to global input injection.
- Unmount failure is surfaced and must not be reported as successful cleanup.
- Cleanup must tolerate partial initialization.

## Implementation map

| Concern | Files |
| --- | --- |
| CLI parsing/dispatch | src/syzygy/cli.cpp, src/syzygy/cli.h, src/main.cpp |
| Private session | src_assets/linux/assets/scripts/syzygy-headless.py |
| Audio | src/platform/linux/audio.cpp |
| Touch/pen | src/platform/linux/input/inputtino_touch.cpp, inputtino_pen.cpp, inputtino_common.h |
| Tests | tests/test_headless_launcher.py, tests/syzygy_smoke.cpp |
| CI | .github/workflows/syzygy-linux.yml |

## Design decisions

1. Private compositor rather than host-wide uinput for touch isolation.
2. Disposable KDE config so tablet mode does not mutate user preferences.
3. Explicit sink and monitor validation to avoid silent wrong-source capture.
4. mountinfo-based FUSE detection because disconnected FUSE mounts may fail ordinary path operations.
5. Fail closed for unsupported pen input rather than leaking it to the physical desktop.

## Open questions

- Should headless audio permit a user sink override, or always pin capture to the private sink?
- Which KWin versions/protocol variants are supported by the fake-input implementation?
- Is a second compositor backend needed, and can it preserve the same isolation guarantees?
- Would native pen support require a separate protocol/backend?
