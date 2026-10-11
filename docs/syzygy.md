# Syzygy development status

Syzygy is a streaming host configured through the CLI. Upstream source history, license notices, and pinned dependencies are retained in the repository.

## First milestone: Linux CLI and key storage

```sh
./syzygy -s -auto
```

`-s` starts the host. On Linux, normal startup (`syzygy` or `syzygy -s`) automatically loads or generates a persistent pairing phrase and enables PIN-free pairing. Interactive terminal startup displays it; unattended startup keeps it out of file/journal logs. In Eclipse, add the host by IP or discover it, click the host, choose **Syzygy pairing phrase**, and paste the displayed key. No PIN or web-page approval is needed. Multiple clients can use the same passkey, and pairing survives restarts. `-auto` clears saved encoder and capture overrides while retaining explicit command-line selections, so the host probes the available backends on this machine; combine it with `-nvec` (or `-nvenc`), `-vaapi`, or `-software` to auto-detect capture while forcing an encoder. This is useful when moving configuration between systems or recovering from an invalid saved option such as `capture=kwin`. Without `-auto`, saved capture configuration is respected. `-h264` disables HEVC and AV1 advertisement. The legacy `-p` still toggles UPnP.

`-psk` loads or generates a persistent six-word randomly generated phrase and prints only that key to stdout. With `-s`, startup logs go to stderr; without `-s`, the command exits after printing. Handle this output as a secret. It is not placed in normal logs.

The key is stored under the host's application data directory, normally `~/.config/sunshine/syzygy/syzygy.phrase` or `$XDG_CONFIG_HOME/sunshine/syzygy/syzygy.phrase` on Linux. The inherited `sunshine` directory name is kept for compatibility with existing installations. The directory must be owned by the current user with no group/other access, and the key file must have mode `0600`. Unsafe symlinks, nonregular files, insecure permissions, and malformed keys are rejected. Creation publishes a fully written file atomically without overwriting a concurrent creator's key.

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

The client decodes `authmessage`, computes `HMAC-SHA256(scrypt-derived-key-as-48-ASCII-hex-bytes, authmessage)`, and signs those same bytes with the private key corresponding to `clientcert` using SHA-256. It sends a second request with the same identity and certificate, `syzygyphase=response`, `syzygyproof` as 64 lowercase hex characters, and `clientsignature` as hex-encoded signature bytes. On success, the host returns `serverproof`, which is HMAC-SHA256 over the ASCII bytes `Syzygy server confirmation v1`, the exact decoded `authmessage`, and the binary SHA-256 fingerprint of the returned server certificate. Eclipse verifies this before trusting the host certificate. A challenge is accepted once, within two minutes, and only for the exact client ID and certificate used to request it. `/pair` query parameters are excluded from debug request logs.

Successful enrollment saves the client certificate and grants the full permission set to the key holder, including input, file, clipboard, server-command, and streaming permissions. The matching Eclipse client implements this extension; its pairing dialog accepts the passkey. This is a high-entropy generated-key protocol, not a password protocol; a user-chosen connection password needs a reviewed PAKE design and a separate client flow. Key rotation and device revocation are still pending. The web interface remains available until its administration functions have CLI equivalents.

Windows keeps the inherited executable and service setup. `-s` continues to start the host with PIN pairing there, while `-psk` and strong-key enrollment remain unavailable until owner-only ACL handling is implemented. The existing Windows SudoVDA integration remains available; Linux portal virtual monitors are available as described below.

## Linux virtual monitors (Wayland portal)

```sh
syzygy -s -auto -virtual -psk
```

Run from the logged-in user's Wayland desktop terminal. `-virtual` selects
`capture=portal` and `portal_virtual_display=enabled`. The portal must advertise
VIRTUAL support in `AvailableSourceTypes`; unsupported compositors fail with a
clear message. Approve the desktop screen-sharing dialog to create its virtual
monitor. Syzygy verifies the returned source is virtual and streams its PipeWire
node. It does not fall back to capturing a physical screen in this mode.

The monitor and portal session are reused during encoder probing and subsequent
streams, and closed when Syzygy exits. Virtual mode does not reuse or overwrite
physical-screen restore tokens. The compositor selects the virtual monitor's
resolution; the standard portal does not provide an arbitrary resolution/refresh
request. Per-client resizing, creation/removal per streaming session, and a
completely logged-out headless compositor are not implemented. This option is
independent of the client's existing Windows SudoVDA checkbox.

A virtual monitor does not create a desktop login session. `WAYLAND_DISPLAY`,
`XDG_RUNTIME_DIR`, the user session bus, PipeWire, and a supporting desktop portal
must be available. Start in KDE Konsole rather than an environment without the
desktop session. No KMS capability grant is needed for portal capture.

