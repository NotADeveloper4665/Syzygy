# Syzygy development status

Syzygy is an Apollo-derived host intended to be configured through the CLI. The imported base is `ClassicOldSong/Apollo` commit `adc5c5a0bd80831ce495434bb16aee2cd4175fb8`. Its source snapshot, license, notices, and pinned submodules are retained. The local checkout retains upstream history; the GitHub repository starts from this documented source import.

## First milestone: Linux CLI and key storage

```sh
./syzygy -s -nvec -h264 -psk
```

`-s` starts the host, `-nvec` (or `-nvenc`) selects NVIDIA NVENC, and `-h264` disables HEVC and AV1 advertisement. `-vaapi` and `-software` select other encoder paths. Select only one encoder shortcut. Existing Apollo `name=value` configuration remains available; explicit shortcuts take precedence over those values. The legacy `-p` still toggles UPnP.

`-psk` loads or generates a persistent 48-character random hex key and prints only that key to stdout. With `-s`, startup logs go to stderr; without `-s`, the command exits after printing. Handle this output as a secret. It is not placed in normal logs.

The key is stored at `<Apollo application data>/syzygy/syzygy.psk`. Linux retains Apollo's `sunshine` application-data namespace for compatibility: normally `~/.config/sunshine/syzygy/syzygy.psk`, or `$XDG_CONFIG_HOME/sunshine/syzygy/syzygy.psk`. The directory must be owned by the current user with no group/other access, and the key file must have mode `0600`. Unsafe symlinks, nonregular files, insecure permissions, and malformed keys are rejected. Creation publishes a fully written file atomically without overwriting a concurrent creator's key.

**Key enrollment is not connected to pairing in this milestone.** The key cannot yet replace the existing PIN pairing flow. Rotation, device revocation, full-permission key enrollment, and password-based PIN-less enrollment still need implementation and client integration. The web interface remains available until its administration functions have CLI equivalents.

Windows keeps the upstream executable and service setup. Syzygy key commands fail explicitly there until owner-only ACL handling is implemented and verified. Apollo's Windows SudoVDA integration remains in the imported baseline; Linux virtual-display support is not implemented.

## Build and validation

GitHub Actions builds the complete Linux host and compatibility web assets, tests the CLI/key module, and checks the actual binary's standalone key output. The artifact is a development archive, not an RPM or a production release. Extract it and run the binary from its archive directory so the relative `assets` path resolves. Runtime GPU/capture support still requires the appropriate host drivers and libraries.

Local focused check:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc src/syzygy/cli.cpp src/syzygy/key_store.cpp \
  tests/syzygy_smoke.cpp -lcrypto -pthread -o /tmp/syzygy-smoke
/tmp/syzygy-smoke
```

## Source map and next milestones

| Subsystem | Existing extension point | Next work |
| --- | --- | --- |
| CLI/configuration | `src/main.cpp`, `src/config.cpp`, `src/syzygy/` | Key status/rotation and device revocation commands |
| Pairing/authorization | `src/nvhttp.cpp`, `src/crypto.cpp` | Strict authentication flow, failed-attempt limits, and full streaming permissions after proof of the key |
| Password enrollment | New PAKE boundary alongside existing pairing | Evaluate an audited OPAQUE implementation and add a compatible Eclipse client flow |
| Virtual displays | `src/process.cpp`, `src/platform/windows/virtual_display.*` | Verify existing SudoVDA lifecycle on Windows and evaluate Linux headless backends separately |
| Kyber | Separate experimental transport boundary | Pin and audit the actual Kyber SDK/mux, license, authentication, and client compatibility before media integration |
| Packaging | `cmake/packaging/`, `packaging/` | Rename and verify service/install paths, then add Fedora RPM builds |

Kyber refers to the QUIC interactive-streaming protocol, not the post-quantum cipher. Adding QUIC alone does not make a Moonlight client compatible with Kyber. No fixed latency promise is made.
