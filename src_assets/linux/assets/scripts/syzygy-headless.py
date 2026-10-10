#!/usr/bin/env python3
"""Own a private KWin/PipeWire desktop without a graphical login or portal prompts."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

DEPENDENCIES = ('dbus-run-session', 'kwin_wayland', 'plasmashell', 'Xwayland',
                'pipewire', 'wireplumber', 'pipewire-pulse', 'pactl', 'kbuildsycoca6')


def size(value):
    match = re.fullmatch(r'(\d{3,4})x(\d{3,4})', value)
    if not match or not all(320 <= int(n) <= 7680 for n in match.groups()):
        raise ValueError('SYZYGY_HEADLESS_SIZE must be WIDTHxHEIGHT, each from 320 to 7680')
    return tuple(int(n) for n in match.groups())


def private_environment(source, runtime, binary):
    env = dict(source)
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS',
                'DBUS_SESSION_BUS_PID', 'PULSE_SERVER', 'PULSE_RUNTIME_PATH',
                'PIPEWIRE_REMOTE', 'PIPEWIRE_RUNTIME_DIR', 'KWIN_DRM_DEVICES'):
        env.pop(key, None)
    env.update(XDG_RUNTIME_DIR=str(runtime), XDG_DATA_HOME=str(runtime / 'data'),
               XDG_CACHE_HOME=str(runtime / 'cache'),
               XDG_SESSION_TYPE='wayland', XDG_CURRENT_DESKTOP='KDE',
               SYZYGY_HEADLESS_SESSION='1', SYZYGY_HEADLESS_BINARY=str(binary),
               QT_QPA_PLATFORM='wayland', KWIN_COMPOSE='O2',
               QT_FORCE_STDERR_LOGGING='1', QT_LOGGING_TO_CONSOLE='1')
    return env


class Children:
    def __init__(self):
        self.children = []

    def start(self, command, env):
        child = subprocess.Popen(command, env=env)
        self.children.append(child)
        return child

    def check(self):
        for child in self.children:
            if child.poll() is not None:
                raise RuntimeError(f'{Path(child.args[0]).name} exited ({child.returncode})')

    def close(self):
        for child in reversed(self.children):
            if child.poll() is None:
                child.terminate()
        deadline = time.monotonic() + 5
        for child in reversed(self.children):
            try:
                child.wait(timeout=max(0.1, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()


def wait_for(predicate, children, description, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        children.check()
        if predicate():
            return
        time.sleep(0.1)
    raise RuntimeError(f'Timed out waiting for {description}')


def install_permission(runtime, binary):
    apps = runtime / 'data/applications'
    apps.mkdir(parents=True, mode=0o700, exist_ok=True)
    # KWin checks the executable and desktop-file protocol allowlist. Permissions
    # are scoped to this private session; no global authorization bypass is used.
    if any(c in str(binary) for c in '\n\r%'):
        raise ValueError('Unsupported binary path for the KWin desktop entry')
    quoted = '"' + str(binary).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$') + '"'
    (apps / 'syzygy-headless.desktop').write_text(
        '[Desktop Entry]\nType=Application\nName=Syzygy headless host\n'
        f'Exec={quoted}\nNoDisplay=true\n'
        'X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1,org_kde_kwin_fake_input\n')


def desktop(binary, arguments):
    children = Children()
    try:
        env = dict(os.environ)
        children.start(['plasmashell'], env)
        # Pass original shortcut semantics to Syzygy, with the private backend
        # taking precedence over saved portal/KMS settings.
        server = children.start([str(binary), *arguments, 'capture=kwin',
                                 'portal_virtual_display=disabled'], env)
        status = server.wait()
        (Path(env['XDG_RUNTIME_DIR']) / 'server-exit-status').write_text(str(status))
        return status
    finally:
        children.close()


def session(binary, arguments):
    children = Children()
    env = dict(os.environ)
    runtime = Path(env['XDG_RUNTIME_DIR'])
    width, height = size(env.get('SYZYGY_HEADLESS_SIZE', '1920x1080'))
    try:
        # Build the permission registry before the compositor or any client connects.
        # Keep it private so an existing user's KDE cache cannot hide this entry.
        subprocess.run(['kbuildsycoca6', '--noincremental'], env=env, check=True, timeout=30)
        children.start(['pipewire'], env)
        wait_for(lambda: (runtime / 'pipewire-0').exists(), children, 'PipeWire')
        children.start(['wireplumber'], env)
        children.start(['pipewire-pulse'], env)
        wait_for(lambda: (runtime / 'pulse/native').exists(), children, 'audio server')
        # A null sink gives applications a working default audio output with no
        # physical audio device. Syzygy captures its monitor through PulseAudio.
        for command in (['pactl', 'load-module', 'module-null-sink',
                         'sink_name=syzygy_headless', 'sink_properties=device.description=Syzygy'],
                        ['pactl', 'set-default-sink', 'syzygy_headless']):
            subprocess.run(command, env=env, check=True, timeout=20, stdout=subprocess.DEVNULL)
        env['SYZYGY_HEADLESS_ARGUMENTS'] = json.dumps(arguments)
        wrapper = runtime / 'desktop-session'
        wrapper.write_text('#!/bin/sh\nexec ' + shlex.join(
            [sys.executable, str(Path(__file__).resolve()), '--desktop', '--binary', str(binary)]) + '\n')
        wrapper.chmod(0o700)
        compositor = children.start(['kwin_wayland', '--virtual', '--width', str(width),
            '--height', str(height), '--output-count', '1', '--socket', 'wayland-syzygy',
            '--xwayland', '--no-lockscreen', '--no-global-shortcuts',
            '--exit-with-session', str(wrapper)], env)
        print(f'Syzygy: starting private headless desktop {width}x{height}', file=sys.stderr, flush=True)
        result = compositor.wait()
        status_file = runtime / 'server-exit-status'
        if status_file.exists():
            return int(status_file.read_text())
        return result if result else 2
    finally:
        children.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--session', action='store_true')
    parser.add_argument('--desktop', action='store_true')
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    arguments = args.arguments[1:] if args.arguments[:1] == ['--'] else args.arguments
    binary = args.binary.resolve(strict=True)
    if args.desktop:
        return desktop(binary, json.loads(os.environ['SYZYGY_HEADLESS_ARGUMENTS']))
    if args.session:
        return session(binary, arguments)
    if os.geteuid() == 0:
        raise RuntimeError('Run headless Syzygy as your regular Linux user, not root')
    size(os.environ.get('SYZYGY_HEADLESS_SIZE', '1920x1080'))
    missing = [name for name in DEPENDENCIES if not shutil.which(name)]
    if missing:
        raise RuntimeError('Missing headless programs: ' + ', '.join(missing) +
            '. Fedora: sudo dnf install kwin-wayland plasma-workspace xorg-x11-server-Xwayland '
            'pipewire pipewire-pulseaudio wireplumber pulseaudio-utils dbus-daemon python3')
    with tempfile.TemporaryDirectory(prefix=f'syzygy-headless-{os.getuid()}-') as directory:
        runtime = Path(directory)
        runtime.chmod(0o700)
        env = private_environment(os.environ, runtime, binary)
        install_permission(runtime, binary)
        command = ['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()),
                   '--session', '--binary', str(binary), '--', *arguments]
        process = subprocess.Popen(command, env=env, start_new_session=True)
        try:
            return process.wait()
        finally:
            # Own exactly this new process group, never another user's session.
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=5)
                # Grandchildren can outlive dbus-run-session; remove the group.
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()


if __name__ == '__main__':
    def stop(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'Syzygy headless: {error}', file=sys.stderr)
        sys.exit(2)
