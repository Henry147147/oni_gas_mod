import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from package_probe import native_build_info

class NativeBuildProvenanceTests(unittest.TestCase):
    def test_completed_report_matches_each_packaged_binary_and_retains_its_frozen_revision(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);core=root/'libmixed_atmospheres.so';observer=root/'libmixed_atmospheres_oni.so'
            core.write_bytes(b'core fixture');observer.write_bytes(b'observer fixture')
            report={'kind':'linux-native-compatibility-build','passed':True,'source_revision':'frozen-source',
                'libraries':{p.name:{'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [core,observer]}}
            path=root/'report.json';path.write_text(json.dumps(report))
            self.assertEqual(native_build_info(path,[core,observer]),report)
            observer.write_bytes(b'different build')
            with self.assertRaises(ValueError):native_build_info(path,[core,observer])
            observer.write_bytes(b'observer fixture');report['passed']=False;path.write_text(json.dumps(report))
            with self.assertRaises(ValueError):native_build_info(path,[core,observer])

if __name__=='__main__':unittest.main()
