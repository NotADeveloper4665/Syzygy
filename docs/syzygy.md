# Syzygy development status

Syzygy is a streaming host configured through the CLI. Upstream source history, license notices, and pinned dependencies are retained in the repository.

## First milestone: Linux CLI and key storage

```sh
./syzygy -s -auto
```

`-s` starts the host. `-auto` clears saved encoder and capture overrides so the host probes the available backends on this machine; it is useful when moving the configuration between systems or recovering from an invalid saved option such as `capture=kwin`. Without `-auto`, saved configuration is respected. For a manual encoder override, use `-nvec` (or `-nvenc`), `-vaapi`, or `-software`; select only one. `-h264` disables HEVC and AV1 advertisement. The legacy `-p` still toggles UPnP.

`-psk` loads or generates a persistent 48-character random hex key and prints only that key to stdout. With `-s`, startup logs go to stderr; without `-s`, the command exits after printing. Handle this output as a secret. It is not placed in normal logs.

The key is stored under the host's application data directory, normally `~/.config/sunshine/syzygy/syzygy.psk` or `$XDG_CONFIG_HOME/sunshine/syzygy/syzygy.psk` on Linux. The inherited `sunshine` directory name is kept for compatibility with existing installations. The directory must be owned by the current user with no group/other access, and the key file must have mode `0600`. Unsafe symlinks, nonregular files, insecure permissions, and malformed keys are rejected. Creation publishes a fully written file atomically without overwriting a concurrent creator's key.

## Linux source installer

The repository includes `scripts/install-syzygy.sh` for Debian/Ubuntu (apt), Fedora (dnf), openSUSE (zypper), Arch (pacman), and Alpine (apk) systems. Run the reviewed script as a regular user; it uses sudo only when installing packages and writing under `/usr/local`.

```sh
curl -fsSLO https://raw.githubusercontent.com/NotADeveloper4665/Syzygy/main/scripts/install-syzygy.sh
less install-syzygy.sh
bash install-syzygy.sh
```

It builds the selected `main` branch into a temporary directory and installs the executable and runtime assets. It generates or retrieves the key for the account running the script and prints `syzygy -s -auto` as the start command. The server is not started automatically. Automatic mode clears saved encoder and capture overrides, then lets Syzygy probe the available backends at startup. Use `SYZYGY_BRANCH` to select a different branch or tag and `SYZYGY_INSTALL_PREFIX` to change the `/usr/local` install prefix. Systems using other package managers can follow [the manual build instructions](building.md).

### Strong-key client pairing protocol

The host enables a separate `GET /pair` extension when started with `-s`. Existing PIN pairing is unchanged. A compatible client first requests `syzygyphase=challenge` with its `uniqueid`, `devicename`, and `clientcert` (the certificate bytes hex-encoded as in the GameStream API). The response supplies a 32-byte random `challenge`, the exact `authmessage` bytes in hex, and the host `plaincert`.

The client decodes `authmessage`, computes `HMAC-SHA256(key-as-48-ASCII-hex-bytes, authmessage)`, and signs those same bytes with the private key corresponding to `clientcert` using SHA-256. It sends a second request with the same identity and certificate, `syzygyphase=response`, `syzygyproof` as 64 lowercase hex characters, and `clientsignature` as hex-encoded signature bytes. On success, the host returns `serverproof`, which is HMAC-SHA256 over the ASCII bytes `Syzygy server confirmation v1`, the exact decoded `authmessage`, and the binary SHA-256 fingerprint of the returned server certificate. Eclipse verifies this before trusting the host certificate. A challenge is accepted once, within two minutes, and only for the exact client ID and certificate used to request it. `/pair` query parameters are excluded from debug request logs.

Successful enrollment saves the client certificate and grants the full permission set to the key holder, including input, file, clipboard, server-command, and streaming permissions. The client must implement this extension before it can pair without the existing PIN. This is a high-entropy generated-key protocol, not a password protocol; a user-chosen connection password needs a reviewed PAKE design and a separate client flow. Key rotation and device revocation are still pending. The web interface remains available until its administration functions have CLI equivalents.

Windows keeps the inherited executable and service setup. `-s` continues to start the host with PIN pairing there, while `-psk` and strong-key enrollment remain unavailable until owner-only ACL handling is implemented. The existing Windows SudoVDA integration remains available; Linux virtual-display support is not implemented.

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
| Pairing/authorization | `src/nvhttp.cpp`, `src/crypto.cpp`, `src/syzygy/pairing_auth.*` | Failed-attempt limits, key rotation, and device revocation |
| Password enrollment | New PAKE boundary alongside existing pairing | Evaluate an audited OPAQUE implementation and add a compatible Eclipse client flow |
| Virtual displays | `src/process.cpp`, `src/platform/windows/virtual_display.*` | Verify existing SudoVDA lifecycle on Windows and evaluate Linux headless backends separately |
| Kyber | Separate experimental transport boundary | Pin and audit the actual Kyber SDK/mux, license, authentication, and client compatibility before media integration |
| Packaging | `cmake/packaging/`, `packaging/` | Rename and verify service/install paths, then add Fedora RPM builds |

Kyber refers to the QUIC interactive-streaming protocol, not the post-quantum cipher. Adding QUIC alone does not make a Moonlight client compatible with Kyber. No fixed latency promise is made.
