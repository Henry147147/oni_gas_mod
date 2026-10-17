"""Acceptance guards for private Linux playtest evidence and package isolation."""
import contextlib
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

if not sys.platform.startswith("linux"):
    raise unittest.SkipTest("The headless ONI runner requires Linux.")

SPEC = importlib.util.spec_from_file_location("oni_headless", Path(__file__).parents[1] / "oni_headless.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class NativeDomainEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.report = {"Kind": "actual-game-sealed-native-fluid-domain", "Status": "passed", "Platform": "LinuxPlayer",
                       "BoundedFluidAuthority": True, "WholeColonyFluidAuthority": False, "FirstPlayableColonyPassed": False,
                       "SavesAllowed": False, "NativeCellsUnchanged": True, "NativeSnapshotCopied": True,
                       "NativeSolidsAdvanced": True, "RetainedFrameVerified": True, "NativeFlowControlObserved": True,
                       "ProjectionPrimed": True, "NativeFlowFenceVerified": True, "NativeRendererAdvanced": True,
                       "SaveGuardVerified": True, "NativeSaveGuardVerified": True, "GameGridProjectionVerified": True, "ProjectionPrimedAtNativeFrame": 0,
                       "RenderedRevision": 30, "Publications": 35,
                       "NativeDomain": {"State": 1, "Session": 1, "CompletedFrames": 30, "NativeTicket": 100, "Substeps": 630,
                           "SolverThread": 2, "StartThread": 1, "MaximumRelativeMassError": 1e-15,
                           "MaximumRelativeEnergyError": 1e-14, "SuppressedPostProcess": 200, "SuppressedTemperature": 800,
                           "SuppressedPressure": 100, "SuppressedLiquid": 0, "SuppressedGas": 180, "TimeSeconds": 6.00000009,
                           "LastTimestepSeconds": .200000003, "InitialOxygenKg": .2, "CurrentOxygenKg": .21,
                           "PressurePa": 100010.0, "LiquidMassKg": 0.0}}
        self.observation = {"BoundedNativeDomainActive": True, "Observation": {"Attached": True, "ForwardingOnly": False,
                            "RecoveryRequired": False, "AuthoritativeFluidOwner": False, "OniBuild": 744825}}

    def test_accepts_only_completed_bounded_physical_ownership(self):
        self.assertTrue(runner.native_domain_passed(self.report, self.observation, 30))
        windows = copy.deepcopy(self.report); windows['Platform'] = 'WindowsPlayer'
        self.assertFalse(runner.native_domain_passed(windows, self.observation, 30))
        self.assertTrue(runner.native_domain_passed(windows, self.observation, 30, expected_platform='WindowsPlayer'))
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 31))
        for field in ("WholeColonyFluidAuthority", "FirstPlayableColonyPassed", "SavesAllowed"):
            value = copy.deepcopy(self.report); value[field] = True
            self.assertFalse(runner.native_domain_passed(value, self.observation, 30), field)

    def test_actual_colony_save_requires_one_paused_paired_section_and_source_clock(self):
        self.report['ColonyCheckpoint']={'Saved':True,'Loaded':False,'WholeColonyFluidCheckpoint':False,'SimSaveWrites':1,
            'SourceFrame':30,'SourceTime':self.report['NativeDomain']['TimeSeconds'],'SourcePhysicalSha256':'a'*64,
            'SavedFile':'private/native-colony.sav','SavedFileSha256':'b'*64,'AutomaticSaveAdmissions':1,'SavePauseStateRestored':True,
            'SavePauseDepthBefore':0,'SavePauseDepthAfter':0,'SavedFileReadbackVerified':True,
            'SerializedBodyCaptures':1,'SerializedBodyBytes':160000,'SerializedBodySha256':'d'*64}
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True))
        for key,value in [('Saved',False),('Loaded',True),('WholeColonyFluidCheckpoint',True),('SimSaveWrites',2),('SourceFrame',29),
                          ('SourceTime',0),('SourcePhysicalSha256',''),('SavedFileSha256',''),('SavedFile',''),('AutomaticSaveAdmissions',0),('SavePauseStateRestored',False),
                          ('SavePauseDepthBefore',-1),('SavePauseDepthBefore',False),('SavePauseDepthAfter',None),('SavePauseDepthAfter',1),
                          ('SavedFileReadbackVerified',False),('SerializedBodyCaptures',0),('SerializedBodyCaptures',2),
                          ('SerializedBodyBytes',0),('SerializedBodySha256','')]:
            report=copy.deepcopy(self.report);report['ColonyCheckpoint'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,saved=True),key)

    def set_restored_colony(self):
        self.report['NativeFlowControlObserved']=False
        self.report['ColonyCheckpoint']={'Saved':False,'Loaded':True,'WholeColonyFluidCheckpoint':False,'ZeroTimeRestoreVerified':True,
            'ManagedSceneRestored':True,'RendererVerified':True,'NativeStartupBatchesDelivered':2,'SourceFrame':31,'SourceTime':1.2,'SourcePhysicalSha256':'a'*64,
            'RestoredInitialSha256':'a'*64,'CurrentPhysicalSha256':'b'*64,'ExactContinuationFrames':30,'ReplayedFrame':30,
            'ReplayedTime':self.report['NativeDomain']['TimeSeconds']}

    def test_actual_restored_colony_requires_exact_initial_state_and_every_worker_frame(self):
        self.set_restored_colony()
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30))
        for key,value in [('Saved',True),('Loaded',False),('WholeColonyFluidCheckpoint',True),('ZeroTimeRestoreVerified',False),
                          ('ManagedSceneRestored',False),('RendererVerified',False),('NativeStartupBatchesDelivered',1),('RestoredInitialSha256','c'*64),
                          ('CurrentPhysicalSha256','a'*64),('ExactContinuationFrames',29),('ReplayedFrame',29),('ReplayedTime',0)]:
            report=copy.deepcopy(self.report);report['ColonyCheckpoint'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,restored=True),key)
        for field in ("NativeCellsUnchanged", "NativeSnapshotCopied", "NativeSolidsAdvanced", "RetainedFrameVerified", "NativeFlowControlObserved", "ProjectionPrimed", "NativeFlowFenceVerified", "NativeRendererAdvanced", "SaveGuardVerified", "NativeSaveGuardVerified", "GameGridProjectionVerified"):
            value = copy.deepcopy(self.report); value[field] = False
            self.assertFalse(runner.native_domain_passed(value, self.observation, 30), field)

    def test_idle_or_failed_solver_and_unexecuted_exclusions_reject(self):
        for field, value in (("State", 3), ("SolverThread", 1), ("CurrentOxygenKg", .2), ("TimeSeconds", 0),
                             ("MaximumRelativeMassError", 1e-7), ("MaximumRelativeEnergyError", float("nan")),
                             ("SuppressedTemperature", 0), ("SuppressedPostProcess", 0)):
            report = copy.deepcopy(self.report); report["NativeDomain"][field] = value
            self.assertFalse(runner.native_domain_passed(report, self.observation, 30), field)
        observation = copy.deepcopy(self.observation); observation["Observation"]["ForwardingOnly"] = True
        self.assertFalse(runner.native_domain_passed(self.report, observation, 30))

    def test_restored_frame_zero_resave_preserves_clock_state_and_nested_pause_depth(self):
        self.set_restored_colony()
        colony=self.report['ColonyCheckpoint']
        colony.update(Saved=True,AutomaticSaveAdmissions=1,SimSaveWrites=1,SavePauseStateRestored=True,
            SavePauseDepthBefore=2,SavePauseDepthAfter=2,SavedFile='private/resaved-colony.sav',SavedFileSha256='c'*64,
            SavedFrame=0,SavedTime=colony['SourceTime'],SavedPhysicalSha256=colony['SourcePhysicalSha256'],
            FrameZeroResaveRequired=True,FrameZeroResaveVerified=True,FrameZeroResaveUsedGameDelayedSave=True,SavedAsAutoSave=True,SavedFileReadbackVerified=True,
            InitialPauseChallengeVerified=True,InitialPositiveTicksHeld=1,
            SerializedBodyCaptures=1,SerializedBodyBytes=160000,SerializedBodySha256='e'*64)
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        for key,value in [('SimSaveWrites',2),('AutomaticSaveAdmissions',2),('SavePauseDepthAfter',3),
                          ('SavePauseDepthAfter',1),('FrameZeroResaveVerified',False),('SavedAsAutoSave',False),('SavedFrame',1),
                          ('SavedFileReadbackVerified',False),('SerializedBodyCaptures',2),('FrameZeroResaveUsedGameDelayedSave',False),
                          ('InitialPauseChallengeVerified',False),('InitialPositiveTicksHeld',0),
                          ('SavedTime',colony['SourceTime']+.2),('SavedPhysicalSha256','d'*64)]:
            report=copy.deepcopy(self.report);report['ColonyCheckpoint'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,restored=True),key)

    def test_restored_equilibrium_and_water_still_require_complete_exact_replay(self):
        self.set_restored_colony()
        self.report['NativeDomain']['CurrentOxygenKg']=self.report['NativeDomain']['InitialOxygenKg']
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.report['Scenario']='compressed-liquid'
        self.report['NativeDomain'].update(LiquidMassKg=1100.0,PressurePa=2.2e8,SuppressedLiquid=100,
            InitialOxygenKg=0,CurrentOxygenKg=0)
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.report['NativeDomain']['PressurePa']=0
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True))

    def test_compressed_liquid_requires_elastic_pressure(self):
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, True))
        self.report["NativeDomain"].update(LiquidMassKg=1100.0, PressurePa=2.2e8, SuppressedLiquid=100)
        self.assertTrue(runner.native_domain_passed(self.report, self.observation, 30, True))
        self.report["NativeDomain"]["PressurePa"] = 0
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, True))

    def test_heat_control_requires_committed_energy_and_no_native_packet_dispatch(self):
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, heat=True))
        self.report.update(HeatCommandsVerified=True, HeatAvoidedNativeDispatch=True, HeatPacketsRouted=2,
                           HeatEnergyResidualJoules=1e-9, HeatCommands={"PendingBatches": 0, "AcceptedBatches": 2,
                           "CommittedBatches": 2, "CommittedHeatJoules": 1500})
        self.assertTrue(runner.native_domain_passed(self.report, self.observation, 30, heat=True))
        for field, value in (("PendingBatches", 1), ("CommittedBatches", 1), ("CommittedHeatJoules", 2000)):
            bad = copy.deepcopy(self.report); bad["HeatCommands"][field] = value
            self.assertFalse(runner.native_domain_passed(bad, self.observation, 30, heat=True), field)
        self.report["HeatEnergyResidualJoules"] = float("nan")
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, heat=True))

    def test_internal_conduction_requires_physical_stages_heat_entropy_and_coherent_frame(self):
        self.report.update(HeatCommandsVerified=True, HeatAvoidedNativeDispatch=True, HeatPacketsRouted=2,
            HeatEnergyResidualJoules=1e-9, HeatCommands={"PendingBatches": 0, "AcceptedBatches": 2,
                "CommittedBatches": 2, "CommittedHeatJoules": 1500})
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, conduction=True))
        self.report.update(ThermalConductionVerified=True, ThermalConduction={"Size": 80, "Version": 1,
            "Enabled": 1, "State": 1, "Session": 1, "CompletedFrame": 30, "Stages": 1260,
            "Contacts": 8820, "Iterations": 1200, "RedistributedEnergy": 1.2, "EntropyChange": .003})
        self.assertTrue(runner.native_domain_passed(self.report, self.observation, 30, conduction=True))
        for field, value in (("Stages", 1259), ("Session", 2), ("CompletedFrame", 29), ("Contacts", 0),
                ("RedistributedEnergy", float("nan")), ("EntropyChange", 0), ("Enabled", 0)):
            bad = copy.deepcopy(self.report); bad["ThermalConduction"][field] = value
            self.assertFalse(runner.native_domain_passed(bad, self.observation, 30, conduction=True), field)

    def test_partial_or_malformed_physical_reports_do_not_raise_or_pass(self):
        for value in (None, {}, {"NativeDomain": None}, {"NativeDomain": []}):
            self.assertFalse(runner.native_domain_passed(value, self.observation, 30))
        for field in ("CompletedFrames", "SuppressedTemperature", "TimeSeconds"):
            for value in (None, True, "30", []):
                report = copy.deepcopy(self.report); report["NativeDomain"][field] = value
                self.assertFalse(runner.native_domain_passed(report, self.observation, 30), (field, value))

    def add_checkpoint(self):
        self.report["PhysicalCheckpoint"]={"Verified": True, "FormatVersion": 2, "ImageBytes": 5000, "SourceFrame": 5, "ResumedFrame": 6,
            "SourcePhysicalTime": 1.0, "ResumedPhysicalTime": 1.200000003, "SourceSha256": "1"*64,
            "ExpectedSha256": "2"*64, "ActualSha256": "2"*64, "OrdinaryGameSaveIntegrated": False}

    def test_checkpoint_requires_identical_actual_next_worker_image_and_retains_save_denial(self):
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,checkpoint=True))
        self.add_checkpoint()
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,checkpoint=True))
        for field,value in (("FormatVersion",1), ("ImageBytes",0), ("SourceFrame",6), ("ResumedFrame",31), ("ActualSha256","3"*64),
                ("SourceSha256","2"*64), ("ExpectedSha256","x"*64), ("ResumedPhysicalTime",float("nan")),
                ("OrdinaryGameSaveIntegrated",True)):
            bad=copy.deepcopy(self.report);bad["PhysicalCheckpoint"][field]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,checkpoint=True),field)

    def test_paired_capture_requires_completed_native_epoch_geometry_and_both_images(self):
        self.add_checkpoint(); self.report.update(X=10,Y=20,Width=3,Height=2)
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,paired=True))
        self.report["PairedCheckpoint"]={"Verified":True,"NativeCompatibilityCellsVerified":True,"ImmutableCopiesVerified":True,"ReleaseVerified":True,
            "OrdinaryGameSaveIntegrated":False,"WholeColonyCheckpoint":False,"RequestedFrame":5,"EncodedBytes":160600,
            "NativeSha256":"1"*64,"PhysicalSha256":"2"*64,"EnvelopeSha256":"3"*64,
            "Native":{"Size":112,"Version":1,"State":2,"Token":7,"Session":1,"CompletedFrame":6,"NativeEpoch":20,"CaptureThread":2,
                "NativeBytes":156845,"PhysicalBytes":3507,"PhysicalTime":1.2,
                "Configuration":{"Size":40,"Version":2,"Flags":1,"GridWidth":64,"X":10,"Y":20,"Width":3,"Height":2}}}
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,paired=True))
        for field,value in (("State",1),("NativeEpoch",0),("CaptureThread",0),("Session",2),("CompletedFrame",31),("PhysicalTime",float("nan")),("PhysicalBytes",0)):
            bad=copy.deepcopy(self.report);bad["PairedCheckpoint"]["Native"][field]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,paired=True),field)
        for field,value in (("ReleaseVerified",False),("ImmutableCopiesVerified",False),("OrdinaryGameSaveIntegrated",True),
                ("WholeColonyCheckpoint",True),("EncodedBytes",100),("EnvelopeSha256","x"*64)):
            bad=copy.deepcopy(self.report);bad["PairedCheckpoint"][field]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,paired=True),field)
        self.report["PairedCheckpoint"]["Native"]["Configuration"]["X"]=11
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,paired=True))

    def add_gas_transfers(self):
        self.report.update(GasTransfersVerified=True, TransfersAvoidedNativeDispatch=True,
            TransferPacketsRouted=2, ReceiptsPublished=2, EmissionCallbacks=1, ConsumptionCallbacks=1,
            TransferEnergyResidualJoules=1e-9, TransferMaximumRelativeMassError=1e-15,
            GasTransfers={"PendingBatches": 0, "AcceptedBatches": 2, "CommittedBatches": 2,
                "UnacknowledgedReceipts": 0, "MassChangesKg": [0, .030000001192092896, 0, 0],
                "KineticEnergyChange": -.001, "MaterialEnergyChange": -2200},
            EmissionResult={"elemIdx": 176, "suceeded": 1, "mass": .05, "temperature": 300,
                "diseaseIdx": 255, "diseaseCount": 0},
            ConsumptionResult={"elemIdx": 176, "mass": .02, "temperature": 300.1,
                "diseaseIdx": 255, "diseaseCount": 0})

    def test_gas_transfers_require_completed_receipts_and_actual_game_callbacks(self):
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, transfers=True))
        self.add_gas_transfers()
        self.assertTrue(runner.native_domain_passed(self.report, self.observation, 30, transfers=True))
        corruptions = (("ReceiptsPublished", 1), ("EmissionCallbacks", 2), ("ConsumptionCallbacks", 0),
                       ("TransferEnergyResidualJoules", float("nan")), ("TransferMaximumRelativeMassError", 1e-8))
        for field, value in corruptions:
            bad = copy.deepcopy(self.report); bad[field] = value
            self.assertFalse(runner.native_domain_passed(bad, self.observation, 30, transfers=True), field)
        for section, field, value in (("GasTransfers", "UnacknowledgedReceipts", 1),
                ("GasTransfers", "MassChangesKg", [0, .03, 0, 0]), ("GasTransfers", "KineticEnergyChange", 0),
                ("ConsumptionResult", "elemIdx", 42), ("ConsumptionResult", "mass", .019),
                ("EmissionResult", "suceeded", 0), ("ConsumptionResult", "temperature", float("inf")),
                ("EmissionResult", "diseaseCount", 1)):
            bad = copy.deepcopy(self.report); bad[section][field] = value
            self.assertFalse(runner.native_domain_passed(bad, self.observation, 30, transfers=True), (section, field))
        carbon = copy.deepcopy(self.report)
        carbon["GasTransfers"]["MassChangesKg"] = [0, 0, 0, 0, .030000001192092896]
        carbon["EmissionResult"].update(elemIdx=42, temperature=310); carbon["ConsumptionResult"]["elemIdx"] = 42
        carbon["CarbonDioxideTransfersVerified"] = True
        carbon["Model"] = "oni-sealed-domain-v2"; carbon["NativeDomain"]["Version"] = 2
        carbon["TargetReading"] = {"Constituents": [{"MaterialId": "carbon_dioxide", "MassKg": .005}]}
        self.assertTrue(runner.native_domain_passed(carbon, self.observation, 30, carbon=True))
        carbon["TargetReading"]["Constituents"] = []
        self.assertFalse(runner.native_domain_passed(carbon, self.observation, 30, carbon=True))

    def test_gravity_requires_carried_potential_force_and_coherent_completed_energy(self):
        self.add_gas_transfers()
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, gravity=True))
        self.report.update(GravityVerified=True, ProjectedPotentialJoules=70.2,
            Gravity={"Size": 96, "Version": 1, "State": 1, "Enabled": 1, "Session": 1, "CompletedFrame": 30,
                "Gravity": 9.81, "Potential": 70.2, "InitialPotential": 70.0, "PendingBoundaryPotential": 0,
                "CommittedBoundaryPotential": .030000001192092896*9.81*.5, "BodyImpulseY": -70,
                "InternalWork": .01, "MaximumEnergyResidual": 1e-9})
        self.assertTrue(runner.native_domain_passed(self.report, self.observation, 30, gravity=True))
        for field, value in (("Session", 2), ("CompletedFrame", 29), ("Gravity", 0), ("BodyImpulseY", 0),
                ("Potential", float("nan")), ("InitialPotential", 70.2), ("CommittedBoundaryPotential", 0),
                ("PendingBoundaryPotential", .1), ("MaximumEnergyResidual", 1e-4), ("InternalWork", float("inf"))):
            bad=copy.deepcopy(self.report);bad["Gravity"][field]=value
            self.assertFalse(runner.native_domain_passed(bad, self.observation, 30, gravity=True), field)
        self.report["EmissionCallbacks"]=0
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, gravity=True))

    def test_registered_actors_require_actual_components_callbacks_and_removal(self):
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, actors=True))
        self.report["RegisteredActors"] = {"Verified": True, "Stage": 8, "Native": {"LiveActors": 0,
            "ConsumerSamples": 16, "EmitterSamples": 24}, "ActualConsumerCallbacks": 16, "ConsumerPublications": 16,
            "EmitterPublications": 24, "RemovalConsumerSamples": 16, "RemovalEmitterSamples": 24, "RemovedAtFrame": 20,
            "EmitterGameStatusObserved": True, "ActualEmitterRegistrations": 1, "ActualBlockedCallbacks": 1,
            "ConfigurationBatchVerified": True, "NativeActorBatchVerified": True, "ConfigurationBatchesRouted": 1,
            "ActualUnblockedCallbacks": 1, "EnergyResidualJoules": 1e-9, "MaximumRelativeMassError": 1e-15}
        self.assertTrue(runner.native_domain_passed(self.report, self.observation, 30, actors=True))
        for field, value in (("ActualConsumerCallbacks", 0), ("ActualBlockedCallbacks", 2), ("ActualUnblockedCallbacks", 0),
                ("EmitterGameStatusObserved", False), ("RemovalEmitterSamples", 23), ("RemovedAtFrame", 28),
                ("EnergyResidualJoules", float("nan")), ("MaximumRelativeMassError", 1e-8), ("Stage", 6)):
            bad = copy.deepcopy(self.report); bad["RegisteredActors"][field] = value
            self.assertFalse(runner.native_domain_passed(bad, self.observation, 30, actors=True), field)
        for field in ("ConfigurationBatchVerified", "NativeActorBatchVerified", "ConfigurationBatchesRouted"):
            bad=copy.deepcopy(self.report);bad["RegisteredActors"].pop(field)
            self.assertFalse(runner.native_domain_passed(bad, self.observation, 30, actors=True), field)


