"""Portable review protocol tests; synthetic bytes are not game/pixel evidence."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import oni_tile_hover as hover


class PortableHoverReviewTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.assertions = {name: True for name in hover.ASSERTIONS}

    def held(self, case=0):
        return dict(Kind="actual-native-tile-hover-contents-fixture", Status="pending", Captured=True,
                    Error="", CallbackError="", Case=case,
                    Observation=dict(Case=case, Cell=100, Revision=case+1, PublicationGeneration=case+10,
                        ScreenWidth=1280, ScreenHeight=720, DistinctHoverFrames=3, NoSelection=True,
                        ShadowBarSelected=False, SourceId="fixture:cell:100", Contents=["one", "two", "three", "four"],
                        SessionEpoch=1, Header="Fixture", State="Unsafe atmosphere", Warnings=["Chlorine warning"], TraceThreshold=.01,
                        NonTraceIds=["nitrogen", "oxygen", "carbon_dioxide"]+(["steam"] if case!=2 else []),
                        TraceIds=["hydrogen", "chlorine"]+(["steam"] if case==2 else [])))

    def image(self, case=0):
        path = self.directory / f"{case+1:02}-tile-hover-{hover.NAMES[case]}.png"
        path.write_bytes(b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR"+struct.pack(">II",1280,720)+b"synthetic-test-only")
        return path

    def fixture(self):
        report = dict(Kind="actual-native-tile-hover-contents-fixture", Status="passed", Passed=True,
                      Platform="LinuxPlayer", FirstPlayableColonyPassed=False, Error="", CallbackError="",
                      PhysicsOwner="native ONI; private immutable display inventories only", TargetCell=100,
                      RequiresActualNativeHover=True, RequiresPixelReviewReceipt=True, Evidence=[])
        for case, name in enumerate(hover.NAMES):
            held=self.held(case); source=self.directory / "live-report.json"
            source.write_text(json.dumps(held)); image=self.image(case)
            hover.write_receipt(source,self.directory,case,self.assertions)
            report["Evidence"].append(dict(Name=name, Passed=True, Observation=held["Observation"], Screenshot=image.name))
        return report

    def test_requires_explicit_image_assertions_and_unselected_completed_observation(self):
        for name in hover.ASSERTIONS:
            assertions=dict(self.assertions); assertions[name]=False
            with self.subTest(name=name), self.assertRaises(ValueError):
                hover.receipt(self.held(),0,assertions)
        changes=[lambda r:r.update(Case=1),lambda r:r.update(Captured=False),lambda r:r.update(CallbackError="fault"),
                 lambda r:r["Observation"].update(NoSelection=False),lambda r:r["Observation"].update(ShadowBarSelected=True),
                 lambda r:r["Observation"].update(DistinctHoverFrames=2),lambda r:r["Observation"].update(Revision=True)]
        for change in changes:
            held=self.held();change(held)
            with self.assertRaises(ValueError):hover.receipt(held,0,self.assertions)

    def test_writes_bound_closed_files_and_rejects_overwrite(self):
        source=self.directory / "live.json";source.write_text(json.dumps(self.held()));self.image()
        destination=hover.write_receipt(source,self.directory,0,self.assertions);before=destination.read_bytes()
        review=json.loads((self.directory / "default-all-gases.pixel-review.json").read_bytes())
        self.assertEqual(review["Receipt"],json.loads(before))
        with self.assertRaises(FileExistsError):hover.write_receipt(source,self.directory,0,self.assertions)
        self.assertEqual(destination.read_bytes(),before)
        self.assertEqual(list(self.directory.glob(".hover-receipt-*")),[])

    def test_refuses_missing_or_different_geometry_capture_before_continuation(self):
        source=self.directory / "live.json";source.write_text(json.dumps(self.held()))
        with self.assertRaises(FileNotFoundError):hover.write_receipt(source,self.directory,0,self.assertions)
        image=self.image();image.write_bytes(image.read_bytes()[:16]+struct.pack(">II",1920,1080))
        with self.assertRaises(ValueError):hover.write_receipt(source,self.directory,0,self.assertions)
        self.assertEqual(list(self.directory.glob("*.continue.json")),[])

    def test_all_four_complete_cases_have_portable_bound_evidence(self):
        report=self.fixture()
        self.assertTrue(hover.tile_hover_passed(report,self.directory))
        report["Platform"]="WindowsPlayer"
        self.assertFalse(hover.tile_hover_passed(report,self.directory))
        self.assertTrue(hover.tile_hover_passed(report,self.directory,"WindowsPlayer"))

    def test_rejects_missing_case_or_changed_rows_selection_or_scope(self):
        report=self.fixture()
        changes=[lambda r:r["Evidence"].pop(),lambda r:r.update(FirstPlayableColonyPassed=True),
                 lambda r:r["Evidence"][0]["Observation"].update(NonTraceIds=["oxygen"]),
                 lambda r:r["Evidence"][1]["Observation"].update(Revision=999),
                 lambda r:r["Evidence"][1]["Observation"].update(Contents=["changed"]*4),
                 lambda r:r["Evidence"][1]["Observation"].update(Warnings=[]),
                 lambda r:r["Evidence"][1]["Observation"].update(NonTraceIds=["nitrogen","oxygen","carbon_dioxide","steam","steam"]),
                 lambda r:r["Evidence"][2]["Observation"].update(ShadowBarSelected=True),
                 lambda r:r["Evidence"][3].update(Screenshot="another.png")]
        for change in changes:
            candidate=copy.deepcopy(report);change(candidate)
            self.assertFalse(hover.tile_hover_passed(candidate,self.directory))

    def test_rejects_changed_image_or_held_observation_after_review(self):
        report=self.fixture();image=self.directory / report["Evidence"][0]["Screenshot"]
        original=image.read_bytes();image.write_bytes(original+b"changed")
        self.assertFalse(hover.tile_hover_passed(report,self.directory));image.write_bytes(original)
        self.assertTrue(hover.tile_hover_passed(report,self.directory))
        held=self.directory / "default-all-gases.reviewed-report.json";held.write_bytes(held.read_bytes()+b" ")
        self.assertFalse(hover.tile_hover_passed(report,self.directory))

    def test_malformed_nested_reports_decline_without_promoting_incomplete_evidence(self):
        report=self.fixture()
        for evidence in (None, [None]*4, [42]*4, {}, "data"):
            report["Evidence"]=evidence
            self.assertFalse(hover.tile_hover_passed(report,self.directory))


if __name__ == "__main__":
    unittest.main()
