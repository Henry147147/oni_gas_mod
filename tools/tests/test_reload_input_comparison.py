import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).parents[1]))
from oni_native_domain_evidence import _reload_input_comparison_passed as passed

class ReloadInputComparisonTests(unittest.TestCase):
    def fixture(self):
        return dict(ComparisonMode='captured-live-inputs',IdenticalPhysicalInputs=False,IdenticalFinalPhysicalImage=False,
            FirstPhysicalInputTranscriptSha256='a'*64,CurrentPhysicalInputTranscriptSha256='b'*64,
            FirstExactContinuationFrames=120,CurrentExactContinuationFrames=120,FirstCompletedFrame=120,FirstPhysicalSha256='c'*64),dict(CompletedFrames=120),dict(
                CurrentPhysicalSha256='d'*64,FrameInputReplay=dict(PhysicalInputTranscriptSha256='b'*64))
    def test_live_input_difference_requires_both_exact_timelines_and_explicit_distinct_transcripts(self):
        workflow,physical,colony=self.fixture();self.assertTrue(passed(workflow,physical,colony,True));self.assertFalse(passed(workflow,physical,colony,False))
        for key,value in [('IdenticalPhysicalInputs',True),('FirstExactContinuationFrames',119),('CurrentExactContinuationFrames',119),
                          ('FirstPhysicalInputTranscriptSha256','b'*64),('CurrentPhysicalInputTranscriptSha256',''),
                          ('IdenticalFinalPhysicalImage',True),('ComparisonMode','ignore-differences')]:
            changed=copy.deepcopy(workflow);changed[key]=value
            with self.subTest(key=key):self.assertFalse(passed(changed,physical,colony,True))
    def test_equal_input_contract_retains_identical_image_requirement(self):
        workflow,physical,colony=self.fixture();workflow.update(ComparisonMode='identical-final-physical-state',IdenticalPhysicalInputs=True,
            IdenticalFinalPhysicalImage=True)
        self.assertTrue(passed(workflow,physical,colony,True));workflow['IdenticalFinalPhysicalImage']=False
        self.assertFalse(passed(workflow,physical,colony,True))
        workflow.pop('ComparisonMode');self.assertFalse(passed(workflow,physical,colony,True))