class EvidenceTests(unittest.TestCase):
    def test_optional_native_observer_requires_its_own_forwarding_report(self):
        manifest = {"native_observer_enabled": True}
        report = {"Status": "observing", "OriginalNativePhysicsActive": True,
                  "AuthoritativeFluidOwner": False, "Observation": {"ForwardingOnly": True,
                      "Attached": True, "RecoveryRequired": False, "AuthoritativeFluidOwner": False, "OniBuild": 744825}}
        self.assertTrue(runner.native_observer_passed(manifest, report))
        self.assertFalse(runner.native_observer_passed(manifest, None))
        report["Observation"]["AuthoritativeFluidOwner"] = True
        self.assertFalse(runner.native_observer_passed(manifest, report))
        report["Observation"]["AuthoritativeFluidOwner"] = False
        report["Observation"]["RecoveryRequired"] = True
        self.assertFalse(runner.native_observer_passed(manifest, report))
        self.assertTrue(runner.native_observer_passed({}, None))

    def setUp(self):
        self.probe = {flag: True for flag in runner.PROBE_FLAGS}
        self.probe["Platform"] = "LinuxPlayer"
        self.log = ("[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n"
                    "[Mixed Atmospheres] Native contract captured without native initialization: /x/game-contract-local\n")

    def test_successful_startup_is_not_successful_colony_validation(self):
        self.assertEqual(runner.classify("menu", self.log, self.probe, None, False)[1], 0)
        self.assertNotEqual(runner.classify("presentation", self.log, self.probe, None, False)[1], 0)

    def test_linux_and_all_scoped_checks_are_required(self):
        self.probe["Platform"] = "WindowsPlayer"
        self.assertNotEqual(runner.classify("menu", self.log, self.probe, None, False)[1], 0)
        self.probe["Platform"] = "LinuxPlayer"
        self.probe[runner.PROBE_FLAGS[-1]] = "true"
        self.assertNotEqual(runner.classify("menu", self.log, self.probe, None, False)[1], 0)

    def test_successful_probe_before_main_menu_does_not_pass(self):
        self.assertNotEqual(runner.classify("menu", "", self.probe, None, False)[1], 0)
        status, code = runner.classify("menu", "Steam not initialized in time", self.probe, None, False)
        self.assertEqual((status, code), ("blocked-steam-authentication", 3))

    def test_crashes_timeout_and_late_failures_override_completed_report(self):
        report = {"Status": "passed", "GameStarted": True}
        for timed_out, player_exit, extra in ((True, 0, ""), (False, 139, ""),
                (False, 0, "[Mixed Atmospheres] Colony validation failed: late error")):
            self.assertNotEqual(runner.classify("presentation", self.log + extra, self.probe,
                                               report, timed_out, player_exit)[1], 0)

    def test_menu_export_failure_with_null_reference_does_not_pass(self):
        # The verified false pass: marker, then the mod's export error and a
        # NullReferenceException through the patched MainMenu.OnSpawn.
        log = ("[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n"
               "[17:30:01.050] [1] [ERROR] [Mixed Atmospheres] Native contract export failed: "
               "System.NullReferenceException: Object reference not set to an instance of an object\n"
               "  at MixedAtmospheres.Oni.NativeContractCapture.Export () [0x00012] in <0a1b>:0 \n"
               "  at MixedAtmospheres.Oni.MainMenuContractExportPatch.Postfix () [0x00000] in <0a1b>:0 \n"
               "NullReferenceException: Object reference not set to an instance of an object\n"
               "  at (wrapper dynamic-method) MainMenu.DMD<MainMenu::OnSpawn>(MainMenu)\n"
               "  at KMonoBehaviour.Spawn () [0x00068] in <b4ac>:0 \n")
        status, code = runner.classify("menu", log, self.probe, None, False, None, ("MA-Linux-x",))
        self.assertEqual((status, code), ("failed-mod-log-errors", 2))
        lines = runner.mod_failure_lines(log)
        self.assertTrue(any("Native contract export failed" in line for line in lines))
        self.assertIn("NullReferenceException: Object reference not set to an instance of an object", lines)
        self.assertEqual(runner.contract_export(log), "failed")
        warning = ("[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n[Mixed Atmospheres] Native contract diagnostics "
                   "capture failed; gameplay is unaffected: System.NullReferenceException: x\n")
        self.assertEqual(runner.contract_export(warning), "failed")
        self.assertEqual(runner.classify("menu", warning, self.probe, None, False), ("failed-mod-log-errors", 2))

    def test_clean_menu_pass_ignores_game_noise(self):
        log = ("kGfxThreadingModeClientWorkerNativeJobs is not supported on OpenGL Core. "
               "Reverting to kGfxThreadingModeClientWorkerJobs instead.\n"
               "Could not fetch DPI for display (screen): Couldn't get DPI\n"
               "[17:01:43.045] [1] [WARNING] MixedAtmospheres.Core, Version=1.0.0.0, Culture=neutral, "
               "PublicKeyToken=null at /x/mods/Dev/MA-Linux-x has no classes inheriting from UserMod, creating one...\n"
               "[Mixed Atmospheres] Integration probe completed. Full simulation ownership is NOT certified; "
               "this package does not change colony physics. Report: /x/mods/Dev/MA-Linux-x/integration-probe.json\n"
               "[17:01:47.536] [1] [WARNING] Missing Anim: [0x31A5D250]. You may have to run Collect Anim on the Assets prefab\n"
               "[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n"
               "[Mixed Atmospheres] Runtime discovery captured 900 prefabs and 300 recipes; "
               "ownership and coverage completeness remain unverified.\n"
               "[Mixed Atmospheres] Native contract captured without native initialization: /x/mods/Dev/MA-Linux-x/game-contract-local\n"
               "[17:30:09.585] [7] [ERROR] Fatal parse error in /x/game/OxygenNotIncluded_Data/StreamingAssets/"
               "templates/events/strangeasteroid/marsh_frag_table_rotated.yaml\n"
               "(Line: 358, Col: 3, Idx: 7486) - (Line: 358, Col: 3, Idx: 7486): Exception during deserialization\n"
               "Thread was being aborted.\n")
        self.assertEqual(runner.mod_failure_lines(log, ("MA-Linux-x",)), [])
        self.assertEqual(runner.classify("menu", log, self.probe, None, False, None, ("MA-Linux-x",)),
                         ("passed-scoped-main-menu", 0))

    def test_menu_requires_contract_export_completion(self):
        log = "[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n"
        self.assertEqual(runner.contract_export(log), None)
        self.assertEqual(runner.classify("menu", log, self.probe, None, False),
                         ("failed-main-menu-contract-export", 2))
        # An export line from before the menu marker does not count.
        early = runner.EXPORT_DONE_MARKER + " /x\n" + log
        self.assertNotEqual(runner.classify("menu", early, self.probe, None, False)[1], 0)

    def test_mod_load_and_harmony_failures_are_detected(self):
        for line in ("[18:31:49.529] [1] [ERROR] null Exception while loading mod MixedAtmospheres.IntegrationProbe at /x.",
                     "[18:31:49.530] [1] [INFO] Failed to load mod Mixed Atmospheres - Integration Probe...disabling",
                     "HarmonyLib.HarmonyException: Patching exception in method System.Void SimMessages::Dig()",
                     "[Mixed Atmospheres] Private input observation failed: InvalidOperationException: x"):
            self.assertEqual(runner.classify("menu", self.log + line + "\n", self.probe, None, False),
                             ("failed-mod-log-errors", 2), line)
        game_only = self.log + ("[17:30:02.000] [1] [ERROR] Something in the game failed\n"
                                "NullReferenceException: Object reference not set to an instance of an object\n"
                                "  at Klei.Foo.Bar () [0x0] in <a>:0 \n")
        self.assertEqual(runner.classify("menu", game_only, self.probe, None, False)[1], 0)

    def test_mod_failure_is_not_hidden_by_steam_block(self):
        blocked = "[17:01:40.292] [1] [ERROR] Steam not initialized in time.\n"
        self.assertEqual(runner.classify("menu", blocked, self.probe, None, False, 2),
                         ("blocked-steam-authentication", 3))
        failing = blocked + "[Mixed Atmospheres] Native contract export failed: System.Exception: x\n"
        self.assertEqual(runner.classify("menu", failing, self.probe, None, False, 2)[1], 2)
        bad_probe = dict(self.probe, NativeMixtureRoundtripPassed=False)
        self.assertEqual(runner.classify("menu", blocked, bad_probe, None, False, 2),
                         ("failed-integration-probe", 2))

    def test_failed_or_unstarted_colony_cannot_pass(self):
        for report in ({"Status": "failed", "GameStarted": True}, {"Status": "passed", "GameStarted": False}):
            self.assertNotEqual(runner.classify("presentation", self.log, self.probe, report, False)[1], 0)
        self.assertEqual(runner.classify("presentation", self.log, self.probe,
                                       {"Status": "passed", "GameStarted": True}, False)[1], 0)


    def test_named_failures_survive_nearby_shutdown_abort(self):
        failures = (
            "[Mixed Atmospheres] Private input observation failed: InvalidOperationException: sentinel",
            "HarmonyLib.HarmonyException: Patching exception in method System.Void SimMessages::Dig()",
            "[ERROR] MA-Linux-unit error: sentinel",
        )
        for failure in failures:
            for shutdown in (
                failure + "\nThread was being aborted.\n",
                failure + "\nworker cleanup\nThread was being aborted.\n",
                "Thread was being aborted.\n" + failure + "\n",
                failure + " Thread was being aborted.\n",
            ):
                for steam in ("", "Steam not initialized in time.\n"):
                    log = self.log + steam + shutdown
                    lines = runner.mod_failure_lines(log, ("MA-Linux-unit",))
                    self.assertTrue(any(failure in line for line in lines), log)
                    self.assertEqual(runner.classify("menu", log, self.probe, None, False, 0,
                                                     ("MA-Linux-unit",)),
                                     ("failed-mod-log-errors", 2), log)

    def test_mod_stack_exception_survives_following_shutdown_abort(self):
        header = "NullReferenceException: sentinel before worker shutdown"
        for frame in (
            "  at MixedAtmospheres.Oni.UnitProbe.Read () [0x0] in <a>:0",
            "  at HarmonyLib.PatchFunctions.UpdateWrapper () [0x0] in <a>:0",
            "  at (wrapper dynamic-method) MainMenu.DMD<MainMenu::OnSpawn>(MainMenu)",
        ):
            log = self.log + header + "\n" + frame + "\nThread was being aborted.\n"
            self.assertIn(header, runner.mod_failure_lines(log), log)
            self.assertEqual(runner.classify("menu", log, self.probe, None, False, 0),
                             ("failed-mod-log-errors", 2), log)

    def test_worker_thread_abort_exception_stack_remains_ignored(self):
        for prefix in ("", "[ERROR] Fatal parse error in /x/game/templates/worker.yaml\n"
                            "Exception during deserialization\n"):
            for frame in (
                "  at Klei.YamlIO.Load () [0x0] in <a>:0",
                "  at MixedAtmospheres.Oni.UnitProbe.Read () [0x0] in <a>:0",
                "  at HarmonyLib.PatchFunctions.UpdateWrapper () [0x0] in <a>:0",
                "  at (wrapper dynamic-method) MainMenu.DMD<MainMenu::OnSpawn>(MainMenu)",
            ):
                log = (self.log + prefix + "System.Threading.ThreadAbortException: Thread was being aborted.\n"
                       + frame + "\n")
                self.assertEqual(runner.mod_failure_lines(log), [], log)
                self.assertEqual(runner.classify("menu", log, self.probe, None, False, 0),
                                 ("passed-scoped-main-menu", 0), log)


