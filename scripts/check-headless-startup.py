#!/usr/bin/env python3
"""Check the installed server's CLI, capture/encoder probe and Web UI with no login."""
import os
from pathlib import Path
import signal
import ssl
import subprocess
import tempfile
import time
from urllib.request import urlopen

with tempfile.TemporaryDirectory(prefix='syzygy-headless-installed-') as directory:
    env = dict(os.environ, XDG_CONFIG_HOME=directory, SYZYGY_HEADLESS_SIZE='640x480')
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'XDG_RUNTIME_DIR'):
        env.pop(key, None)
    log_path = Path(directory) / 'server.log'
    with log_path.open('w') as log:
        process = subprocess.Popen(['/usr/bin/syzygy', '-s', '-auto', '-headless',
                                    '-software', '-h264', 'port=49189'],
                                   cwd=directory, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            deadline = time.monotonic() + 60
            success = False
            while time.monotonic() < deadline and process.poll() is None:
                text = log_path.read_text(errors='replace')
                if 'Found H.264 encoder: libx264' in text:
                    try:
                        with urlopen('https://127.0.0.1:49190/welcome',
                                     context=ssl._create_unverified_context(), timeout=1) as response:
                            success = '<html' in response.read().decode().lower()
                    except Exception:
                        pass
                    if success:
                        break
                time.sleep(0.2)
            assert success, 'Installed headless server did not initialize capture/encoding and Web UI:\n' + log_path.read_text(errors='replace')
            print('PASS: installed -headless CLI, native capture/CPU encoder probe, and Web UI without a desktop session')
        finally:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
