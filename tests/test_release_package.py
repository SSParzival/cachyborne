"""The published source asset includes pinned sources, never local saves or binaries."""
from paths import ROOT
import hashlib
import importlib.util
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('package_release', ROOT / 'scripts/package-release.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class ReleasePackageTests(unittest.TestCase):
    def test_archive_and_checksum_include_only_tracked_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'VERSION').write_text('0.7.0\n')
            (root / 'launcher').mkdir(); (root / 'launcher/gui.py').write_text('source')
            (root / 'gpu/vendor').mkdir(parents=True); (root / 'gpu/vendor/source.c').write_text('vendor')
            (root / 'user').mkdir(); (root / 'user/save.dat').write_bytes(b'private')
            files = b'VERSION\0launcher/gui.py\0gpu/vendor/source.c\0'
            with patch.object(package, 'ROOT', root), patch.object(package.subprocess, 'check_output', return_value=files), patch('builtins.print'):
                package.main()
            archive = root / 'dist/cachyborne-0.7.0-source.tar.gz'
            with tarfile.open(archive) as stream:
                self.assertEqual(set(stream.getnames()), {'cachyborne-0.7.0/VERSION', 'cachyborne-0.7.0/launcher/gui.py',
                                                         'cachyborne-0.7.0/gpu/vendor/source.c'})
            expected = hashlib.sha256(archive.read_bytes()).hexdigest()
            self.assertTrue((root / 'dist/SHA256SUMS').read_text().startswith(expected))

    def test_private_paths_in_git_manifest_refuse_packaging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); (root / 'VERSION').write_text('0.7.0')
            with patch.object(package, 'ROOT', root), patch.object(package.subprocess, 'check_output', return_value=b'out/bb-probe\0'):
                with self.assertRaisesRegex(RuntimeError, 'private/generated'):
                    package.main()