class SandboxTests(unittest.TestCase):
    def test_game_shares_host_pid_namespace_and_keeps_filesystem_sandbox(self):
        root = Path("/srv/state")
        command = runner.sandbox_command(root / "runs/r/home", root, root / "runs/r", Path("/games/oni"),
                                         root / "runs/r/game", keep_open=False)
        self.assertNotIn("--unshare-pid", command)
        self.assertNotIn("--unshare-all", command)
        self.assertNotIn("--proc", command)
        pairs = list(zip(command, command[1:], command[2:]))
        for expected in (("--ro-bind", "/", "/"), ("--bind", "/proc", "/proc"), ("--dev-bind", "/dev", "/dev"),
                         ("--bind", "/tmp", "/tmp"), ("--ro-bind", str(root), str(root)),
                         ("--bind", str(root / "runs/r/home"), str(Path.home())),
                         ("--ro-bind", "/games/oni", "/games/oni")):
            self.assertIn(expected, pairs)
        self.assertEqual(command[:2], ["bwrap", "--die-with-parent"])
        self.assertNotIn("--die-with-parent", runner.sandbox_command(
            root / "h", root, root / "o", Path("/g"), root / "t", keep_open=True))

    def test_load_rejection_is_rejected_on_linux(self):
        self.assertIn("Windows", runner.unsupported_mode("load-rejection", "linux"))
        self.assertIsNone(runner.unsupported_mode("menu", "linux"))
        with tempfile.TemporaryDirectory() as state, mock.patch.object(sys, "platform", "linux"), \
                mock.patch.object(sys, "argv", ["oni_headless", "--state", state, "run", "--package", state,
                                                "--mode", "load-rejection"]), \
                contextlib.redirect_stderr(io.StringIO()) as error:
            self.assertEqual(runner.main(), 2)
        self.assertIn("load-rejection cannot run on Linux", error.getvalue())


