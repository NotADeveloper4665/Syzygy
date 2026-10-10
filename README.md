# Syzygy

Syzygy is a self-hosted streaming host for compatible Moonlight clients, with CLI setup, protected host credentials, and headless operation.

## Current development milestone

- Linux CLI shortcuts: `-s`, `-auto`, `-nvec`/`-nvenc`, `-vaapi`, `-software`, `-h264`, and `-psk`.
- PIN-free Eclipse pairing with a persistent passkey shown on interactive Linux startup. Anyone holding it can pair with full host permissions; paired devices reconnect without entering it again.
- Cryptographically random passkey with private file permissions and atomic creation.
- Linux source installer for apt, dnf, zypper, pacman, and apk systems. It installs build dependencies, builds the server, and prints the host key and automatic-mode start command.
- Managed headless Wayland desktop: `syzygy -s -auto -headless -psk` (no graphical login required; install the headless runtime below).
- Opt-in Linux Wayland portal virtual monitor: `syzygy -s -auto -virtual -psk`. Requires a logged-in desktop and portal advertising virtual-monitor support; client-driven resizing is pending.
- Automated Linux build and tests for key storage, concurrency, unsafe files, argument handling, and stdout redaction.

```sh
./syzygy -s -auto
```

**This is an early development build.** Syzygy's host-key challenge-response pairing endpoint is intended for the matching Eclipse client build. Standard PIN pairing and streaming remain available. Password-based pairing, key rotation and device revocation, Kyber transport, and additional virtual-display controls remain future work.

