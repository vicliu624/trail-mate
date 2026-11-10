"""Exercise real DEB adaptation for plain and Debian multiarch SDK layouts."""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
TOOLS = ROOT / 'apps/linux_cardputer_zero/tools'
spec = importlib.util.spec_from_file_location('store_paths', TOOLS / 'check_store_package.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class StorePaths(unittest.TestCase):
    def test_private_runtime_and_launcher_paths(self):
        for path in ('usr/bin/trailmate-cardputer-zero',
                     'usr/lib/trailmate-cardputer-zero/application',
                     'usr/lib/trailmate/tmap-producer/TmapTool',
                     'usr/lib/tmpfiles.d/trailmate.conf',
                     'usr/share/doc/trailmate/COPYING',
                     'usr/share/lintian/overrides/trailmate-cardputer-zero',
                     'usr/share/APPLaunch/applications/trailmate.desktop',
                     'usr/share/APPLaunch/share/images/trailmate-cardputer-zero.png',
                     'usr/share/icons/hicolor/64x64/apps/trailmate.png'):
            with self.subTest(path=path):
                self.assertTrue(policy.allowed_path(path))

    def test_global_sdk_and_unrelated_paths_are_rejected(self):
        for path in ('usr/lib/aarch64-linux-gnu/libtmap.a',
                     'usr/lib/aarch64-linux-gnu/cmake/tmap/tmapConfig.cmake',
                     'usr/lib/libtmap.a', 'usr/include/tmap/tmap.h',
                     'usr/local/lib/trailmate/code', 'usr/lib/unrelated/file',
                     '/usr/lib/trailmate/file', 'usr/lib/trailmate/../../file'):
            with self.subTest(path=path):
                self.assertFalse(policy.allowed_path(path))

    @unittest.skipUnless(shutil.which('dpkg-deb'), 'Integration fixture requires native dpkg-deb')
    def test_actual_packages_strip_both_sdk_layouts_and_preserve_runtime(self):
        for library in ('usr/lib', 'usr/lib/aarch64-linux-gnu'):
            with self.subTest(library=library), tempfile.TemporaryDirectory() as temporary:
                base = Path(temporary)
                source = base / 'input'
                (source / 'DEBIAN').mkdir(parents=True)
                (source / 'DEBIAN/control').write_text(
                    'Package: trailmate-cardputer-zero\nVersion: 1.2.1\nArchitecture: arm64\n'
                    'Maintainer: Test Maintainer\nDescription: Store packaging fixture\n')
                runtime = {'usr/lib/trailmate-cardputer-zero/application': b'real-runtime-surrogate\n',
                           'usr/lib/trailmate/tmap-producer/TmapTool': b'producer-content-must-stay\n'}
                development = {library + '/libtmap.a': b'development-archive'}
                for name in ('tmapConfig', 'tmapConfigVersion', 'tmapTargets-release', 'tmapTargets'):
                    development[library + '/cmake/tmap/' + name + '.cmake'] = b'development-export'
                development['usr/include/tmap/tmap.h'] = b'development-header'
                development['usr/lib/udev/rules.d/70-trailmate-cardputer-zero.rules'] = b'old-udev-rule'
                for name, content in {**runtime, **development}.items():
                    path = source / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(content)
                native = base / 'native.deb'
                subprocess.run(['dpkg-deb', '--build', '--root-owner-group', str(source), str(native)], check=True,
                               stdout=subprocess.DEVNULL)
                output = base / 'trailmate_1.2.1_arm64.deb'
                subprocess.run([sys.executable, str(TOOLS / 'package_store.py'), '--deb', str(native),
                                '--sha256', hashlib.sha256(native.read_bytes()).hexdigest(),
                                '--output', str(output)], check=True, stdout=subprocess.DEVNULL)
                policy.verify(output, '1.2.1')
                unpacked = base / 'unpacked'
                subprocess.run(['dpkg-deb', '-x', str(output), str(unpacked)], check=True)
                for name, content in runtime.items():
                    self.assertEqual((unpacked / name).read_bytes(), content)
                for name in development:
                    self.assertFalse((unpacked / name).exists(), name)
                with self.assertRaisesRegex(ValueError, 'Incorrect Version'):
                    policy.verify(output, '1.2.0')


if __name__ == '__main__':
    unittest.main()