class ProcessTests(unittest.TestCase):
    def test_stop_process_tolerates_already_exited_process(self):
        with tempfile.TemporaryDirectory() as directory:
            record = runner.spawn(["true"], Path(directory) / "log", dict(os.environ))
            runner.CHILDREN[record["pid"]].wait()
            runner.stop_process(record)
            # Simulate exit between the identity check and the signal.
            with mock.patch.object(runner, "alive", return_value=True), \
                    mock.patch.object(runner.os, "getpgid", side_effect=ProcessLookupError):
                runner.stop_process(record)

    def test_session_commands_share_one_lock(self):
        with tempfile.TemporaryDirectory() as state:
            (Path(state) / "session.json").write_text("{}")
            with runner.locked(Path(state) / runner.LOCK_NAME):
                for argv in (["stop-game"], ["stop"], ["start"], ["run", "--package", state]):
                    with mock.patch.object(sys, "argv", ["oni_headless", "--state", state, *argv]):
                        with self.assertRaisesRegex(RuntimeError, "Another runner operation is active"):
                            runner.main()
            self.assertEqual(set(runner.LOCKED_COMMANDS), {"setup", "start", "stop", "stop-game", "run"})


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        paths = ["MixedAtmospheres.Oni.dll", "MixedAtmospheres.Core.dll", "native/linux-x64/libmixed_atmospheres.so"]
        for name in paths:
            file = self.root / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"test fixture")
        self.manifest = {"kind": "integration-probe", "release_ready": False,
                         "source_revision": "0123456789abcdef0123456789abcdef01234567", "source_dirty": True,
                         "files": {name: runner.digest(self.root / name) for name in paths}}
        self.write_manifest()

    def write_manifest(self):
        (self.root / "package-manifest.json").write_text(json.dumps(self.manifest))

    def test_clean_package_passes_but_tampering_does_not(self):
        runner.verify_package(self.root)
        (self.root / "MixedAtmospheres.Oni.dll").write_bytes(b"changed")
        with self.assertRaises(ValueError):
            runner.verify_package(self.root)

    def test_manifest_must_record_source_revision(self):
        for key, value in (("source_revision", None), ("source_revision", "HEAD"), ("source_dirty", "yes")):
            manifest = dict(self.manifest)
            if value is None:
                del manifest[key]
            else:
                manifest[key] = value
            (self.root / "package-manifest.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "source_revision"):
                runner.verify_package(self.root)

    def test_result_revision_comes_from_package_not_checkout(self):
        fields = runner.revision_fields(runner.verify_package(self.root))
        self.assertEqual(fields["source_revision"], self.manifest["source_revision"])
        self.assertIs(fields["source_dirty"], True)
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=runner.ROOT, text=True,
                              capture_output=True).stdout.strip() or None
        self.assertEqual(fields["runner_git_revision"], head)
        self.assertNotEqual(fields["source_revision"], fields["runner_git_revision"])

    def test_unlisted_dll_is_rejected(self):
        (self.root / "StaleMod.dll").write_bytes(b"unlisted")
        with self.assertRaises(ValueError):
            runner.verify_package(self.root)

    def test_manifest_cannot_include_outside_files(self):
        self.manifest["files"]["../outside"] = "unused"
        self.write_manifest()
        with self.assertRaises(ValueError):
            runner.verify_package(self.root)

    def test_symlinks_are_rejected_even_with_matching_hash(self):
        file = self.root / "MixedAtmospheres.Core.dll"
        target = self.root / "MixedAtmospheres.Oni.dll"
        file.unlink()
        file.symlink_to(target.name)
        with self.assertRaises(ValueError):
            runner.verify_package(self.root)


