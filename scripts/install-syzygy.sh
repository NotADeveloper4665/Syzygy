#!/usr/bin/env bash
set -Eeuo pipefail

readonly REPOSITORY="https://github.com/NotADeveloper4665/Syzygy.git"
readonly INSTALL_PREFIX="${SYZYGY_INSTALL_PREFIX:-/usr/local}"
readonly INSTALL_LIBDIR="${INSTALL_PREFIX}/lib/syzygy"
readonly INSTALL_BINDIR="${INSTALL_PREFIX}/bin"

usage() {
  cat <<'HELP'
Install Syzygy from source on Linux.

Usage: install-syzygy.sh [--help]

The script installs build dependencies using apt, dnf, zypper, pacman, or apk,
builds and installs Syzygy, creates the current user's host key, and prints a
start command that enables automatic capture and encoder selection.

Environment:
  SYZYGY_INSTALL_HEADLESS  Set to 1 to install the Fedora headless desktop runtime
  SYZYGY_BRANCH          Git branch or tag to build (default: main)
  SYZYGY_INSTALL_PREFIX  Install prefix (default: /usr/local)
HELP
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
elif (($#)); then
  usage >&2
  exit 2
fi

as_root() {
  if ((EUID == 0)); then
    "$@"
  else
    if ! command -v sudo >/dev/null 2>&1; then
      printf 'This step needs root. Install sudo or rerun this script as root.\n' >&2
      return 1
    fi
    sudo "$@"
  fi
}

detect_package_manager() {
  if command -v apt-get >/dev/null 2>&1; then
    echo apt
  elif command -v dnf >/dev/null 2>&1; then
    echo dnf
  elif command -v zypper >/dev/null 2>&1; then
    echo zypper
  elif command -v pacman >/dev/null 2>&1; then
    echo pacman
  elif command -v apk >/dev/null 2>&1; then
    echo apk
  else
    printf 'No supported package manager found (supported: apt, dnf, zypper, pacman, apk). Install dependencies from docs/building.md and rerun.\n' >&2
    return 1
  fi
}

install_dependencies() {
  local manager="$1"
  case "$manager" in
    apt)
      as_root apt-get update
      as_root apt-get install -y build-essential cmake ninja-build pkg-config git curl nodejs npm \
        libcap-dev libcurl4-openssl-dev libdrm-dev libevdev-dev libgbm-dev libminiupnpc-dev \
        libglib2.0-dev libnotify-dev libnuma-dev libopus-dev libpipewire-0.3-dev libpulse-dev libssl-dev libva-dev libwayland-dev \
        libx11-dev libxcb-shm0-dev libxcb-xfixes0-dev libxcb1-dev libxfixes-dev libxrandr-dev libxtst-dev
      ;;
    dnf)
      as_root dnf install -y gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config git curl nodejs npm \
        glib2-devel pipewire-devel libcap-devel libcurl-devel libdrm-devel libevdev-devel mesa-libgbm-devel miniupnpc-devel \
        libnotify-devel numactl-devel opus-devel pulseaudio-libs-devel openssl-devel libva-devel \
        wayland-devel libX11-devel libxcb-devel libXfixes-devel libXrandr-devel libXtst-devel
      ;;
    zypper)
      as_root zypper --non-interactive install --no-recommends gcc gcc-c++ make cmake ninja pkg-config git curl nodejs npm \
        glib2-devel pipewire-devel libcap-devel libcurl-devel libdrm-devel libevdev-devel Mesa-libgbm-devel libminiupnpc-devel \
        libnotify-devel libnuma-devel libopus-devel libpulse-devel libopenssl-devel libva-devel \
        wayland-devel libX11-devel libxcb-devel libXfixes-devel libXrandr-devel libXtst-devel
      ;;
    pacman)
      as_root pacman -Sy --needed --noconfirm base-devel cmake ninja pkgconf git curl nodejs npm \
        glib2 pipewire libcap curl libdrm libevdev mesa miniupnpc libnotify numactl opus libpulse openssl libva \
        wayland libx11 libxcb libxfixes libxrandr libxtst
      ;;
    apk)
      as_root apk add --no-cache build-base cmake ninja pkgconf git curl nodejs npm linux-headers \
        glib-dev pipewire-dev libcap-dev curl-dev libdrm-dev libevdev-dev mesa-dev miniupnpc-dev libnotify-dev numactl-dev \
        opus-dev pulseaudio-dev openssl-dev libva-dev wayland-dev libx11-dev libxcb-dev libxfixes-dev \
        libxrandr-dev libxtst-dev
      ;;
  esac
}