See [CLI usage, current limits, and the source map](docs/syzygy.md). Development build artifacts appear under [GitHub Actions](https://github.com/NotADeveloper4665/Syzygy/actions).

### Install from source on Linux

Install the Syzygy server and build dependencies, generate your host key, and get an automatic-mode start command with this one-line command:

```sh
curl -fsSL https://raw.githubusercontent.com/NotADeveloper4665/Syzygy/main/scripts/install-syzygy.sh | bash
```

The installer supports apt, dnf, zypper, pacman, and apk. It requests sudo only when it needs to install system packages or write under `/usr/local`; the server build and host-key creation run as your user. It builds the latest `main` branch by default, leaves the server stopped, and prints your host key plus `syzygy -s -auto`. Automatic mode clears saved encoder and capture overrides, retains explicit command-line selections, then probes the available host backends at startup. The key grants full host permissions; share it only with trusted clients. Set `SYZYGY_BRANCH` to choose a branch or tag. On distributions with another package manager, install the dependencies in [docs/building.md](docs/building.md) and build manually.

### Update development RPMs on Fedora

Successful `main` builds publish a validated RPM to the rolling
[development release](https://github.com/NotADeveloper4665/Syzygy/releases/tag/dev).
Update without compiling or signing in to GitHub:

```bash
curl -fsSL https://raw.githubusercontent.com/NotADeveloper4665/Syzygy/main/scripts/update-syzygy-dev-rpm.sh | bash
```

For a headless server, also install the private Wayland runtime:

```bash
curl -fsSL https://raw.githubusercontent.com/NotADeveloper4665/Syzygy/main/scripts/update-syzygy-dev-rpm.sh | SYZYGY_INSTALL_HEADLESS=1 bash
```

The updater verifies the build checksum and Fedora version/CPU architecture,
then installs with `dnf` dependency resolution. It reinstalls builds that share
an RPM version and preserves configuration and pairing keys. Current RPMs target
Fedora 44 x86_64. Reconnect SSH if render-group membership changed, and restart
Syzygy using `/usr/bin/syzygy` to select the RPM instead of an older source install.
The script leaves running servers alone and does not create or print a new key.
The manifest and release notes link the exact RPM source commit; the `dev` tag
anchors the download channel and its generated source archives remain fixed.

## Upstream and license

Syzygy retains upstream source history, license notices, and dependency pins. See the preserved [upstream README](README.upstream.md) for original platform notes and Windows SudoVDA details.

Distributed under [GPLv3](LICENSE), with upstream copyright notices and dependency licenses retained.

### Headless Linux server (development)

`-headless` starts a private KWin Wayland desktop, virtual output, D-Bus session,
PipeWire audio/video server and Plasma shell. It works from SSH without an existing
graphical login, connected monitor, portal approval dialog or KMS capture capability.
Keyboard and mouse input use the private compositor’s native input protocol.
The server keeps its regular user’s configuration and pairing key.

Fedora installation with the additional runtime dependencies:

```bash
curl -fsSL https://raw.githubusercontent.com/NotADeveloper4665/Syzygy/main/scripts/install-syzygy.sh | SYZYGY_INSTALL_HEADLESS=1 bash
```

Start as a regular user:

```bash
syzygy -s -auto -headless -psk
```

The default virtual desktop is 1920x1080. Set its initial size before starting:

```bash
SYZYGY_HEADLESS_SIZE=2560x1440 syzygy -s -auto -headless -psk
```

Stop with Ctrl+C. Syzygy cleans up only the processes and runtime directory it
created. Applications launched through this host inherit the private desktop and
audio output. Xwayland supports applications that use X11 inside the Wayland desktop.
KWin requires access to a working DRM render device, even with no monitor attached.
The Fedora headless installer adds the current user to `render` when needed;
reconnect SSH after installation to apply new group membership.
KDE settings are temporary; Syzygy configuration and pairing keys remain persistent.
Hardware encoding requires working vendor drivers;
`-software` can select CPU encoding. This mode creates a separate desktop rather
than attaching to another logged-in user’s desktop. Changing its display size during
a stream and native touch/pen routing are not implemented yet. Gamepads require the
normal uinput permissions. Automatic headless dependency installation currently
supports Fedora; other distributions need the listed runtime programs installed.

GitHub CI builds the RPM and runs a real displayless KWin-to-PipeWire frame test
with a virtual vgem DRM device and Mesa software rendering. Hardware-specific NVIDIA/Intel/AMD validation remains
necessary before treating headless mode as production-ready.

## Word pairing and automatic Tailscale permissions

`syzygy -psk` prints the persistent six-word pairing phrase. Enter the host address
and phrase in the updated Eclipse or Occultation client. Case is ignored; spaces
and hyphens are interchangeable. The old hexadecimal enrollment key is retired
on first phrase creation; previously paired device certificates keep working.

To authorize devices in this host's own tailnet without entering a phrase:

```sh
/usr/bin/syzygy -s -auto -headless -tails -psk
```

Alternatively enable **Network → Automatically authorize same-tailnet devices**
in the Syzygy Web UI and restart the host. This option is disabled by default.
Linux requires the root-owned `/run/tailscale/tailscaled.sock` LocalAPI and a
running Tailscale tunnel. The socket's daemon-reported source identity, local
Tailscale destination address, current network map and tailnet DNS suffix must
match. Shared-in/shared-to nodes, expired nodes, LAN connections, forwarded
identity headers, userspace tunnels and unavailable identity checks are rejected.
A successful automatic enrollment grants full permissions and saves the client
certificate. Disabling the option prevents new automatic enrollment; revoke
already paired devices separately if their access should also be removed.

Generated phrases select six independent words with rejection sampling from a
7,772-entry list, providing approximately 77.5 bits of entropy. scrypt derives the
wire credential (`N=32768`, `r=8`, `p=1`, 24 output bytes); this does not increase
the phrase's entropy. The signed challenge and authenticated server confirmation
remain in use. This is not OPAQUE/PAKE: a captured phrase-pairing transcript can
be used to test guesses offline with the scrypt cost. Use the generated phrase,
not a manually chosen sentence. Challenge creation is limited to 20 per source
and 200 globally per minute; failed proofs consume their challenge.

Word list adapted from [EFF's Large Wordlist](https://www.eff.org/files/2016/07/18/eff_large_wordlist.txt),
licensed [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).
Four internally hyphenated entries are omitted to make hyphens unambiguous separators.