@contextlib.contextmanager
def _mock_playtest_case(failure=None, outcome=("passed-scoped-main-menu", 0)):
    """Exercise only launch ownership/order with private files and mocked processes."""
    with tempfile.TemporaryDirectory() as directory, contextlib.ExitStack() as stack:
        root = Path(directory)
        package = root / "package"
        package.mkdir()
        game = root / "game-source"
        game.mkdir()
        (game / "OxygenNotIncluded").write_bytes(b"\x7fELFunit fixture")
        state = root / "state"
        state.mkdir()
        output = root / "evidence"
        args = mock.Mock(package=package, game_dir=game, output=output, mode="menu",
                         frames=1, timeout=30, keep_open=True, load_save=None, mod_id=None, save_colony=False, resave_before_resume=False)
        steam = {"unit_role": "steam"}
        xvfb = {"unit_role": "xvfb"}
        session = {"steam": steam, "xvfb": xvfb, "display": ":unit"}
        record = {"pid": 987654321, "start_ticks": "unit"}
        child = mock.Mock()
        child.poll.return_value = 0
        events = []
        writes = []

        def write(path, value):
            events.append("write:" + path.name)
            if (failure == "session" and path == state / "session.json") \
                    or (failure == "result" and path == output / "result.json"):
                raise OSError("unit " + failure + " persistence failed")
            writes.append((path, value))

        def stage(*unused_args):
            events.append("stage")
            if failure == "copy":
                raise ValueError("unit copied payload rejected")

        def spawn(*unused_args):
            events.append("spawn")
            return record

        def digest(*unused_args):
            events.append("digest")
            if failure == "metadata":
                raise OSError("unit game hash failed")
            return "0" * 64

        cleanup = mock.Mock(side_effect=lambda unused_record: events.append("cleanup"))
        patches = (
            mock.patch.object(runner, "verify_package", return_value={"unit": "manifest"}),
            mock.patch.object(runner, "stage_package", side_effect=stage, create=True),
            mock.patch.object(runner, "alive", side_effect=lambda value: value is steam or value is xvfb),
            mock.patch.object(Path, "symlink_to", autospec=True, return_value=None),
            mock.patch.object(runner, "write_json", side_effect=write),
            mock.patch.object(runner, "desktop_env", return_value={}),
            mock.patch.object(runner, "spawn", side_effect=spawn),
            mock.patch.object(runner, "screenshot"),
            mock.patch.object(runner, "classify", return_value=outcome),
            mock.patch.object(runner, "digest", side_effect=digest),
            mock.patch.object(runner, "revision_fields", return_value={}),
            mock.patch.object(runner, "stop_process", cleanup),
            mock.patch.dict(runner.CHILDREN, {record["pid"]: child}),
        )
        for patch in patches:
            stack.enter_context(patch)
        yield args, state, session, record, events, writes, cleanup


