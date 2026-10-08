#!/usr/bin/env python3
"""Check the installed RPM assets and serve the Web UI from an unrelated cwd."""
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import time
from html.parser import HTMLParser
from urllib.request import urlopen
from urllib.parse import urljoin

assets = Path('/usr/share/syzygy')
for name in ('web/index.html', 'web/config.html', 'web/welcome.html',
             'shaders/opengl/ConvertUV.frag', 'shaders/opengl/ConvertUV.vert',
             'shaders/opengl/ConvertY.frag', 'shaders/opengl/Scene.vert',
             'shaders/opengl/Scene.frag'):
    path = assets / name
    assert path.is_file() and path.stat().st_size > 0, f'Missing or empty installed asset: {path}'

class Scripts(HTMLParser):
    def __init__(self):
        super().__init__()
        self.urls = []

    def handle_starttag(self, tag, attrs):
        if tag == 'script':
            src = dict(attrs).get('src')
            if src:
                self.urls.append(src)

with tempfile.TemporaryDirectory(prefix='syzygy-installed-check-') as directory:
    env = dict(os.environ, XDG_CONFIG_HOME=directory)
    for name in ('WAYLAND_DISPLAY', 'DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
        env.pop(name, None)
    with open(Path(directory) / 'server.log', 'w+') as log:
        process = subprocess.Popen(
            ['/usr/bin/syzygy', 'capture=x11', 'encoder=software', 'port=49189'],
            cwd=directory, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            context = ssl._create_unverified_context()
            base = 'https://127.0.0.1:49190/welcome'
            page = None
            for attempt in range(100):
                try:
                    with urlopen(base, context=context, timeout=1) as response:
                        page = response.read().decode('utf-8')
                    break
                except Exception:
                    if process.poll() is not None:
                        break
                    time.sleep(0.2)
            if not page or '<html' not in page.lower():
                log.seek(0)
                raise AssertionError('Installed Web UI did not serve HTML from a foreign cwd:\n' + log.read())
            parser = Scripts()
            parser.feed(page)
            assert parser.urls, 'Web UI contains no JavaScript assets'
            for src in parser.urls:
                with urlopen(urljoin(base, src), context=context, timeout=3) as response:
                    assert response.status == 200 and response.read(), f'Empty JavaScript asset: {src}'
            print('PASS: installed shaders, Web UI HTML, and JavaScript assets from an unrelated cwd')
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
