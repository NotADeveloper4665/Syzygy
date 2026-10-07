# Syzygy

Syzygy is an Apollo-derived streaming host being developed for CLI setup, protected host credentials, and headless operation alongside the Eclipse client.

## Current development milestone

- Linux CLI shortcuts: `-s`, `-nvec`/`-nvenc`, `-vaapi`, `-software`, `-h264`, and `-psk`.
- Persistent cryptographically random host key with private file permissions and atomic creation.
- Linux source installer for apt, dnf, zypper, pacman, and apk systems. It installs build dependencies, builds the server, detects an encoder, and prints the host key and start command.
- Automated Linux build and tests for key storage, concurrency, unsafe files, argument handling, and stdout redaction.

```sh
./syzygy -s -nvec -h264 -psk
```

**This is an early development build.** Syzygy's host-key challenge-response pairing endpoint is intended for the matching Eclipse client build. Existing Apollo PIN pairing and streaming behavior remain available. Password-based pairing, key rotation and device revocation, Kyber transport, and additional virtual-display controls remain future work.

See [CLI usage, current limits, and the source map](docs/syzygy.md). Development build artifacts appear under [GitHub Actions](https://github.com/NotADeveloper4665/Syzygy/actions).

### Install from source on Linux

Install the Syzygy server, build dependencies, detect an encoder, and generate your host key with this one-line command:

```sh
curl -fsSL https://raw.githubusercontent.com/NotADeveloper4665/Syzygy/main/scripts/install-syzygy.sh | bash
```

The installer supports apt, dnf, zypper, pacman, and apk. It requests sudo only when it needs to install system packages or write under `/usr/local`; the server build and host-key creation run as your user. It builds the latest `main` branch by default, leaves the server stopped, and prints your host key plus a recommended command such as `syzygy -s -psk -nvenc`. The key grants full host permissions; share it only with trusted clients. Set `SYZYGY_BRANCH` to choose a branch or tag. On distributions with another package manager, install the dependencies in [docs/building.md](docs/building.md) and build manually.

## Upstream and license

Based on [ClassicOldSong/Apollo](https://github.com/ClassicOldSong/Apollo) commit `adc5c5a0bd80831ce495434bb16aee2cd4175fb8`, which derives from Sunshine. The upstream source snapshot, notices, and dependency pins are retained. See the preserved [Apollo README](README.upstream.md) for its platform support and existing Windows SudoVDA integration.

Distributed under [GPLv3](LICENSE), with upstream copyright notices and dependency licenses retained.
