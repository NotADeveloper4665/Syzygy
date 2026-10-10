import importlib.util
import os
import subprocess
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, patch

SCRIPT = Path(__file__).resolve().parents[1] / 'src_assets/linux/assets/scripts/syzygy-headless.py'
spec = importlib.util.spec_from_file_location('headless', SCRIPT)
headless = importlib.util.module_from_spec(spec)
spec.loader.exec_module(headless)


class LauncherTests(unittest.TestCase):
    def test_display_environment_reaches_dbus_before_desktop_starts(self):
        with tempfile.TemporaryDirectory() as directory:
            events = []
            env = {'XDG_RUNTIME_DIR': directory, 'WAYLAND_DISPLAY': 'wayland-syzygy'}
            child = MagicMock()
            child.wait.return_value = 0
            def run(command, **kwargs):
                events.append(command)
                self.assertEqual(kwargs['env'], env)
            def start(command, **kwargs):
                events.append(command)
                return child
            with patch.dict(headless.os.environ, env, clear=True), \
                 patch.object(headless.subprocess, 'run', side_effect=run), \
                 patch.object(headless.subprocess, 'Popen', side_effect=start):
                self.assertEqual(headless.desktop(Path('/usr/bin/syzygy'), ['-s']), 0)
            self.assertEqual(events[0][0], 'dbus-update-activation-environment')
            self.assertIn('WAYLAND_DISPLAY', events[0])
            self.assertNotIn('--systemd', events[0])
            self.assertEqual(events[1], ['plasmashell'])

    def test_activation_failure_does_not_start_an_unusable_desktop(self):
        with patch.object(headless.subprocess, 'run',
                          side_effect=subprocess.CalledProcessError(1, 'dbus-update-activation-environment')), \
             patch.object(headless.subprocess, 'Popen') as popen:
            with self.assertRaises(subprocess.CalledProcessError):
                headless.desktop(Path('/usr/bin/syzygy'), [])
            popen.assert_not_called()

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
        self.assertEqual(env['KWIN_COMPOSE'], 'O2')
        self.assertEqual(env['XDG_CONFIG_HOME'], '/tmp/private/config')
        self.assertEqual(env['CONFIGURATION_DIRECTORY'], '/home/test/.config')

    def test_persistent_host_configuration_precedence(self):
        for source, expected in (({'HOME': '/home/test', 'XDG_CONFIG_HOME': '/persist'}, '/persist'),
                                 ({'XDG_CONFIG_HOME': '/persist', 'CONFIGURATION_DIRECTORY': '/service'}, '/service')):
            env = headless.private_environment(source, Path('/tmp/private'), Path('/usr/bin/syzygy'))
            self.assertEqual(env['CONFIGURATION_DIRECTORY'], expected)
            self.assertEqual(env['SUNSHINE_MIGRATE_CONFIG'], '0')

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
            self.assertIn('zkde_screencast_unstable_v1,org_kde_kwin_fake_input', text)
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