class CopiedPayloadTests(unittest.TestCase):
    write_manifest = PackageTests.write_manifest

    def setUp(self):
        PackageTests.setUp(self)
        self.copy_temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.copy_temporary.cleanup)
        self.copy_root = Path(self.copy_temporary.name)

    def test_launch_copy_matches_the_verified_source_manifest(self):
        pinned = runner.verify_package(self.root)
        target = self.copy_root / "mod"
        runner.stage_package(self.root, target, pinned)
        self.assertEqual(runner.verify_package(target), pinned)
        for relative in pinned["files"]:
            self.assertEqual((target / relative).read_bytes(), (self.root / relative).read_bytes())

    def test_copied_payload_or_inventory_tampering_is_rejected(self):
        pinned = runner.verify_package(self.root)
        copytree = runner.shutil.copytree
        for variant in ("payload", "inventory"):
            target = self.copy_root / variant

            def tamper(source, destination, *args, **kwargs):
                result = copytree(source, destination, *args, **kwargs)
                if Path(source) == self.root:
                    if variant == "payload":
                        (Path(destination) / "MixedAtmospheres.Oni.dll").write_bytes(b"changed copy")
                    else:
                        (Path(destination) / "Unlisted.dll").write_bytes(b"unlisted copy")
                return result

            with mock.patch.object(runner.shutil, "copytree", side_effect=tamper):
                with self.assertRaises(ValueError):
                    runner.stage_package(self.root, target, pinned)
        self.assertEqual(runner.verify_package(self.root), pinned)

    def test_repinning_the_copy_does_not_replace_the_verified_source_manifest(self):
        pinned = runner.verify_package(self.root)
        target = self.copy_root / "repinned"
        copytree = runner.shutil.copytree

        def repin(source, destination, *args, **kwargs):
            result = copytree(source, destination, *args, **kwargs)
            if Path(source) == self.root:
                changed = Path(destination) / "MixedAtmospheres.Oni.dll"
                changed.write_bytes(b"different self-consistent copy")
                manifest = json.loads(json.dumps(pinned))
                manifest["files"]["MixedAtmospheres.Oni.dll"] = runner.digest(changed)
                (Path(destination) / "package-manifest.json").write_text(json.dumps(manifest))
            return result

        with mock.patch.object(runner.shutil, "copytree", side_effect=repin):
            with self.assertRaisesRegex(ValueError, "Copied package manifest"):
                runner.stage_package(self.root, target, pinned)
        self.assertEqual(runner.verify_package(self.root), pinned)

    def test_playtest_copy_rejection_precedes_config_and_launch(self):
        with _mock_playtest_case(failure="copy") as (args, state, session, record, events, writes, cleanup):
            with self.assertRaisesRegex(ValueError, "copied payload rejected"):
                runner.playtest(args, state, session)
            self.assertEqual(events, ["stage"])
            self.assertEqual(writes, [])
            cleanup.assert_not_called()


