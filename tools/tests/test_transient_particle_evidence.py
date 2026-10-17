"""Reject missing or duplicated native parcel persistence receipts."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).parents[1]))
from oni_native_domain_evidence import _transient_particles_passed


class TransientParticleEvidenceTests(unittest.TestCase):
    def fixture(self,restored):
        return dict(NativeDomain=dict(Session=3,CompletedFrames=120),TransientParticles=dict(
            Format='MACO2P01',Version=1,MaximumParticles=4096,Source='original-CO2Manager.SpawnCO2-pool',
            UsesOriginalGameFactory=True,WholeColonyParticlePersistence=False,NativeSaveRootChecks=3,
            SavedCount=1,SavedSha256='a'*64,RestoreRequired=restored,Restored=restored,RestoredCount=1,
            RestoredSha256='a'*64,Fixture=dict(Required=True,Identity=7,MassKg=.03125,TemperatureK=333.25,
                Seeded=not restored,DepositReceipts=1 if restored else 0,DepositVerified=restored,
                Deposit=dict(Session=3,CompletedFrame=2,Callback=-1,Kind=1,Species=4,MassKg=.03125,Temperature=333.25))))

    def test_saved_and_restored_original_parcel_have_separate_required_evidence(self):
        for restored in (False,True):
            report=self.fixture(restored);self.assertTrue(_transient_particles_passed(report,restored))
            for field,value in [('Format','other'),('Version',2),('SavedCount',0),('SavedSha256',''),
                                ('NativeSaveRootChecks',0),('UsesOriginalGameFactory',False),('WholeColonyParticlePersistence',True)]:
                bad=copy.deepcopy(report);bad['TransientParticles'][field]=value
                with self.subTest(restored=restored,field=field):self.assertFalse(_transient_particles_passed(bad,restored))

    def test_reload_requires_exact_fields_and_one_current_native_deposit(self):
        for field,value in [('Session',2),('CompletedFrame',0),('CompletedFrame',121),('Callback',7),
                            ('Kind',2),('Species',1),('MassKg',.0625),('Temperature',310.)]:
            report=self.fixture(True);report['TransientParticles']['Fixture']['Deposit'][field]=value
            with self.subTest(field=field):self.assertFalse(_transient_particles_passed(report,True))
        for field,value in [('DepositReceipts',2),('DepositVerified',False),('Seeded',True),('Identity',0)]:
            report=self.fixture(True);report['TransientParticles']['Fixture'][field]=value
            self.assertFalse(_transient_particles_passed(report,True))
        for field,value in [('RestoredSha256','b'*64),('RestoredCount',2),('Restored',False),('RestoreRequired',False)]:
            report=self.fixture(True);report['TransientParticles'][field]=value
            self.assertFalse(_transient_particles_passed(report,True))
