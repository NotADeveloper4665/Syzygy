# Syzygy development status

Syzygy is an Apollo-derived host intended to be configured through the CLI. The imported base is `ClassicOldSong/Apollo` commit `adc5c5a0bd80831ce495434bb16aee2cd4175fb8`. Its source snapshot, license, notices, and pinned submodules are retained. The local checkout retains upstream history; the GitHub repository starts from this documented source import.

## First milestone: Linux CLI and key storage

```sh
./syzygy -s -nvec -h264 -psk
```

`-s` starts the host, `-nvec` (or `-nvenc`) selects NVIDIA NVENC, and `-h264` disables HEVC and AV1 advertisement. `-vaapi` and `-software` select other encoder paths. Select only one encoder shortcut. Existing Apollo `name=value` configuration remains available; explicit shortcuts take precedence over those values. The legacy `-p` still toggles UPnP.

`-psk` loads or generates a persistent 48-character random hex key and prints only that key to stdout. With `-s`, startup logs go to stderr; without `-s`, the command exits after printing. Handle this output as a secret. It is not placed in normal logs.

The key is stored at `<Apollo application data>/syzygy/syzygy.psk`. Linux retains Apollo's `sunshine` application-data namespace for compatibility: normally `~/.config/sunshine/syzygy/syzygy.psk`, or `$XDG_CONFIG_HOME/sunshine/syzygy/syzygy.psk`. The directory must be owned by the current user with no group/other access, and the key file must have mode `0600`. Unsafe symlinks, nonregular files, insecure permissions, and malformed keys are rejected. Creation publishes a fully written file atomically without overwriting a concurrent creator's key.

### Strong-key client pairing protocol

The host enables a separate `GET /pair` extension when started with `-s`. Existing PIN pairing is unchanged. A compatible client first requests `syzygyphase=challenge` with its `uniqueid`, `devicename`, and `clientcert` (the certificate bytes hex-encoded as in the GameStream API). The response supplies a 32-byte random `challenge`, the exact `authmessage` bytes in hex, and the host `plaincert`.

The client decodes `authmessage`, computes `HMAC-SHA256(key-as-48-ASCII-hex-bytes, authmessage)`, and signs those same bytes with the private key corresponding to `clientcert` using SHA-256. It sends a second request with the same identity and certificate, `syzygyphase=response`, `syzygyproof` as 64 lowercase hex characters, and `clientsignature` as hex-encoded signature bytes. A proof is accepted once, within two minutes, and only for the exact client ID and certificate used to request the challenge. `/pair` query parameters are excluded from debug request logs.

Successful enrollment saves the client certificate and grants standard view/list permissions; it does not grant input, file, clipboard, server-command, or full administrative permissions. The client must implement this extension before it can pair without the existing PIN. This is a high-entropy generated-key protocol, not a password protocol; a user-chosen connection password needs a reviewed PAKE design and a separate client flow. Key rotation and device revocation are still pending. The web interface remains available until its administration functions have CLI equivalents.

Windows keeps the upstream executable and service setup. `-s` continues to start the host with PIN pairing there, while `-psk` and strong-key enrollment remain unavailable until owner-only ACL handling is implemented. Apollo's Windows SudoVDA integration remains in the imported baseline; Linux virtual-display support is not implemented.

## Build and validation

GitHub Actions builds the complete Linux host and compatibility web assets, tests the CLI/key module, and checks the actual binary's standalone key output. The artifact is a development archive, not an RPM or a production release. Extract it and run the binary from its archive directory so the relative `assets` path resolves. Runtime GPU/capture support still requires the appropriate host drivers and libraries.

Local focused check:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc src/syzygy/cli.cpp src/syzygy/key_store.cpp \
  src/syzygy/pairing_auth.cpp tests/syzygy_smoke.cpp -lcrypto -pthread -o /tmp/syzygy-smoke
/tmp/syzygy-smoke
```

## Source map and next milestones

| Subsystem | Existing extension point | Next work |
| --- | --- | --- |
| CLI/configuration | `src/main.cpp`, `src/config.cpp`, `src/syzygy/` | Key status/rotation and device revocation commands |
| Pairing/authorization | `src/nvhttp.cpp`, `src/crypto.cpp`, `src/syzygy/pairing_auth.*` | Eclipse client integration, failed-attempt limits, key rotation, and device revocation |
| Password enrollment | New PAKE boundary alongside existing pairing | Evaluate an audited OPAQUE implementation and add a compatible Eclipse client flow |
| Virtual displays | `src/process.cpp`, `src/platform/windows/virtual_display.*` | Verify existing SudoVDA lifecycle on Windows and evaluate Linux headless backends separately |
| Kyber | Separate experimental transport boundary | Pin and audit the actual Kyber SDK/mux, license, authentication, and client compatibility before media integration |
| Packaging | `cmake/packaging/`, `packaging/` | Rename and verify service/install paths, then add Fedora RPM builds |

Kyber refers to the QUIC interactive-streaming protocol, not the post-quantum cipher. Adding QUIC alone does not make a Moonlight client compatible with Kyber. No fixed latency promise is made.