For a physical screen, omit `-virtual` and use `capture=portal`. `-auto` now
preserves that explicit capture argument instead of clearing it. Input continues
through the existing Linux input backend and requires its normal uinput access.

Reference: https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html

## Build and validation

GitHub Actions builds the complete Linux host and compatibility web assets, tests the CLI/key module, and checks the actual binary's standalone key output. The Fedora RPM installs runtime assets at `/usr/share/syzygy`, so the host can run from any working directory. Installed-binary checks exercise the web assets and actual passkey enrollment, including wrong-key rejection, consumed challenges, multiple clients, host confirmation, authenticated TLS, pairing persistence, and unattended log redaction. Runtime GPU/capture support still requires the appropriate host drivers and libraries.

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
| Virtual displays | `src/process.cpp`, `src/platform/windows/virtual_display.*` | Verify existing SudoVDA lifecycle on Windows and test Linux portal virtual monitors and add per-client resizing/headless backends |
| Kyber | Separate experimental transport boundary | Pin and audit the actual Kyber SDK/mux, license, authentication, and client compatibility before media integration |
| Packaging | `cmake/packaging/`, `packaging/` | Rename and verify service/install paths, then add Fedora RPM builds |

Kyber refers to the QUIC interactive-streaming protocol, not the post-quantum cipher. Adding QUIC alone does not make a Moonlight client compatible with Kyber. No fixed latency promise is made.

See the README for phrase entropy, scrypt parameters, key migration, and the Linux `-tails` trust policy. The persistent phrase flow uses the signed/HMAC challenge protocol, not PAKE.


### Linux background operation

`syzygy -s` now starts a detached, per-user process manager and returns to the
terminal. Existing options, including `-auto`, `-headless`, and `-tails`, are
forwarded to the server. Closing the terminal does not stop its process tree.

```sh
syzygy -s -auto -headless -tails
syzygy -status
syzygy -stop
syzygy -psk
```

Status shows running/starting/stopped state, active streaming clients, their
negotiated codecs (idle when no stream exists), the last selected encoder,
recent errors with repeat counts, and the log path. Codec negotiation events
are separate from startup encoder probes. An active client is a streaming
session, not a saved paired device. Diagnostics belong to the latest managed
run; unmanaged foreground servers are not included.

Logs and status are private under `${XDG_STATE_HOME:-~/.local/state}/syzygy/service`.
Server logs rotate at 2 MiB with three backups. At most ten recent error entries
are retained. The background manager never requests or logs the pairing phrase;
`-psk` on a background start directs you to run `syzygy -psk` separately.
Repeated starts do not launch another managed server. `-stop` terminates its
owned desktop/audio process group. Use `-foreground` with `-s` for interactive
debugging or when an external systemd service already manages Syzygy.

This is terminal-independent operation, not boot activation. On servers whose
login policy kills all user processes at logout, configure a lingering user
service separately. The managed start acknowledgement means the launcher
started; use `-status` to inspect readiness and errors.

### Experimental authenticated QUIC (Linux IPv4)

Start `syzygy -s -auto -headless -tails -quic` to enable the SQ1 QUIC listener
on UDP `base port + 31` (default 48020). Install `python3-aioquic` on the host
and desktop client; RPMs require it. Allow that UDP port through your firewall
or tailnet ACL as needed. Existing GameStream remains available.

Eclipse's Network transport preference offers Automatic (prefer QUIC), Force
QUIC (fail instead of falling back), and GameStream. Pair through the existing
six-word or Tailscale flow first. Transport authentication pins the saved host
certificate and signs a fresh host challenge using the already-paired client
key. No pairing phrase is sent over this transport. The host rejects unknown,
expired, and revoked certificates and checks streaming permission.

One QUIC connection forwards pinned HTTPS launch and RTSP over separate reliable
streams, ENet control packets over a reliable stream, and existing video/audio
UDP packets over QUIC DATAGRAM. Fragment reassembly is bounded and expires after
100 ms. This preserves existing media codecs, encryption and input permissions.
The loopback relay is limited to HTTPS, RTSP and the three media/control ports;
it is not a general-purpose proxy. HTTP launch occurs through the same private
loopback identity used by the RTSP and UDP relays. Python process shutdown owns
all sockets and helpers.

This is a custom QUIC transport, not Kyber wire compatibility. It is currently
an experimental Fedora/Linux IPv4 implementation; Flatpak/Windows deployment
and Android are not included. CPU overhead and real-game latency must be measured
before claiming an improvement. Automatic falls back only when QUIC is
unavailable; certificate/authentication failures stop the connection.