class LaunchLifetimeTests(unittest.TestCase):
    def test_save_load_profiles_keep_a_stable_mod_label(self):
        identities=[]
        for requested in (None,None,'OriginalSourceMod'):
            with _mock_playtest_case() as (args,state,session,record,events,writes,cleanup):
                args.mod_id=requested
                self.assertEqual(runner.playtest(args,state,session),0)
                settings=next(value for path,value in writes if path.name=='mods.json')
                label=settings['mods'][0]['label'];identities.append(label['id'])
                self.assertEqual(label['version'],runner.mono_label_version(label['id']))
        self.assertEqual(identities,['MixedAtmospheres.IntegrationProbe','MixedAtmospheres.IntegrationProbe','OriginalSourceMod'])
        for invalid in ('.','..','../other','a/b','a'*129):
            with _mock_playtest_case() as (args,state,session,record,events,writes,cleanup):
                args.mod_id=invalid
                with self.assertRaises(ValueError):runner.playtest(args,state,session)
                self.assertNotIn('spawn',events)

    def test_keep_open_failures_cleanup_after_spawn(self):
        for failure in ("session", "metadata", "result"):
            with _mock_playtest_case(failure=failure) as (args, state, session, record, events, writes, cleanup):
                with self.assertRaises(OSError):
                    runner.playtest(args, state, session)
                cleanup.assert_called_once_with(record)
                self.assertLess(events.index("spawn"), events.index("cleanup"))
                self.assertFalse(any(path.name == "result.json" for path, value in writes))

    def test_retention_commits_only_after_success_receipt(self):
        for outcome in (("passed-scoped-main-menu", 0), ("failed-mod-log-errors", 2)):
            with _mock_playtest_case(outcome=outcome) as (args, state, session, record, events, writes, cleanup):
                self.assertEqual(runner.playtest(args, state, session), outcome[1])
                saved = [value for path, value in writes if path.name == "result.json"]
                self.assertEqual(len(saved), 1)
                self.assertEqual(saved[0]["retained_game"], outcome[1] == 0)
                if outcome[1] == 0:
                    cleanup.assert_not_called()
                else:
                    cleanup.assert_called_once_with(record)
                    self.assertLess(events.index("write:result.json"), events.index("cleanup"))