if [[ ! -r /etc/os-release ]]; then
  printf 'Cannot identify this Linux distribution: /etc/os-release is missing.\n' >&2
  exit 1
fi

package_manager="$(detect_package_manager)"
if [[ "${SYZYGY_INSTALL_HEADLESS:-0}" == 1 && "$package_manager" != dnf ]]; then
  printf 'Automatic headless dependency installation currently supports Fedora/dnf. Install KWin, Plasma, Xwayland, PipeWire, WirePlumber, pactl and dbus-run-session on this distribution first.\n' >&2
  exit 1
fi
if [[ "${SYZYGY_INSTALL_HEADLESS:-0}" == 1 ]]; then
  as_root dnf install -y kwin-wayland plasma-workspace xorg-x11-server-Xwayland pipewire pipewire-pulseaudio wireplumber pulseaudio-utils dbus-daemon python3 mesa-dri-drivers
  as_root install -d -m 1777 /tmp/.X11-unix
  # SSH users do not receive the graphical-login ACL on GPU render nodes.
  if ((EUID != 0)) && getent group render >/dev/null && [[ " $(id -nG) " != *" render "* ]]; then
    as_root usermod -aG render "$(id -un)"
    printf 'Added your account to the render group. Reconnect your SSH session before starting Syzygy.\n'
  fi
fi
printf 'Installing Syzygy build dependencies with %s...\n' "$package_manager"
install_dependencies "$package_manager"

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/syzygy-install.XXXXXXXX")"
cleanup() { rm -rf "$work_dir"; }
trap cleanup EXIT

branch="${SYZYGY_BRANCH:-main}"
printf 'Building Syzygy from branch/tag %s...\n' "$branch"
git clone --quiet --depth 1 --branch "$branch" --recurse-submodules --shallow-submodules "$REPOSITORY" "$work_dir/source"

jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')"
[[ "$jobs" =~ ^[0-9]+$ ]] || jobs=2
((jobs > 4)) && jobs=4
((jobs < 1)) && jobs=1

cmake -S "$work_dir/source" -B "$work_dir/source/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSUNSHINE_ENABLE_CUDA=OFF -DSUNSHINE_ENABLE_TRAY=OFF -DBUILD_TESTS=OFF \
  -DSUNSHINE_ASSETS_DIR_DEF=assets
cmake --build "$work_dir/source/build" --parallel "$jobs"

if [[ ! -x "$work_dir/source/build/syzygy" || ! -d "$work_dir/source/build/assets" ]]; then
  printf 'Build completed without the expected syzygy binary or runtime assets.\n' >&2
  exit 1
fi

printf 'Installing Syzygy under %s...\n' "$INSTALL_PREFIX"
as_root install -d -m 0755 "$INSTALL_LIBDIR" "$INSTALL_BINDIR"
as_root install -m 0755 "$work_dir/source/build/syzygy" "$INSTALL_LIBDIR/syzygy"
as_root rm -rf "$INSTALL_LIBDIR/assets"
# The build tree can contain an absolute shaders symlink back into the temporary
# checkout. Dereference it so installed runtime assets survive cleanup.
as_root cp -aL "$work_dir/source/build/assets" "$INSTALL_LIBDIR/assets"

wrapper="$work_dir/syzygy-wrapper"
cat >"$wrapper" <<WRAPPER
#!/bin/sh
cd '$INSTALL_LIBDIR' || exit 1
exec '$INSTALL_LIBDIR/syzygy' "\$@"
WRAPPER
as_root install -m 0755 "$wrapper" "$INSTALL_BINDIR/syzygy"

printf '\nSyzygy is installed. Generating or retrieving this user\x27s access key...\n'
if ! key="$("$INSTALL_BINDIR/syzygy" -psk)"; then
  printf 'Could not create the host key. Run %s/bin/syzygy -psk as the account that will run the server.\n' "$INSTALL_PREFIX" >&2
  exit 1
fi
if [[ ! "$key" =~ ^[0-9a-f]{48}$ ]]; then
  printf 'Syzygy returned an unexpected key format; run %s/bin/syzygy -psk to inspect it.\n' "$INSTALL_PREFIX" >&2
  exit 1
fi

printf '\nHost access key (store it securely; it grants full host permissions):\n%s\n' "$key"
printf '\nCapture and encoder selection: automatic\n'
if [[ "${SYZYGY_INSTALL_HEADLESS:-0}" == 1 ]]; then
  printf 'To start without a graphical login run: syzygy -s -auto -headless -psk\n'
else
  printf 'To start run: syzygy -s -auto\n'
fi
printf 'Run the command as the same Linux account that owns this key.\n'
