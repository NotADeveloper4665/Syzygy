# Syzygy

Syzygy is a self-hosted streaming host for compatible Moonlight clients, with CLI setup, protected host credentials, and headless operation.

## Current development milestone

- Linux CLI shortcuts: `-s`, `-auto`, `-nvec`/`-nvenc`, `-vaapi`, `-software`, `-h264`, and `-psk`.
- PIN-free Eclipse pairing with a persistent passkey shown on interactive Linux startup. Anyone holding it can pair with full host permissions; paired devices reconnect without entering it again.
- Cryptographically random passkey with private file permissions and atomic creation.
- Linux source installer for apt, dnf, zypper, pacman, and apk systems. It installs build dependencies, builds the server, and prints the host key and automatic-mode start command.
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

The installer supports apt, dnf, zypper, pacman, and apk. It requests sudo only when it needs to install system packages or write under `/usr/local`; the server build and host-key creation run as your user. It builds the latest `main` branch by default, leaves the server stopped, and prints your host key plus `syzygy -s -auto`. Automatic mode clears saved encoder and capture overrides, then probes the available host backends at startup. The key grants full host permissions; share it only with trusted clients. Set `SYZYGY_BRANCH` to choose a branch or tag. On distributions with another package manager, install the dependencies in [docs/building.md](docs/building.md) and build manually.

## Upstream and license

Syzygy retains upstream source history, license notices, and dependency pins. See the preserved [upstream README](README.upstream.md) for original platform notes and Windows SudoVDA details.

Distributed under [GPLv3](LICENSE), with upstream copyright notices and dependency licenses retained.
