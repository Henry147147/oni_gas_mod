import copy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).parents[1]))
from oni_native_domain_evidence import _native_solid_heat_passed


class NativeSolidHeatEvidenceTests(unittest.TestCase):
    def fixture(self):
        cp=struct.unpack('<f',struct.pack('<f',.79))[0];capacity=10*cp*1000
        before=400.;after=struct.unpack('<f',struct.pack('<f',399.9))[0];loss=capacity*(before-after)
        bound=128*2.220446049250313e-16*(capacity*(before+after)+abs(loss)+abs(loss))
        cell=dict(Size=48,Version=1,Session=3,ExpectedFrame=0,PublicCell=649,NativeElementIndex=17,
            MassKg=10,SpecificHeatKJPerKgK=.79,ConductivityWPerMK=2,NativeInsulation=255)
        heat=dict(Size=112,Version=1,State=1,Reserved=0,Session=3,CompletedFrame=120,Contacts=120,Transfers=100,
            Unresolved=20,Iterations=0,BodyHeatLossJoules=loss,StoredFluidHeatJoules=loss,
            MaximumBalanceResidualJoules=1e-9,MaximumBalanceBoundJoules=1e-7,EntropyLowerBound=.1,RequestedAbsoluteHeatJoules=loss+1)
        value=dict(Required=True,AbiVersion=1,Source='actual-native-UpdateTemperature-cell-pair',ConfiguredCells=[cell],Status=heat,
            Endpoints=[dict(PublicCell=649,BeforeTemperatureK=before,PublishedTemperatureK=after,
                HeatCapacityJoulesPerKelvin=capacity,MassKg=10,SpecificHeatKJPerKgK=cp)],ObservedFrame=120,
            ObservedSolidHeatLossJoules=loss,ObservedBalanceResidualJoules=0,ObservedBalanceBoundJoules=bound,
            EndpointsVerified=True,NativeOldTemperatureGridPreserved=True,WholeColonySolidHeat=False,
            CapacityModel='native-mass-times-native-specific-heat-without-binary32-product-rounding',
            ConductivityModel='harmonic-conductivity-with-squared-native-insulation-fractions')
        return dict(NativeDomain=dict(Session=3,CompletedFrames=120),TargetCell=650,NativeSolidHeat=value,
            ColonyCheckpoint=dict(FrameInputReplay=dict(ActorRefreshFramesCompared=0,HeatBatches=100)))

    def test_actual_current_endpoints_and_separate_account_are_required(self):
        report=self.fixture()
        for restored in (False,True):self.assertTrue(_native_solid_heat_passed(report,restored))
        for key,value in [('Required',False),('Source','fixture'),('ObservedFrame',119),('EndpointsVerified',False),
                          ('NativeOldTemperatureGridPreserved',False),('WholeColonySolidHeat',True),('ObservedBalanceResidualJoules',1),
                          ('ObservedBalanceBoundJoules',1),('CapacityModel','rounded-native-product')]:
            bad=copy.deepcopy(report);bad['NativeSolidHeat'][key]=value
            self.assertFalse(_native_solid_heat_passed(bad,False),key)
        for key,value in [('Session',2),('CompletedFrame',119),('Contacts',0),('Transfers',0),('StoredFluidHeatJoules',0),
                          ('MaximumBalanceResidualJoules',1),('StoredFluidHeatJoules',1),('Contacts',121),('EntropyLowerBound',-1)]:
            bad=copy.deepcopy(report);bad['NativeSolidHeat']['Status'][key]=value
            self.assertFalse(_native_solid_heat_passed(bad,False),key)

    def test_rounded_capacity_wrong_native_binding_and_missing_replay_do_not_pass(self):
        report=self.fixture()
        for key,value in [('MassKg',11),('PublicCell',650),('NativeInsulation',256),('SpecificHeatKJPerKgK',.8),('ExpectedFrame',1)]:
            bad=copy.deepcopy(report);bad['NativeSolidHeat']['ConfiguredCells'][0][key]=value
            self.assertFalse(_native_solid_heat_passed(bad,False),key)
        for key,value in [('HeatCapacityJoulesPerKelvin',7900),('PublishedTemperatureK',400),('BeforeTemperatureK',float('nan')),
                          ('SpecificHeatKJPerKgK',.79),('PublicCell',650)]:
            bad=copy.deepcopy(report);bad['NativeSolidHeat']['Endpoints'][0][key]=value
            self.assertFalse(_native_solid_heat_passed(bad,False),key)
        report['ColonyCheckpoint']['FrameInputReplay']['HeatBatches']=0
        self.assertFalse(_native_solid_heat_passed(report,True))
