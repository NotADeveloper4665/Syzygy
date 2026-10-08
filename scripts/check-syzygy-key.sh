#!/usr/bin/env bash
set -euo pipefail

export XDG_CONFIG_HOME="$(mktemp -d)"
trap 'rm -rf -- "$XDG_CONFIG_HOME"' EXIT

./syzygy --help | grep -F -- '-psk'
./syzygy -psk > "$XDG_CONFIG_HOME/key-first" 2> "$XDG_CONFIG_HOME/diagnostics"
./syzygy -psk > "$XDG_CONFIG_HOME/key-second" 2>> "$XDG_CONFIG_HOME/diagnostics"
python3 - <<'PY'
import os, pathlib, re, stat
root = pathlib.Path(os.environ['XDG_CONFIG_HOME'])
first = (root / 'key-first').read_bytes()
assert re.fullmatch(rb'[0-9a-f]{48}\n', first), 'stdout must contain exactly one key'
assert first == (root / 'key-second').read_bytes(), 'key must persist'
assert first.strip() not in (root / 'diagnostics').read_bytes(), 'diagnostics must not reveal the key'
assert stat.S_IMODE((root / 'sunshine/syzygy/syzygy.psk').stat().st_mode) == 0o600
print('PASS: standalone key output, persistence, and redaction')
PY
