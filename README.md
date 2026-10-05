# Syzygy

Syzygy is an Apollo-derived streaming host being developed for CLI setup, protected host credentials, and headless operation alongside the Eclipse client.

## Current development milestone

- Linux CLI shortcuts: `-s`, `-nvec`/`-nvenc`, `-vaapi`, `-software`, `-h264`, and `-psk`.
- Persistent cryptographically random host key with private file permissions and atomic creation.
- Automated Linux build and tests for key storage, concurrency, unsafe files, argument handling, and stdout redaction.

```sh
./syzygy -s -nvec -h264 -psk
```

**This is an initial development build. Host-key enrollment is not yet connected to pairing.** PIN-less password pairing, device revocation, Kyber transport, and additional virtual-display controls are upcoming work. Existing Apollo pairing and streaming behavior remain available.

See [CLI usage, current limits, and the source map](docs/syzygy.md). Development build artifacts appear under [GitHub Actions](https://github.com/NotADeveloper4665/Syzygy/actions).

## Upstream and license

Based on [ClassicOldSong/Apollo](https://github.com/ClassicOldSong/Apollo) commit `adc5c5a0bd80831ce495434bb16aee2cd4175fb8`, which derives from Sunshine. The upstream source snapshot, notices, and dependency pins are retained. See the preserved [Apollo README](README.upstream.md) for its platform support and existing Windows SudoVDA integration.

Distributed under [GPLv3](LICENSE), with upstream copyright notices and dependency licenses retained.
