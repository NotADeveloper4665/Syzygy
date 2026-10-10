#!/usr/bin/env bash
set -Eeuo pipefail

if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
  cat <<'HELP'
Install the newest validated Syzygy development RPM on Fedora.
Usage: update-syzygy-dev-rpm.sh
SYZYGY_INSTALL_HEADLESS=1 also installs the private Wayland desktop runtime.
Downloads public dev release assets, verifies SHA-256 and distro/architecture,
and uses dnf dependency resolution. Existing configuration and keys are retained.
HELP
  exit 0
fi
(($# == 0)) || { printf 'Unexpected arguments; use --help.\n' >&2; exit 2; }
command -v dnf >/dev/null || { printf 'This RPM updater requires Fedora and dnf.\n' >&2; exit 1; }
as_root() { if ((EUID == 0)); then "$@"; else sudo "$@"; fi; }
if ((EUID != 0)); then
  command -v sudo >/dev/null || { printf 'Install sudo first.\n' >&2; exit 1; }
fi
if ! command -v python3 >/dev/null; then as_root dnf install -y python3; fi
command -v rpm >/dev/null || { printf 'rpm is required.\n' >&2; exit 1; }
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/syzygy-dev-update.XXXXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

python3 - "$work_dir" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import sys
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

root = Path(sys.argv[1])
base = 'https://github.com/NotADeveloper4665/Syzygy/releases/download/dev/'
os_release = {}
for line in Path('/etc/os-release').read_text().splitlines():
    if '=' in line and not line.startswith('#'):
        name, value = line.split('=', 1)
        parsed = shlex.split(value)
        os_release[name] = parsed[0] if parsed else ''
if os_release.get('ID') != 'fedora':
    sys.exit('Development RPMs currently target Fedora; use the source installer on other distributions.')

def download(name, destination):
    request = Request(base + name, headers={'User-Agent': 'Syzygy-development-updater', 'Cache-Control': 'no-cache'})
    with urlopen(request, timeout=60) as response, destination.open('wb') as output:
        while chunk := response.read(1024 * 1024):
            output.write(chunk)

try:
    for attempt in range(3):
        download('dev-build.json', root / 'dev-build.json')
        manifest = json.loads((root / 'dev-build.json').read_text())
        if manifest.get('schema') != 1 or manifest.get('rpm') != 'Syzygy.rpm':
            sys.exit('Unsupported development release manifest.')
        if str(manifest.get('fedora')) != os_release.get('VERSION_ID'):
            sys.exit(f"This build targets Fedora {manifest.get('fedora')}; this system is Fedora {os_release.get('VERSION_ID')}.")
        if manifest.get('arch') != platform.machine():
            sys.exit(f"No development RPM for this architecture ({platform.machine()}).")
        expected = manifest.get('sha256', '')
        if len(expected) != 64 or any(c not in '0123456789abcdef' for c in expected):
            sys.exit('Invalid development RPM checksum in manifest.')
        download('Syzygy.rpm', root / 'Syzygy.rpm')
        with (root / 'Syzygy.rpm').open('rb') as package:
            digest = hashlib.file_digest(package, 'sha256').hexdigest()
        if digest == expected:
            print(f"Verified development build {manifest['commit']} (GitHub run {manifest['run_id']}).")
            break
        if attempt == 2:
            sys.exit('RPM checksum mismatch. Nothing was installed; retry after the release finishes updating.')
        time.sleep(1)
except HTTPError as error:
    if error.code == 404:
        sys.exit('The development RPM is not published yet. Wait for a successful main build on GitHub Actions, then retry.')
    sys.exit(f'GitHub download failed: HTTP {error.code}. Nothing was installed.')
except (OSError, ValueError, KeyError) as error:
    sys.exit(f'Development download failed: {error}. Nothing was installed.')
PY

rpm_path="$work_dir/Syzygy.rpm"
package_name="$(rpm -qp --queryformat '%{NAME}' "$rpm_path")"
[[ "$package_name" == syzygy ]] || { printf 'Unexpected RPM package name: %s\n' "$package_name" >&2; exit 1; }
new_package="$(rpm -qp --queryformat '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}' "$rpm_path")"
old_package="$(rpm -q --queryformat '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}' syzygy 2>/dev/null || true)"
if [[ "$new_package" == "$old_package" ]]; then
  # Development commits can share an RPM version; install still needs to replace it.
  as_root dnf reinstall -y "$rpm_path"
else
  as_root dnf install -y "$rpm_path"
fi
if [[ "${SYZYGY_INSTALL_HEADLESS:-0}" == 1 ]]; then
  as_root dnf install -y kwin-wayland plasma-workspace xorg-x11-server-Xwayland pipewire pipewire-pulseaudio wireplumber pulseaudio-utils dbus-daemon python3 mesa-dri-drivers
  as_root install -d -m 1777 /tmp/.X11-unix
  if ((EUID != 0)) && getent group render >/dev/null && [[ " $(id -nG) " != *" render "* ]]; then
    as_root usermod -aG render "$(id -un)"
    printf 'Reconnect SSH to apply your new render group membership.\n'
  fi
fi
printf '\nInstalled: %s\nRestart your running Syzygy process to use the new binary.\n' "$new_package"
if [[ "${SYZYGY_INSTALL_HEADLESS:-0}" == 1 ]]; then
  printf 'Start: /usr/bin/syzygy -s -auto -headless -psk\n'
else
  printf 'Start: /usr/bin/syzygy -s -auto -psk\n'
fi
if [[ "$(command -v syzygy || true)" != /usr/bin/syzygy ]]; then
  printf 'Your PATH resolves syzygy to another installation. Use /usr/bin/syzygy for this RPM.\n'
fi