class SpawnLifetimeTests(unittest.TestCase):
    def child(self):
        process = mock.Mock()
        process.pid = 987654322
        process.wait.return_value = 0
        return process

    def test_identity_failure_cleans_only_the_fresh_child(self):
        process = self.child()
        original = OSError("unit identity unavailable")
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(runner.subprocess, "Popen", return_value=process), \
                mock.patch.object(runner, "identity", side_effect=original), \
                mock.patch.dict(runner.CHILDREN, {}, clear=True):
            with self.assertRaises(OSError) as raised:
                runner.spawn(["unit-never-executed"], Path(directory) / "console.log", {})
            self.assertIs(raised.exception, original)
            process.terminate.assert_called_once_with()
            process.wait.assert_called_once_with(timeout=5)
            process.kill.assert_not_called()
            self.assertNotIn(process.pid, runner.CHILDREN)

    def test_unrecorded_child_cleanup_escalates_boundedly(self):
        process = self.child()
        process.wait.side_effect = [subprocess.TimeoutExpired("unit", 5), 0]
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(runner.subprocess, "Popen", return_value=process), \
                mock.patch.object(runner, "identity", side_effect=OSError("unit identity unavailable")), \
                mock.patch.dict(runner.CHILDREN, {}, clear=True):
            with self.assertRaises(OSError):
                runner.spawn(["unit-never-executed"], Path(directory) / "console.log", {})
            process.terminate.assert_called_once_with()
            process.kill.assert_called_once_with()
            self.assertEqual(process.wait.call_args_list, [mock.call(timeout=5), mock.call(timeout=5)])
            self.assertNotIn(process.pid, runner.CHILDREN)

    def test_log_close_failure_after_popen_cleans_the_child(self):
        process = self.child()
        log = mock.MagicMock()
        log.open.return_value.__exit__.side_effect = OSError("unit log close failed")
        with mock.patch.object(runner.subprocess, "Popen", return_value=process), \
                mock.patch.object(runner, "identity") as identity, \
                mock.patch.dict(runner.CHILDREN, {}, clear=True):
            with self.assertRaisesRegex(OSError, "log close failed"):
                runner.spawn(["unit-never-executed"], log, {})
            identity.assert_not_called()
            process.terminate.assert_called_once_with()
            process.wait.assert_called_once_with(timeout=5)
            self.assertNotIn(process.pid, runner.CHILDREN)

    def test_unrecorded_child_cleanup_failure_is_explicit(self):
        process = self.child()
        original = OSError("unit identity unavailable")
        process.wait.side_effect = subprocess.TimeoutExpired("unit", 5)
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(runner.subprocess, "Popen", return_value=process), \
                mock.patch.object(runner, "identity", side_effect=original), \
                mock.patch.dict(runner.CHILDREN, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "Spawn failed.*identity unavailable.*cleanup failed") as raised:
                runner.spawn(["unit-never-executed"], Path(directory) / "console.log", {})
            self.assertIs(raised.exception.__cause__, original)
            process.terminate.assert_called_once_with()
            process.kill.assert_called_once_with()
            self.assertEqual(process.wait.call_count, 2)
            self.assertNotIn(process.pid, runner.CHILDREN)

    def test_popen_constructor_failure_has_no_child_to_signal(self):
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(runner.subprocess, "Popen", side_effect=OSError("unit constructor failed")), \
                mock.patch.object(runner, "_stop_unrecorded_child", create=True) as cleanup, \
                mock.patch.dict(runner.CHILDREN, {}, clear=True):
            with self.assertRaisesRegex(OSError, "constructor failed"):
                runner.spawn(["unit-never-executed"], Path(directory) / "console.log", {})
            cleanup.assert_not_called()
            self.assertEqual(runner.CHILDREN, {})


if __name__ == "__main__":
    unittest.main()
