import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, patch

SCRIPT = Path(__file__).resolve().parents[1] / 'src_assets/linux/assets/scripts/syzygy-headless.py'
spec = importlib.util.spec_from_file_location('headless', SCRIPT)
headless = importlib.util.module_from_spec(spec)
spec.loader.exec_module(headless)


class LauncherTests(unittest.TestCase):
    def test_physical_desktop_and_audio_are_not_inherited(self):
        source = {'DISPLAY': ':0', 'WAYLAND_DISPLAY': 'wayland-0',
                  'DBUS_SESSION_BUS_ADDRESS': 'physical', 'PULSE_SERVER': 'physical',
                  'PIPEWIRE_REMOTE': 'physical', 'HOME': '/home/test'}
        env = headless.private_environment(source, Path('/tmp/private'), Path('/usr/bin/syzygy'))
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'PULSE_SERVER', 'PIPEWIRE_REMOTE'):
            self.assertNotIn(key, env)
            self.assertIn(key, source)
        self.assertEqual(env['XDG_RUNTIME_DIR'], '/tmp/private')
        self.assertEqual(env['HOME'], '/home/test')

    def test_size_validation(self):
        self.assertEqual(headless.size('1920x1080'), (1920, 1080))
        for value in ('0x0', '9999x1080', '1920x1080;id', '1920x-1'):
            with self.assertRaises(ValueError):
                headless.size(value)

    def test_permission_is_private_and_bound_to_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            headless.install_permission(root, Path('/usr/bin/syzygy'))
            text = (root / 'data/applications/syzygy-headless.desktop').read_text()
            self.assertIn('Exec="/usr/bin/syzygy"', text)
            self.assertIn('zkde_screencast_unstable_v1;org_kde_kwin_fake_input;', text)
            with self.assertRaises(ValueError):
                headless.install_permission(root, Path('/tmp/evil\nExec=other'))

    def test_cleanup_only_owns_started_children(self):
        child = MagicMock()
        child.poll.return_value = None
        with patch.object(headless.subprocess, 'Popen', return_value=child):
            children = headless.Children()
            children.start(['private-child'], {})
            children.close()
        child.terminate.assert_called_once()
        child.wait.assert_called_once()

    def test_dependency_failure_precedes_session_creation(self):
        with patch.object(headless.os, 'geteuid', return_value=1000), \
             patch.object(headless.shutil, 'which', return_value=None), \
             patch.object(headless.sys, 'argv', ['launcher', '--binary', '/bin/true']), \
             patch.object(headless.subprocess, 'Popen') as popen:
            with self.assertRaisesRegex(RuntimeError, 'Missing headless programs'):
                headless.main()
            popen.assert_not_called()


if __name__ == '__main__':
    unittest.main()
