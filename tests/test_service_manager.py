import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'src_assets/linux/assets/scripts/syzygy-service.py'
spec = importlib.util.spec_from_file_location('manager', SCRIPT)
manager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manager)

class ServiceTests(unittest.TestCase):
    def test_sessions_codecs_errors_and_secret_redaction(self):
        monitor = manager.Monitor()
        monitor.consume('[Info] SYZYGY_SESSION_START 1 H.264')
        monitor.consume('[Info] SYZYGY_SESSION_START 2 HEVC')
        monitor.consume('[Info] SYZYGY_SESSION_START 2 HEVC')
        self.assertEqual(monitor.fields()['clients'], 2)
        self.assertEqual(monitor.fields()['codecs'], ['H.264', 'HEVC'])
        monitor.consume('[Info] SYZYGY_SESSION_STOP 1')
        monitor.consume('[Info] SYZYGY_SESSION_STOP 1')
        self.assertEqual(monitor.fields()['clients'], 1)
        for _ in range(30):
            monitor.consume('[Error: test] Error: broken')
        self.assertEqual(monitor.errors[0]['count'], 30)
        for i in range(20):
            monitor.consume('Error: ' + str(i))
        self.assertEqual(len(monitor.errors), 10)
        self.assertIsNone(monitor.consume('Sensitive pairing phrase: keep private'))
        self.assertIsNone(monitor.consume('acorn-birch-maple-ocean-river-velvet'))

    @unittest.skipIf(int(Path("/proc/self/stat").read_text().split()[0]) != os.getpid(),
                     "Sandbox PID namespace differs from its mounted /proc; lifecycle runs in CI")
    def test_background_lifecycle_and_single_instance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = root / 'fake-server'
            fake.write_text('#!/usr/bin/env python3\nimport time\nprint("Configuration UI available", flush=True)\nprint("SYZYGY_SESSION_START 123 AV1", flush=True)\ntime.sleep(60)\n')
            fake.chmod(0o700)
            env = dict(os.environ, XDG_STATE_HOME=directory)
            def call(command):
                return subprocess.run([sys.executable, str(SCRIPT), command, '--binary', str(fake), '--', '-s'],
                                      env=env, capture_output=True, text=True, timeout=20)
            try:
                self.assertEqual(call('start').returncode, 0)
                time.sleep(0.5)
                status = call('status')
                self.assertIn('Running', status.stdout)
                self.assertIn('Connected clients: 1', status.stdout)
                self.assertIn('AV1', status.stdout)
                self.assertIn('already running', call('start').stdout)
                self.assertEqual((root / 'syzygy/service/status.json').stat().st_mode & 0o777, 0o600)
            finally:
                call('stop')
            self.assertIn('Stopped', call('status').stdout)
            state = json.loads((root / 'syzygy/service/status.json').read_text())
            self.assertEqual(state['clients'], 0)

if __name__ == '__main__':
    unittest.main()
