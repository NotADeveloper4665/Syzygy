"""Exercise downloads and dnf selection without network or privileged installs."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/update-syzygy-dev-rpm.sh'


class DevUpdaterTests(unittest.TestCase):
    def run_updater(self, installed=True, corrupt=False, fedora='44', arch='x86_64', same_binary=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / 'sitecustomize.py'
            fixture.write_text('''import hashlib, io, json, os, pathlib, platform, urllib.request
original = pathlib.Path.read_text
def read_text(self, *args, **kwargs):
    if str(self) == '/etc/os-release':
        return 'ID=fedora\\nVERSION_ID=' + os.environ['TEST_FEDORA'] + '\\n'
    return original(self, *args, **kwargs)
pathlib.Path.read_text = read_text
platform.machine = lambda: os.environ['TEST_ARCH']
payload = b'controlled-test-rpm'
manifest = {'schema': 1, 'rpm': 'Syzygy.rpm', 'fedora': 44, 'arch': 'x86_64',
            'commit': 'test-commit', 'run_id': '123', 'sha256': hashlib.sha256(payload).hexdigest()}
def urlopen(request, **kwargs):
    if request.full_url.endswith('dev-build.json'):
        return io.BytesIO(json.dumps(manifest).encode())
    return io.BytesIO(b'corrupt' if os.environ['TEST_CORRUPT'] == '1' else payload)
urllib.request.urlopen = urlopen
''')
            for name, source in {
                'dnf': '#!/bin/bash\nprintf "%s\\n" "$*" >> "$TEST_DNF_LOG"\n',
                'sudo': '#!/bin/bash\nexec "$@"\n',
                'rpm': '''#!/bin/bash
if [[ "$1" == -qp && "$3" == '%{NAME}' ]]; then
  printf syzygy
elif [[ "$1" == -qp ]]; then
  printf syzygy-0.0.7-1.x86_64
elif [[ "$TEST_INSTALLED" == 1 ]]; then
  printf syzygy-0.0.7-1.x86_64
else
  exit 1
fi
''',
            }.items():
                path = root / name
                path.write_text(source)
                path.chmod(0o755)
            if same_binary:
                # A different PATH spelling/symlink can still select the RPM.
                for name, source in {
                    'syzygy': '#!/bin/bash\nexit 0\n',
                    'readlink': '#!/bin/bash\nprintf /usr/bin/syzygy\n',
                }.items():
                    path = root / name
                    path.write_text(source)
                    path.chmod(0o755)
            log = root / 'dnf.log'
            env = dict(os.environ, PATH=str(root) + ':' + os.environ['PATH'],
                       PYTHONPATH=str(root), TEST_DNF_LOG=str(log),
                       TEST_INSTALLED=str(int(installed)), TEST_CORRUPT=str(int(corrupt)),
                       TEST_FEDORA=fedora, TEST_ARCH=arch, SYZYGY_INSTALL_HEADLESS='0')
            result = subprocess.run(['bash', str(SCRIPT)], env=env, text=True,
                                    capture_output=True, timeout=15)
            return result, log.read_text() if log.exists() else ''

    def test_same_version_development_build_is_reinstalled(self):
        result, operations = self.run_updater()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(operations.startswith('reinstall -y '), operations)
        self.assertIn('/usr/bin/syzygy', result.stdout)

    def test_first_install_uses_dependency_resolution(self):
        result, operations = self.run_updater(installed=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(operations.startswith('install -y '), operations)

    def test_equivalent_binary_path_does_not_warn_or_repair(self):
        result, _ = self.run_updater(same_binary=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('takes precedence', result.stdout)
        self.assertNotIn('Previous installation:', result.stdout)

    def test_corrupt_download_never_installs(self):
        result, operations = self.run_updater(corrupt=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checksum mismatch', result.stderr)
        self.assertEqual(operations, '')

    def test_incompatible_distro_or_architecture_never_installs(self):
        for kwargs in ({'fedora': '43'}, {'arch': 'aarch64'}):
            with self.subTest(**kwargs):
                result, operations = self.run_updater(**kwargs)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(operations, '')


class PathRepairTests(unittest.TestCase):
    def repair(self, root, owned=False, fail_link=False):
        # Run the updater's actual helper on temporary files, without installing.
        helper = SCRIPT.read_text().split('# BEGIN PATH REPAIR\n', 1)[1].split('# END PATH REPAIR', 1)[0]
        tools = root / 'tools'
        tools.mkdir(exist_ok=True)
        rpm = tools / 'rpm'
        rpm.write_text('#!/bin/bash\nexit ' + ('0' if owned else '1') + '\n')
        rpm.chmod(0o755)
        if fail_link:
            link = tools / 'ln'
            link.write_text('#!/bin/bash\nexit 1\n')
            link.chmod(0o755)
        env = dict(os.environ, PATH=str(tools) + ':' + os.environ['PATH'])
        return subprocess.run(['bash', '-c', 'set -Eeuo pipefail\nas_root() { "$@"; }\n' + helper +
                               '\nrepair_local_syzygy "$1" "$2"', 'path-repair', str(root / 'syzygy'), str(root / 'rpm-syzygy')],
                              env=env, capture_output=True, text=True, timeout=5)

    def fixture(self, root):
        (root / 'syzygy').write_text('previous installation')
        target = root / 'rpm-syzygy'
        target.write_text('#!/bin/sh\nexit 0\n')
        target.chmod(0o755)

    def test_old_installation_is_preserved_and_rpm_is_selected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            result = self.repair(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / 'syzygy').resolve(), root / 'rpm-syzygy')
            backups = list(root.glob('syzygy.pre-rpm.*'))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_text(), 'previous installation')
            self.assertEqual(self.repair(root).returncode, 0)
            self.assertEqual(len(list(root.glob('syzygy.pre-rpm.*'))), 1)

    def test_package_owned_installation_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            self.assertNotEqual(self.repair(root, owned=True).returncode, 0)
            self.assertEqual((root / 'syzygy').read_text(), 'previous installation')
            self.assertEqual(list(root.glob('syzygy.pre-rpm.*')), [])

    def test_failed_link_creation_restores_previous_installation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            self.assertNotEqual(self.repair(root, fail_link=True).returncode, 0)
            self.assertFalse((root / 'syzygy').is_symlink())
            self.assertEqual((root / 'syzygy').read_text(), 'previous installation')

    def test_old_symlink_is_backed_up_as_a_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'syzygy').unlink()
            (root / 'syzygy').symlink_to(root / 'missing-old-binary')
            self.assertEqual(self.repair(root).returncode, 0)
            backup = next(root.glob('syzygy.pre-rpm.*'))
            self.assertTrue(backup.is_symlink())
            self.assertEqual(os.readlink(backup), str(root / 'missing-old-binary'))


if __name__ == '__main__':
    unittest.main()
