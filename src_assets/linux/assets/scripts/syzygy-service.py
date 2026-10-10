#!/usr/bin/env python3
"""Own a detached Syzygy process tree and bounded, private diagnostics."""
import argparse
import fcntl
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import threading


def state_directory():
    path = Path(os.environ.get('XDG_STATE_HOME', str(Path.home() / '.local/state'))) / 'syzygy/service'
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    if path.is_symlink() or path.stat().st_uid != os.getuid():
        raise RuntimeError('Unsafe service state directory')
    path.chmod(0o700)
    return path


def read_state(path):
    try:
        return json.loads((path / 'status.json').read_text())
    except (OSError, ValueError):
        return {}


def identity(pid):
    try:
        return Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[19]
    except OSError:
        return None


def alive(state):
    return bool(state.get('pid') and state.get('identity') and identity(state['pid']) == state['identity'])


def save(path, state):
    temporary = path / 'status.tmp'
    temporary.write_text(json.dumps(state))
    temporary.replace(path / 'status.json')


class Monitor:
    def __init__(self):
        self.sessions = {}
        self.errors = []
        self.ready = False
        self.encoder = 'Not initialized'
        self.hide_phrase = False

    def consume(self, line):
        # Startup phrase output must never enter diagnostics or status.
        if 'Sensitive host' in line or 'Sensitive pairing' in line:
            self.hide_phrase = True
            return None
        if self.hide_phrase:
            self.hide_phrase = False
            if not line.startswith('['):
                return None
        if re.search(r'\b[a-z]+(?:[- ][a-z]+){5}\b', line.strip()) and not line.startswith('['):
            return None
        event = re.search(r'SYZYGY_SESSION_(START|STOP) (\d+)(?: (H\.264|HEVC|AV1))?', line)
        if event:
            kind, key, codec = event.groups()
            if kind == 'START':
                self.sessions[key] = codec
            else:
                self.sessions.pop(key, None)
        if 'Configuration UI available' in line:
            self.ready = True
        if 'Creating encoder [' in line:
            self.encoder = line.split('Creating encoder [', 1)[1].split(']', 1)[0]
        if re.search(r'\b(?:Error|Fatal):', line) or 'Syzygy headless:' in line:
            message = re.sub(r'^\[[^]]+\]:?\s*', '', line).strip()[:1000]
            now = time.strftime('%Y-%m-%d %H:%M:%S')
            if self.errors and self.errors[-1]['message'] == message:
                self.errors[-1]['count'] += 1
                self.errors[-1]['last_seen'] = now
            else:
                self.errors.append({'message': message, 'count': 1, 'last_seen': now})
                self.errors = self.errors[-10:]
        return line

    def fields(self):
        return {'ready': self.ready, 'clients': len(self.sessions),
                'codecs': sorted(set(self.sessions.values())), 'encoder': self.encoder,
                'errors': self.errors}


def supervise(path, binary, arguments):
    lock = (path / 'service.lock').open('a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return 0
    state = {'pid': os.getpid(), 'identity': identity(os.getpid()), 'started': time.strftime('%Y-%m-%d %H:%M:%S')}
    monitor = Monitor()
    save(path, dict(state, **monitor.fields()))
    logger = logging.getLogger('syzygy-service')
    logger.setLevel(logging.INFO)
    logger.addHandler(RotatingFileHandler(path / 'service.log', maxBytes=2 * 1024 * 1024, backupCount=3))
    env = dict(os.environ, SYZYGY_MANAGED_CHILD='1')
    # -psk is an explicit foreground disclosure operation; never collect a phrase.
    args = [arg for arg in arguments if arg not in ('-psk', '-status', '-stop') and not arg.startswith('min_log_level=')]
    args.append('min_log_level=info')
    process = subprocess.Popen([binary, *args], env=env, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
    def force_stop():
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    def stop(signum, frame):
        try:
            os.killpg(process.pid, signal.SIGTERM)
            timer = threading.Timer(8, force_stop)
            timer.daemon = True
            timer.start()
        except ProcessLookupError:
            pass
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    updated_at = 0.0
    try:
        while True:
            raw = process.stdout.readline(8192)
            if not raw:
                break
            line = raw.decode('utf-8', errors='replace').rstrip()
            safe = monitor.consume(line)
            if safe is not None:
                logger.info(safe)
            if time.monotonic() - updated_at >= 0.25 or "SYZYGY_SESSION_" in line:
                save(path, dict(state, **monitor.fields()))
                updated_at = time.monotonic()
        code = process.wait()
    finally:
        stop(None, None)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        # Also terminate orphaned desktop/audio processes in our owned group.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        save(path, {**state, **monitor.fields(), "exited": True, "exit_code": process.returncode, "clients": 0, "codecs": []})
    return code


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['start', 'run', 'status', 'stop'])
    parser.add_argument('--binary', required=True)
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    # REMAINDER requires options before the positional command.
    argv = sys.argv[1:]
    command = argv.pop(0)
    boundary = argv.index('--') if '--' in argv else len(argv)
    args = parser.parse_args([*argv[:boundary], command, *argv[boundary:]])
    arguments = args.arguments[1:] if args.arguments[:1] == ['--'] else args.arguments
    path = state_directory()
    state = read_state(path)
    running = alive(state) and not state.get('exited')
    if args.command == 'status':
        print('Syzygy: ' + ('Running' if running and state.get('ready') else 'Starting' if running else 'Stopped'))
        print('Connected clients: ' + str(state.get('clients', 0) if running else 0))
        print('Current codec: ' + (', '.join(state.get('codecs', [])) if running and state.get('codecs') else 'Idle'))
        print('Encoder: ' + (state.get('encoder', 'Not initialized') if running else 'Inactive'))
        if state.get('exit_code') is not None:
            print('Last exit code: ' + str(state['exit_code']))
        print('Recent errors:')
        for error in state.get('errors', []):
            print(f"  {error['last_seen']} ({error['count']}x): {error['message']}")
        if not state.get('errors'):
            print('  None recorded')
        print('Log: ' + str(path / 'service.log'))
        return 0
    if args.command == 'stop':
        if running:
            os.kill(state['pid'], signal.SIGTERM)
            for _ in range(100):
                if not alive(state):
                    break
                time.sleep(0.1)
            print('Syzygy: stopping' if alive(state) else 'Syzygy: stopped')
        else:
            print('Syzygy: already stopped')
        return 0
    if args.command == 'run':
        return supervise(path, args.binary, arguments)
    if running:
        print('Syzygy is already running. Use syzygy -status.')
        return 0
    log = (path / 'manager.log').open('w')
    process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), 'run', '--binary', args.binary,
                               '--', *arguments], stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                               start_new_session=True)
    for _ in range(50):
        current = read_state(path)
        if current.get('pid') == process.pid:
            if current.get('exited'):
                print('Syzygy failed to start. Use syzygy -status.', file=sys.stderr)
                return 1
            print('Syzygy started in the background. Use syzygy -status or syzygy -stop.')
            if '-psk' in arguments:
                print('Show your pairing phrase privately with: syzygy -psk')
            return 0
        if process.poll() is not None:
            print('Background startup failed: ' + (path / 'manager.log').read_text()[-2000:], file=sys.stderr)
            return 1
        time.sleep(0.1)
    print('Startup is still pending. Use syzygy -status.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Syzygy service: {error}', file=sys.stderr)
        sys.exit(2)
