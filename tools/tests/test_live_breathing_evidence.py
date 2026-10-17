"""Actual-request/receipt/callback acceptance, without starting the game."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).parents[1]))
from oni_native_domain_evidence import _live_breathing_passed


class LiveBreathingEvidenceTests(unittest.TestCase):
    def fixture(self,restored=False):
        rows=[]
        for i in range(4):
            rows.append(dict(Request=dict(Size=64,Version=2,Kind=2,Species=1,Flags=769,Session=2,ExpectedFrame=i,Callback=10+i,PublicCell=650,MassKg=.03125),
                Receipt=dict(Session=2,Kind=2,Species=1,Callback=10+i,PublicCell=650,CompletedFrame=i+1,Sequence=i+1,MassKg=.03125,Temperature=300.),
                Element=17,CallbackMassKg=.03125,CallbackTemperature=300.,Disease=255,DiseaseCount=0))
        value=dict(Required=True,Verified=True,Source='actual-OxygenBreather-Sim200ms-world-provider',GameClockUnchanged=True,
            ArtificialBreathingTicks=0,CompleteMetabolismVerified=False,Outstanding=0,Requests=4,Receipts=4,Callbacks=4,PositiveCallbacks=4,
            BreatherInstance=12,SourcePlacement=not restored,RestoredWithoutTeleport=restored,Evidence=rows,OxygenElementId=17,
            RequestedMassKg=.125,DeliveredMassKg=.125,CallbackMassKg=.125)
        body=dict(Size=112,Version=1,State=1,Reserved=0,Session=2,CompletedFrame=10,Contacts=120,Transfers=100,Unresolved=20,
            BodyHeatLossJoules=20.,StoredFluidHeatJoules=20.,MaximumBalanceResidualJoules=1e-12,MaximumBalanceBoundJoules=1e-9,
            EntropyLowerBound=.1,RequestedAbsoluteHeatJoules=21.)
        return dict(NativeBodyHeat=dict(AbiVersion=1,Source='native-element-chunk-contact',CompleteMetabolicBodyModel=False,Status=body),
            LiveBreathing=value,RadialConsumptionMessagesRouted=4,GasTransfers=dict(Session=2,PendingBatches=0,
            UnacknowledgedReceipts=0,MassChangesKg=[0,-.125,0,0,.3]),ColonyCheckpoint=dict(FrameInputReplay=dict(TransferBatches=4,
                CompletedSnapshotAbiVersion=1,HeatBatches=100))),dict(Session=2,CompletedFrames=10)

    def test_complete_original_callback_chain_and_loaded_inputs_are_required(self):
        for restored in (False,True):
            report,physical=self.fixture(restored);self.assertTrue(_live_breathing_passed(report,physical,restored))
            for field,value in [('Verified',False),('CompleteMetabolismVerified',True),('Outstanding',1),('Callbacks',3),
                                ('PositiveCallbacks',0),('ArtificialBreathingTicks',1),('GameClockUnchanged',False),
                                ('DeliveredMassKg',.12),('BreatherInstance',0),('SourcePlacement',restored)]:
                bad=copy.deepcopy(report);bad['LiveBreathing'][field]=value
                with self.subTest(restored=restored,field=field):self.assertFalse(_live_breathing_passed(bad,physical,restored))
            if restored:
                report['ColonyCheckpoint']['FrameInputReplay']['TransferBatches']=0
                self.assertFalse(_live_breathing_passed(report,physical,restored))

    def test_original_callback_identity_and_physical_delivery_cannot_be_substituted(self):
        report,physical=self.fixture()
        for record,field,value in [('Request','Callback',999),('Request','Session',1),('Request','Flags',1),
                                   ('Request','ExpectedFrame',7),('Receipt','Sequence',0),('Receipt','MassKg',1.),
                                   ('Receipt','PublicCell',651),('Receipt','Temperature',310.)]:
            bad=copy.deepcopy(report);bad['LiveBreathing']['Evidence'][0][record][field]=value
            with self.subTest(record=record,field=field):self.assertFalse(_live_breathing_passed(bad,physical,False))
        for field,value in [('Element',18),('CallbackMassKg',.01),('CallbackTemperature',310.),('DiseaseCount',1)]:
            bad=copy.deepcopy(report);bad['LiveBreathing']['Evidence'][0][field]=value
            self.assertFalse(_live_breathing_passed(bad,physical,False))
        for delta in ([.001,-.125,0,0,.3],[0,-.12,0,0,.3],[0,-.125,0,.01,.3]):
            bad=copy.deepcopy(report);bad['GasTransfers']['MassChangesKg']=delta
            self.assertFalse(_live_breathing_passed(bad,physical,False))

    def test_opaque_callback_handles_retain_all_bits_and_only_minus_one_is_absent(self):
        report,physical=self.fixture()
        for handle in (-2147483648,-7,2147483647):
            changed=copy.deepcopy(report);row=changed['LiveBreathing']['Evidence'][0]
            row['Request']['Callback']=row['Receipt']['Callback']=handle
            self.assertTrue(_live_breathing_passed(changed,physical,False))
        row=report['LiveBreathing']['Evidence'][0];row['Request']['Callback']=row['Receipt']['Callback']=-1
        self.assertFalse(_live_breathing_passed(report,physical,False))

    def test_native_body_contacts_must_be_current_finite_and_conservative(self):
        report,physical=self.fixture()
        for key,value in [('Size',80),('Version',2),('Session',1),('CompletedFrame',9),('Transfers',0),('Contacts',0),
                          ('Unresolved',121),('MaximumBalanceResidualJoules',1.),('MaximumBalanceBoundJoules',-1.),
                          ('RequestedAbsoluteHeatJoules',0.),('BodyHeatLossJoules',float('nan')),('EntropyLowerBound',-1.)]:
            changed=copy.deepcopy(report);changed['NativeBodyHeat']['Status'][key]=value
            with self.subTest(key=key):self.assertFalse(_live_breathing_passed(changed,physical,False))
