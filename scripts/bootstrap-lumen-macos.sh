#!/usr/bin/env bash
set -euo pipefail
# Baseline checkout only; never overwrite the Syzygy working tree.
url=https://github.com/lumaaaaaa/Lumen.git
revision=5c3bd0f4109eb4069d10ee1a8201b9bf3a328018
root="$(git rev-parse --show-toplevel)"
dest="${1:-$(dirname "$root")/lumen-macos-baseline}"
if [[ -e "$dest" ]]; then
  echo "Destination already exists: $dest" >&2
  exit 1
fi
git clone --no-checkout "$url" "$dest"
git -C "$dest" checkout --detach "$revision"
git -C "$dest" submodule update --init --recursive
actual="$(git -C "$dest" rev-parse HEAD)"
[[ "$actual" == "$revision" ]]
printf 'Pinned Lumen baseline: %s\nPath: %s\n' "$actual" "$dest"
printf 'Review install.sh before executing it; do not disable macOS security protections.\n'
