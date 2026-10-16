import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('observer_package', Path(__file__).resolve().parents[1] / 'package_probe.py')
PACKAGE = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(PACKAGE)


class ObserverBuildLayoutTests(unittest.TestCase):
    def complete(self, root):
        for name in ('CMakeCache.txt', 'oni-generated/licenses/funchook/LICENSE',
                     'oni-generated/licenses/distorm/COPYING', '_deps/ma_funchook-src/src/funchook_x86.c'):
            path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text('fixture')

    def test_single_config(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); self.complete(root)
            self.assertEqual(PACKAGE.observer_build_root(root / 'libmixed_atmospheres_oni.so'), root.resolve())

    def test_visual_studio_release(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); self.complete(root); (root / 'Release').mkdir()
            self.assertEqual(PACKAGE.observer_build_root(root / 'Release/mixed_atmospheres_oni.dll'), root.resolve())

    def test_incomplete_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); self.complete(root); (root / 'oni-generated/licenses/distorm/COPYING').unlink()
            with self.assertRaises(ValueError): PACKAGE.observer_build_root(root / 'mixed_atmospheres_oni.dll')

    def test_ambiguous_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); self.complete(root); self.complete(root / 'Release')
            with self.assertRaises(ValueError): PACKAGE.observer_build_root(root / 'Release/mixed_atmospheres_oni.dll')


if __name__ == '__main__':
    unittest.main()
