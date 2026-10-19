import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from package_probe import native_build_info
from build_linux_compat import glibc_versions_within_limit

class NativeBuildProvenanceTests(unittest.TestCase):
    def test_completed_report_matches_each_packaged_binary_and_retains_its_frozen_revision(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);core=root/'libmixed_atmospheres.so';observer=root/'libmixed_atmospheres_oni.so'
            core.write_bytes(b'core fixture');observer.write_bytes(b'observer fixture')
            report={'kind':'linux-native-compatibility-build','passed':True,'source_revision':'frozen-source',
                'libraries':{p.name:{'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'required_glibc':['2.2.5','2.34']} for p in [core,observer]}}
            path=root/'report.json';path.write_text(json.dumps(report))
            self.assertEqual(native_build_info(path,[core,observer]),report)
            observer.write_bytes(b'different build')
            with self.assertRaises(ValueError):native_build_info(path,[core,observer])
            observer.write_bytes(b'observer fixture');report['passed']=False;path.write_text(json.dumps(report))
            with self.assertRaises(ValueError):native_build_info(path,[core,observer])

    def test_library_versions_must_meet_the_stricter_project_ceiling(self):
        self.assertTrue(glibc_versions_within_limit(['2.2.5','2.3.4','2.34']))
        for values in (None,[],['2.35'],['2.34','2.35'],['3.0'],['2'],['2.x'],[2.34]):
            with self.subTest(values=values):self.assertFalse(glibc_versions_within_limit(values))

    def test_packaging_rejects_a_passed_older_report_with_a_newer_glibc_dependency(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);library=root/'libmixed_atmospheres_oni.so';library.write_bytes(b'native fixture')
            report={'kind':'linux-native-compatibility-build','passed':True,'glibc_maximum':'2.35',
                'libraries':{library.name:{'sha256':hashlib.sha256(library.read_bytes()).hexdigest(),'required_glibc':['2.2.5','2.35']}}}
            path=root/'report.json';path.write_text(json.dumps(report))
            with self.assertRaises(ValueError):native_build_info(path,[library])
            report['libraries'][library.name]['required_glibc']=['2.2.5','2.34'];path.write_text(json.dumps(report))
            self.assertEqual(native_build_info(path,[library]),report)
            del report['libraries'][library.name]['required_glibc'];path.write_text(json.dumps(report))
            with self.assertRaises(ValueError):native_build_info(path,[library])

if __name__=='__main__':unittest.main()
