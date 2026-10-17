"""Acceptance guards for private Linux playtest evidence and package isolation."""
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest
from unittest import mock

if not sys.platform.startswith("linux"):
    raise unittest.SkipTest("The headless ONI runner requires Linux.")

SPEC = importlib.util.spec_from_file_location("oni_headless", Path(__file__).parents[1] / "oni_headless.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class CompletedInspectionTests(unittest.TestCase):
    def test_starts_only_after_completion_and_uses_the_first_success_deadline(self):
        self.assertEqual(runner.completed_inspection({'Status':'pending'},120,None,1000),(None,False))
        started,done=runner.completed_inspection({'Status':'passed'},120,None,1100)
        self.assertEqual((started,done),(1100,False))
        self.assertEqual(runner.completed_inspection({'Status':'passed'},120,started,1219.9),(1100,False))
        self.assertEqual(runner.completed_inspection({'Status':'passed'},120,started,1220),(1100,True))

    def test_declines_failure_without_waiting_and_resets_an_incomplete_leg(self):
        self.assertEqual(runner.completed_inspection({'Status':'failed'},120,None,100),(None,True))
        self.assertEqual(runner.completed_inspection({'Status':'pending'},120,90,100),(None,False))

    def test_invalid_hold_declines_before_package_or_game_mutation(self):
        from types import SimpleNamespace
        for seconds,mode,keep_open in [(-1,'native-domain',False),(301,'native-domain',False),
                                       (True,'native-domain',False),(120,'menu',False),(120,'native-domain',True)]:
            with self.subTest(seconds=seconds,mode=mode,keep_open=keep_open),self.assertRaises(ValueError):
                runner.playtest(SimpleNamespace(inspection_seconds=seconds,mode=mode,keep_open=keep_open),Path('/unused'),{})


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

    def gpu_texture_report(self):
        report=copy.deepcopy(self.report);report['NativeDomain']['Cells']=6;report.update(Width=3,Height=2,X=119,Y=207)
        colours=[[110+i,154,168,255] for i in range(6)];opacity=[150]*6;danger=[0]*6
        report['RenderingInputs']=dict(RenderedRevision=30,PublicationGeneration=35,TexturePublicationGeneration=35,
            Rows=[dict(Cell=100+i,Reading=dict(Revision=30,GasMassKg=1),
                       ExpectedGas=dict(Colour=colours[i],Pressure=opacity[i],Danger=danger[i])) for i in range(6)])
        report['GpuGasTextures']=dict(Required=True,Verified=True,BindingsVerified=True,Source='shader-bound-GPU-readback',
            GraphicsDevice='OpenGLCore',UploadPath='native-texture-page-blit',NativeUploadsUnchanged=True,Session=1,CompletedFrame=30,
            Orientation=dict(Verified=True,Source='native-phase-and-mass-sealed-outline',RowsReflected=True,X=118,Y=206,Width=5,Height=4,
                Expected=[[250,0,0,255]]*20,Identity=dict(Available=True,Rgba=[[255,0,0,131]]*20),Reflected=dict(Available=True,Rgba=[[250,0,0,255]]*20)),
            PublicationGeneration=35,TexturePublicationGeneration=35,ExpectedColours=colours,ExpectedOpacity=opacity,ExpectedDanger=danger,
            **{name:dict(Available=True,Kind='GPU ReadPixels',RowsReflected=True,Rgba=copy.deepcopy(pixels)) for name,pixels in
               [('Colour',colours),('Opacity',[[0,0,0,value] for value in opacity]),('Danger',[[0,0,0,value] for value in danger])]})
        return report

    def test_gpu_extension_requires_actual_colour_opacity_and_hazard_from_the_current_publication(self):
        report=self.gpu_texture_report()
        self.assertTrue(runner.native_domain_passed(report,self.observation,30))
        report['Platform']='WindowsPlayer';report['GpuGasTextures'].update(GraphicsDevice='Direct3D11')
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,expected_platform='WindowsPlayer'))

    def mixed_phase_report(self):
        report=self.gpu_texture_report();report['Scenario']='gas-liquid-mixture'
        report['MixedPhase']=dict(Required=True,Verified=True,Source='completed-native-EOS-partial-volumes',NativeScenario=4,
            SinglePhaseConductionDisabled=True,LiquidWorldRenderingVerified=False,FreeSurfaceDynamicsVerified=False)
        report['ThermalConduction']=dict(Enabled=0,Stages=0,Contacts=0,Iterations=0,RedistributedEnergy=0.0,EntropyChange=0.0)
        pressure=100000.0;temperature=300.0;moles=.8*pressure/(8.31446261815324*temperature)
        contents=[dict(MaterialId=name,Phase=phase,MassKg=mass,MolarMassKgPerMol=molar,
            PartialVolumeCubicMetres=.2 if phase==2 else mass/molar*8.31446261815324*temperature/pressure)
            for name,phase,mass,molar in [('nitrogen',1,.8*moles*.028,.028),('oxygen',1,.2*moles*.032,.032),
                ('helium',1,1e-28,.004),('water',2,.2*1000*(1+pressure/2.2e9),.018)]]
        for row in report['RenderingInputs']['Rows']:
            row['Reading'].update(SourceId='native-domain:cell:'+str(row['Cell']),TemperatureKelvin=temperature,
                MechanicalPressurePa=pressure,GasPressurePa=pressure,GasVolumeCubicMetres=.8,LiquidVolumeCubicMetres=.2,
                VolumeCubicMetres=1.0,VacuumVolumeCubicMetres=0.0,Constituents=copy.deepcopy(contents),
                TotalMassKg=sum(c['MassKg'] for c in contents),GasMassKg=sum(c['MassKg'] for c in contents if c['Phase']==1),
                GasMoles=sum(c['MassKg']/c['MolarMassKgPerMol'] for c in contents if c['Phase']==1))
        report['NativeDomain'].update(PressurePa=pressure,LiquidMassKg=contents[-1]['MassKg'],CurrentOxygenKg=contents[1]['MassKg'])
        return report

    def test_mixed_phase_requires_completed_eos_volumes_without_promoting_liquid_rendering_or_free_surfaces(self):
        report=self.mixed_phase_report()
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,mixed_phase=True))
        self.assertTrue(runner.native_domain_passed(report,self.observation,30))
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,mixed_phase=True))
        for key,value in [('Verified',False),('Required',False),('NativeScenario',4.0),('Source','nominal density'),
                          ('SinglePhaseConductionDisabled',False),('LiquidWorldRenderingVerified',True),('FreeSurfaceDynamicsVerified',True)]:
            changed=copy.deepcopy(report);changed['MixedPhase'][key]=value
            self.assertFalse(runner.native_domain_passed(changed,self.observation,30,mixed_phase=True),(key,value))

    def test_mixed_phase_reviewer_rejects_nominal_liquid_volume_missing_traces_and_inconsistent_common_pressure(self):
        mutations=[lambda r:r['RenderingInputs']['Rows'][0]['Reading'].update(LiquidVolumeCubicMetres=.21),
            lambda r:r['RenderingInputs']['Rows'][0]['Reading'].update(GasVolumeCubicMetres=.9),
            lambda r:r['RenderingInputs']['Rows'][0]['Reading'].update(GasPressurePa=90000),
            lambda r:r['RenderingInputs']['Rows'][0]['Reading']['Constituents'].pop(2),
            lambda r:r['RenderingInputs']['Rows'][0]['Reading']['Constituents'][-1].update(Phase=1),
            lambda r:r['ThermalConduction'].update(Enabled=1),lambda r:r['ThermalConduction'].update(Stages=2),
            lambda r:r.update(Scenario='mixed-air')]
        for mutation in mutations:
            report=self.mixed_phase_report();mutation(report)
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,mixed_phase=True))

    def mixed_heat_report(self):
        report=self.mixed_phase_report()
        report['MixedPhase'].update(NativeScenario=5,SinglePhaseConductionDisabled=False,
            ConductionModel='complete-EOS-contact-split-backward-Euler-v1')
        report['ThermalConduction']=dict(Size=80,Version=1,State=1,Enabled=1,Session=1,CompletedFrame=30,
            Stages=2*report['NativeDomain']['Substeps'],Contacts=8400,Iterations=9000,RedistributedEnergy=12.0,EntropyChange=.001)
        report.update(HeatCommandsVerified=True,MixedHeatVerified=True,HeatAvoidedNativeDispatch=True,HeatPacketsRouted=2,
            ThermalConductionVerified=True,HeatEnergyResidualJoules=0.0,HeatEnergyScaleJoules=1e10,
            HeatEnergyBoundJoules=128*2.2204460492503131e-16*1e10,
            HeatCommands=dict(PendingBatches=0,AcceptedBatches=2,CommittedBatches=2,CommittedHeatJoules=1500000))
        return report

    def test_heated_mixture_requires_full_eos_conduction_and_large_capacity_heat_accounts(self):
        report=self.mixed_heat_report()
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,mixed_phase=True,heat=True,conduction=True))
        for change in [lambda r:r['MixedPhase'].update(NativeScenario=4),
            lambda r:r['MixedPhase'].update(ConductionModel='constant initial heat capacity'),
            lambda r:r['MixedPhase'].update(SinglePhaseConductionDisabled=True),
            lambda r:r['ThermalConduction'].update(Enabled=0),lambda r:r['ThermalConduction'].update(Enabled=True),
            lambda r:r['ThermalConduction'].update(Stages=0),lambda r:r['ThermalConduction'].update(CompletedFrame=29),
            lambda r:r['ThermalConduction'].update(EntropyChange=0),lambda r:r['ThermalConduction'].update(RedistributedEnergy=float('nan')),
            lambda r:r.update(MixedHeatVerified=False),lambda r:r.update(HeatEnergyBoundJoules=1e-6),
            lambda r:r['HeatCommands'].update(CommittedHeatJoules=1500)]:
            altered=copy.deepcopy(report);change(altered)
            self.assertFalse(runner.native_domain_passed(altered,self.observation,30,mixed_phase=True,heat=True,conduction=True))

    def test_restored_heated_mixture_retains_its_operator_without_requiring_new_source_heat_packets(self):
        self.report=self.mixed_heat_report();self.set_restored_colony();self.report['Scenario']='gas-liquid-mixture'
        self.report.update(HeatCommandsVerified=False,MixedHeatVerified=False,HeatPacketsRouted=0)
        self.report['HeatCommands'].update(AcceptedBatches=0,CommittedBatches=0,CommittedHeatJoules=0)
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.report['MixedPhase']['ConductionModel']='disabled'
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True))

    def viscous_report(self):
        report=self.mixed_phase_report();report['MixedPhase'].update(NativeScenario=6,SinglePhaseConductionDisabled=False,
            ConductionModel='complete-EOS-contact-split-backward-Euler-v1')
        report['ThermalConduction']=dict(Size=80,Version=1,State=1,Enabled=1,Session=1,CompletedFrame=30,
            Stages=1260,Contacts=8400,Iterations=24,RedistributedEnergy=0.0,EntropyChange=0.0)
        report['PhysicalViscosity']=dict(Required=True,Verified=True,Source='completed-native-worker-physical-stress',
            Model='plane-strain-Newtonian-exact-mode-split-v1',Boundary='impermeable-free-slip',CompleteMaterialViscosityModel=False,
            Status=dict(Size=120,Version=1,State=1,Enabled=1,Session=1,CompletedFrame=30,Stages=1260,Modes=16800,
                UnresolvedHeatUpdates=12,UnresolvedMomentumUpdates=4,ShearPaSeconds=.5,BulkPaSeconds=.25,
                DissipatedKineticEnergy=.75,StoredHeat=.74999,EntropyLowerBound=.0024,WallImpulseX=-.01,WallImpulseY=.02))
        return report

    def test_viscosity_requires_completed_stress_and_separate_represented_heat_without_source_heat_packets(self):
        report=self.viscous_report()
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,mixed_phase=True))
        changes=[lambda r:r.pop('PhysicalViscosity'),lambda r:r['PhysicalViscosity'].update(Verified=False),
            lambda r:r['PhysicalViscosity'].update(Model='compression-only'),
            lambda r:r['PhysicalViscosity'].update(Boundary='no-slip'),
            lambda r:r['PhysicalViscosity'].update(CompleteMaterialViscosityModel=True)]
        for key,value in [('Size',112),('Enabled',0),('CompletedFrame',29),('Stages',1259),('Modes',0),
            ('ShearPaSeconds',.25),('BulkPaSeconds',.5),('StoredHeat',0),('DissipatedKineticEnergy',0),
            ('EntropyLowerBound',0),('UnresolvedHeatUpdates',-1),('WallImpulseY',float('nan'))]:
            changes.append(lambda r,k=key,v=value:r['PhysicalViscosity']['Status'].update({k:v}))
        for change in changes:
            bad=copy.deepcopy(report);change(bad)
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,mixed_phase=True))

    def test_restored_viscosity_retains_its_worker_profile_and_cannot_claim_work_for_disabled_scenes(self):
        self.report=self.viscous_report();self.set_restored_colony();self.report['Scenario']='gas-liquid-mixture'
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.report['MixedPhase'].update(NativeScenario=5)
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True))

    def test_gpu_gate_requires_independent_appearance_review_when_operands_are_present(self):
        report=self.gpu_texture_report()
        self.assertTrue(runner.native_domain_passed(report,self.observation,30))
        for invalid in (None, {}, {'Model':'invented-phase-renderer'}):
            report['RenderingInputs']['WorldAppearance']=invalid
            self.assertFalse(runner.native_domain_passed(report,self.observation,30))

    def test_cpu_counters_and_exact_physics_cannot_hide_zero_or_stale_gpu_texels(self):
        changes=[lambda r:r['GpuGasTextures'].update(Verified=False),lambda r:r['GpuGasTextures'].update(BindingsVerified=False),
                 lambda r:r['GpuGasTextures'].update(CompletedFrame=29),lambda r:r['GpuGasTextures'].update(Session=2),
                 lambda r:r['GpuGasTextures'].update(TexturePublicationGeneration=34),lambda r:r['GpuGasTextures'].update(NativeUploadsUnchanged=False),
                 lambda r:r['GpuGasTextures']['Orientation'].update(Verified=False),
                 lambda r:r['GpuGasTextures']['Orientation'].update(RowsReflected=False),
                 lambda r:r['GpuGasTextures']['Orientation']['Identity'].update(Rgba=[[250,0,0,255]]*20),
                 lambda r:r['GpuGasTextures']['Colour'].update(Rgba=[[0,0,0,0]]*6),
                 lambda r:r['GpuGasTextures']['Opacity']['Rgba'][0].__setitem__(3,0),
                 lambda r:r['GpuGasTextures']['Danger']['Rgba'][0].__setitem__(3,255),
                 lambda r:r['RenderingInputs']['Rows'][0]['Reading'].update(Revision=29),
                 lambda r:r['RenderingInputs']['Rows'][0]['Reading'].update(GasMassKg=float('nan')),
                 lambda r:r['GpuGasTextures']['Colour'].update(Available=False)]
        for change in changes:
            report=self.gpu_texture_report();report['TextureWrites']=999999;change(report)
            self.assertFalse(runner.native_domain_passed(report,self.observation,30))

    def imported_inventory(self, liquid=False):
        report = copy.deepcopy(self.report)
        rows = []
        for index in range(6):
            masses = [0.0] * 5
            # Include a native float whose short JSON decimal differs from its
            # exact promoted double. A direct JSON-number comparison is wrong.
            masses[3 if liquid else 4] = 1100.0 if liquid else struct.unpack('<f', struct.pack('<f', 1.01))[0]
            rows.append(dict(PublicCell=100+index, NativeElementIndex=10, NativeMassKg=1100.0 if liquid else 1.01,
                             NativeTemperatureK=300.0, MassesKg=masses, TemperatureK=300.0))
        report['ImportedNativeInventory'] = dict(Required=True, Verified=True, AdvancedFromSource=True, AbiVersion=1, NativeFramesInSource=1, ExclusionsBeforeNativeFence=True,
            Source='completed-native-GameDataUpdate', EnergyBasis='declared-reference-EOS-at-native-temperature',
            InitialMomentum='at-rest', Cells=rows)
        self.rehash_inventory(report)
        report['NativeProjectionMasses'] = list((1000,1200,1100) if liquid else (.6,1.2,2.4)) * 2
        report['NativeProjectionTemperatures'] = [280,300,320] * 2
        report['NativeDomain'].update(Cells=6, InitialOxygenKg=0, CurrentOxygenKg=0)
        if liquid: report['NativeDomain'].update(LiquidMassKg=1100, SuppressedLiquid=100)
        return report

    def carbon_particle(self):
        report = copy.deepcopy(self.report)
        report.update(CellMassEditsRouted=1, ReceiptsPublished=1)
        report['GasTransfers'] = dict(Size=104, Version=2, State=1, Session=1, PendingBatches=0,
            AcceptedBatches=1, CommittedBatches=1, UnacknowledgedReceipts=0, MassChangesKg=[0,0,0,0,.03125],
            MaterialEnergyChange=-270000.0, KineticEnergyChange=0)
        receipt = dict(Session=1,CompletedFrame=5,Sequence=1,Kind=1,Callback=-1,Species=4,
                       MassKg=.03125,Temperature=310,InternalEnergy=-270000.0,KineticEnergy=0)
        report['CarbonParticle'] = dict(Required=True,Started=True,Verified=True,ParticleRetired=True,NativeDispatchAvoided=True,
            Source='CO2Manager.SpawnBreath',MassKg=.03125,TemperatureK=310,SourceFrame=4,CompletedFrame=5,
            Receipts=1,Receipt=receipt,MassErrorKg=1e-15,EnergyResidualJoules=1e-9,EnergyBoundJoules=1e-6,
            RetirementObservedBy='CO2Manager.Sim33ms-postfix',ParticleRetirementUnityFrame=100,ParticleReusedAfterRetirement=False)
        return report

    def test_native_co2_particle_requires_actual_retirement_and_conserved_single_deposition(self):
        report = self.carbon_particle()
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,carbon_particle=True))
        report['Platform']='WindowsPlayer'
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,carbon_particle=True,expected_platform='WindowsPlayer'))
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,carbon_particle=True))

    def test_completed_particle_retirement_survives_native_pool_reuse(self):
        report=self.carbon_particle();report['CarbonParticle']['ParticleReusedAfterRetirement']=True
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,carbon_particle=True))

    def subnormal_particle(self):
        report=self.carbon_particle();mass=2.0**-149
        report['CarbonParticle'].update(SubnormalInput=True,InputMassBits=1,MassKg=mass,
            InitialCarbonDioxideMassKg=0,FinalCarbonDioxideMassKg=mass,MassErrorKg=0)
        report['CarbonParticle']['Receipt']['MassKg']=mass
        report['GasTransfers']['MassChangesKg'][4]=mass
        return report

    def test_subnormal_native_particle_needs_positive_full_transport_and_exact_input_bits(self):
        report=self.subnormal_particle()
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,carbon_particle=True,subnormal_carbon_particle=True))
        self.assertFalse(runner.native_domain_passed(self.carbon_particle(),self.observation,30,carbon_particle=True,subnormal_carbon_particle=True))
        for key,value in [('SubnormalInput',False),('InputMassBits',0),('InputMassBits',2),('FinalCarbonDioxideMassKg',0),
                          ('FinalCarbonDioxideMassKg',float('nan')),('FinalCarbonDioxideMassKg',2.0**-148),
                          ('InitialCarbonDioxideMassKg',1e-300),('MassKg',0)]:
            bad=copy.deepcopy(report);bad['CarbonParticle'][key]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,carbon_particle=True,subnormal_carbon_particle=True),key)
        report['GasTransfers']['MassChangesKg'][4]=0
        self.assertFalse(runner.native_domain_passed(report,self.observation,30,carbon_particle=True,subnormal_carbon_particle=True))

    def test_particle_cannot_pass_duplicate_native_effects_or_relaxed_energy_bounds(self):
        changes = [('Verified',False),('ParticleRetired',False),('NativeDispatchAvoided',False),('Receipts',2),
                   ('Source','manual-fixture'),('SourceFrame',0),('CompletedFrame',31),('EnergyBoundJoules',1),
                   ('EnergyResidualJoules',1e-5),('MassErrorKg',1e-9),('RetirementObservedBy','object-identity-at-save'),
                   ('ParticleRetirementUnityFrame',-1)]
        for key,value in changes:
            report=self.carbon_particle();report['CarbonParticle'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,carbon_particle=True),key)
        for key,value in [('Callback',0),('Kind',2),('Sequence',2),('Species',1),('InternalEnergy',0),('MassKg',.0625)]:
            report=self.carbon_particle();report['CarbonParticle']['Receipt'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,carbon_particle=True),key)
        report=self.carbon_particle();report['CellMassEditsRouted']=2
        self.assertFalse(runner.native_domain_passed(report,self.observation,30,carbon_particle=True))

    def breathing_reader(self):
        report=copy.deepcopy(self.report)
        report.update(CellMassEditsRouted=1,ReceiptsPublished=1)
        report['GasTransfers']=dict(Size=104,Version=2,State=1,Session=1,PendingBatches=0,AcceptedBatches=1,
            CommittedBatches=1,UnacknowledgedReceipts=0,MassChangesKg=[0,0,0,0,.125])
        def rows(frame,blocked):
            return [dict(PublicCell=100+i,CompletedPhysicalFrame=frame,OxygenMassKg=.25,ScalarGridMassKg=1.125,
                OxygenMoleFraction=.21,OxygenPartialPressurePa=21000,CarbonDioxideMoleFraction=.01 if blocked else 0,
                CarbonDioxidePartialPressurePa=1000 if blocked else 0,AdvertisedOxygenMassKg=0 if blocked else .25,
                ScalarElement=1,AdvertisedElement=0 if blocked else 1,Failures=48 if blocked else 0,Breathable=not blocked) for i in range(6)]
        def selection(blocked):
            return dict(Cell=105,ElementID=0 if blocked else 1,Mass=0 if blocked else .25,IsBreathable=not blocked,
                TotalBreathableMass=0 if blocked else 1.5,BreatherInstanceId=-900,NoOxygenThreshold=.01)
        report['BreathingCellReader']=dict(Required=True,Started=True,Verified=True,NativeOutsideVerified=True,NativeDispatchAvoided=True,
            Source='GasBreatherFromWorldProvider.GetBestBreathableCellAroundSpecificCell',Reads=24,OxygenElementId=1,VacuumElementId=0,
            Policy=dict(HardMode=True,MinimumOxygenFraction=.15,MaximumOxygenFraction=.35,MinimumOxygenPartialPressurePa=15000,
                MaximumCo2Fraction=.005,MaximumCo2PartialPressurePa=500),Before=rows(4,False),After=rows(30,True),
            SelectionBefore=selection(False),SelectionAfter=selection(True))
        return report

    def test_real_breathing_reader_requires_native_selection_and_completed_oxygen_policy(self):
        report=self.breathing_reader()
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,breathing_read=True))
        report['Platform']='WindowsPlayer'
        self.assertTrue(runner.native_domain_passed(report,self.observation,30,breathing_read=True,expected_platform='WindowsPlayer'))
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,breathing_read=True))

    def test_breathing_evidence_rejects_scalar_oxygen_fallback_stale_cells_and_false_selections(self):
        for key,value in [('AdvertisedOxygenMassKg',1.125),('ScalarElement',9),('Failures',0),('Breathable',True),
                          ('AdvertisedElement',1),('CompletedPhysicalFrame',29),('OxygenMassKg',0),('PublicCell',100),
                          ('CarbonDioxideMoleFraction',0),('CarbonDioxidePartialPressurePa',float('nan'))]:
            report=self.breathing_reader();report['BreathingCellReader']['After'][-1][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,breathing_read=True),key)
        for key,value in [('Cell',999),('Mass',.3),('TotalBreathableMass',6.75),('BreatherInstanceId',0),('IsBreathable',False)]:
            report=self.breathing_reader();report['BreathingCellReader']['SelectionBefore'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,breathing_read=True),key)
        report=self.breathing_reader();report['BreathingCellReader']['Policy']['MaximumCo2Fraction']=1
        self.assertFalse(runner.native_domain_passed(report,self.observation,30,breathing_read=True))
        report=self.breathing_reader();report['BreathingCellReader']['NativeOutsideVerified']=False
        self.assertFalse(runner.native_domain_passed(report,self.observation,30,breathing_read=True))

    def pressure_reader(self,liquid=False):
        report=copy.deepcopy(self.report)
        def f32(value):return struct.unpack('<f',struct.pack('<f',value))[0]
        pressure=220000000 if liquid else 100010
        mass=1100 if liquid else 1.2
        if liquid:report['NativeDomain'].update(LiquidMassKg=1100,PressurePa=pressure,SuppressedLiquid=100,InitialOxygenKg=0,CurrentOxygenKg=0)
        report['NativePressureReader']=dict(Required=True,Verified=True,NativeOutsideVerified=True,CallSitePatchesVerified=True,
            Source='Grid.PressureIndexer.get_Item',Units='kPa-from-completed-mechanical-Pa',GetterReads=6,
            Outside=dict(PublicCell=999,ScalarMassKg=1000,NativeKilopascals=101300,ExpectedScalarKilopascals=101300,
                NativeBits=struct.unpack('<i',struct.pack('<f',101300))[0],ExpectedBits=struct.unpack('<i',struct.pack('<f',101300))[0],
                GetterReadsBefore=6,GetterReadsAfter=6),
            GameCallSiteReads=0 if liquid else 60,PatchedGameCallSites=5,Samples=[dict(PublicCell=100+i,CompletedPhysicalFrame=30,
                PhysicalPressurePa=pressure,GasPressurePa=0 if liquid else pressure,NativeKilopascals=f32(pressure/1000),
                LegacyScalarKilopascals=f32(f32(mass)*f32(101.3)),ScalarMassKg=mass) for i in range(6)])
        return report

    def test_native_pressure_reader_uses_mechanical_pressure_and_retains_native_units(self):
        for liquid in (False,True):
            for platform in ('LinuxPlayer','WindowsPlayer'):
                report=self.pressure_reader(liquid);report['Platform']=platform
                self.assertTrue(runner.native_domain_passed(report,self.observation,30,liquid=liquid,pressure_read=True,expected_platform=platform))
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,pressure_read=True))

    def test_pressure_reader_rejects_scalar_fallback_wrong_units_stale_data_and_unpatched_callers(self):
        for key,value in [('NativeKilopascals',100010),('PhysicalPressurePa',1),('GasPressurePa',0),
                          ('LegacyScalarKilopascals',100),('ScalarMassKg',0),('CompletedPhysicalFrame',29),
                          ('PublicCell',100),('PhysicalPressurePa',float('nan'))]:
            report=self.pressure_reader();report['NativePressureReader']['Samples'][-1][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,pressure_read=True),key)
        for key,value in [('GetterReads',0),('GameCallSiteReads',0),('PatchedGameCallSites',4),('Units','Pa'),('NativeOutsideVerified',False),('CallSitePatchesVerified',False)]:
            report=self.pressure_reader();report['NativePressureReader'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,pressure_read=True),key)
        for key,value in [('NativeBits',0),('ExpectedBits',0),('NativeKilopascals',100),('GetterReadsAfter',7),('PublicCell',100)]:
            report=self.pressure_reader();report['NativePressureReader']['Outside'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,pressure_read=True),key)

    @staticmethod
    def rehash_inventory(report):
        inventory = report['ImportedNativeInventory']
        encoded = b''.join(struct.pack('<II6d',56,1,*row['MassesKg'],row['TemperatureK']) for row in inventory['Cells'])
        inventory['SourceSha256'] = hashlib.sha256(encoded).hexdigest()

    def test_native_inventory_requires_actual_packet_and_preserves_native_float_values(self):
        for liquid in (False, True):
            report = self.imported_inventory(liquid)
            self.assertTrue(runner.native_domain_passed(report,self.observation,30,initial_inventory=True,liquid=liquid))
            report['Platform'] = 'WindowsPlayer'
            self.assertTrue(runner.native_domain_passed(report,self.observation,30,initial_inventory=True,liquid=liquid,expected_platform='WindowsPlayer'))
            self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,initial_inventory=True,liquid=liquid))

    def test_native_inventory_cannot_pass_with_forged_admission_or_scalar_changes(self):
        changes = [('Verified',False),('Required',False),('AdvancedFromSource',False),('NativeFramesInSource',0),('NativeFramesInSource',True),('ExclusionsBeforeNativeFence',False),('Source','fixture'),('InitialMomentum','inferred'),
                   ('EnergyBasis','native-energy-preserved'),('SourceSha256','0'*64),('Cells',[])]
        for key,value in changes:
            report = self.imported_inventory();report['ImportedNativeInventory'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,initial_inventory=True),key)
        for key in ('NativeProjectionMasses','NativeProjectionTemperatures'):
            report=self.imported_inventory();report[key][-1]+=1
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,initial_inventory=True),key)

    def test_native_inventory_rejects_late_corruption_even_with_recomputed_hash(self):
        for fault in ('phase','vacuum','trace','position','element','temperature','boolean','non-native-float'):
            report=self.imported_inventory();rows=report['ImportedNativeInventory']['Cells'];last=rows[-1]
            if fault=='phase':last['MassesKg']=[0,0,0,last['MassesKg'][4],0]
            if fault=='vacuum':last['MassesKg']=[0]*5
            if fault=='trace':last['MassesKg'][2]=1e-28
            if fault=='position':last['PublicCell']=rows[0]['PublicCell']
            if fault=='element':last['NativeElementIndex']=65536
            if fault=='temperature':last['TemperatureK']=float('inf')
            if fault=='boolean':last['MassesKg'][4]=True
            if fault=='non-native-float':last['MassesKg'][4]=1.01
            self.rehash_inventory(report)
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,initial_inventory=True),fault)

    def test_accepts_only_completed_bounded_physical_ownership(self):
        self.assertTrue(runner.native_domain_passed(self.report, self.observation, 30))
        windows = copy.deepcopy(self.report); windows['Platform'] = 'WindowsPlayer'
        self.assertFalse(runner.native_domain_passed(windows, self.observation, 30))
        self.assertTrue(runner.native_domain_passed(windows, self.observation, 30, expected_platform='WindowsPlayer'))
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 31))
        for field in ("WholeColonyFluidAuthority", "FirstPlayableColonyPassed", "SavesAllowed"):
            value = copy.deepcopy(self.report); value[field] = True
            self.assertFalse(runner.native_domain_passed(value, self.observation, 30), field)

    def set_saved_colony(self):
        self.report['ColonyCheckpoint']={'Saved':True,'Loaded':False,'WholeColonyFluidCheckpoint':False,'SimSaveWrites':1,
            'SourceFrame':30,'SourceTime':self.report['NativeDomain']['TimeSeconds'],'SourcePhysicalSha256':'a'*64,
            'SavedFile':'private/native-colony.sav','SavedFileSha256':'b'*64,'AutomaticSaveAdmissions':1,'SavePauseStateRestored':True,
            'SavePauseDepthBefore':0,'SavePauseDepthAfter':0,'SavedFileReadbackVerified':True,
            'SerializedHeaderCaptures':1,'SerializedHeaderSha256':'c'*64,
            'SerializedBodyCaptures':1,'SerializedBodyBytes':160000,'SerializedBodySha256':'d'*64}
        self.set_orbital_references()

    def set_orbital_references(self, restored=False):
        self.report['NativeOrbitalReferences']=dict(Required=True,Verified=True,RestoredBeforeAdvanceVerified=restored,
            Source='read-only-native-orbital-reference-and-save-root-registry',Stage='restored-before-positive-time' if restored else 'before-paired-save',
            RestoredPhysicalFrame=0,RestoredPhysicalTime=self.report['ColonyCheckpoint']['SourceTime'] if restored else 0,
            Inspections=1,Error='',RewrittenReferences=0,GeneralManagedReferenceIntegrityVerified=False,
            Worlds=[dict(OwnerId=1,WorldId=0,Tracked=True,RootInitialized=True,RootSpawned=True,SaveRegistrations=1)],
            References=[dict(OwnerId=1,Slot=0,SavedId=2,TargetId=2,OrbitingWorldId=0,Prefab='OrbitalBG',Tracked=True,
                RootInitialized=True,RootSpawned=True,SaveRegistrations=1)])

    def set_persistent_actors(self,restored=False):
        self.set_restored_colony() if restored else self.set_saved_colony()
        actors=[];bindings=[]
        for index,kind in enumerate((1,2,2)):
            a=dict(Kind=kind,Owner=101+index,Prefab=12345,Component=0,Cell=650+index,Species=1,
                   Mass=.05 if kind==1 else .03125,Interval=0 if kind==1 else .7,Temperature=0 if kind==1 else 300,
                   MaximumDensity=0 if kind==1 else .01 if index==2 else 10000,
                   Elapsed=0 if kind==1 else .3,Remainder=1e-10 if kind==1 else 0,Blocked=index==2)
            actors.append(a)
            bindings.append(dict(Kind=kind,PersistentOwner=a['Owner'],PersistentPrefab=a['Prefab'],PersistentComponent=0,
                Handle=100+index,UnityInstance=-100-index,Cell=a['Cell'],Species=1,PersistentExpectedCallbacks=20 if kind==1 else 0,
                PersistentConsumerCallbacks=20 if kind==1 else 0,PersistentExpectedMass=.2 if kind==1 else 0,
                PersistentConsumerMass=.2 if kind==1 else 0,PersistentEmitterStatusObserved=index==1,EmitterBlocked=index==2))
        self.report['PersistentActors']=dict(Required=True,Source='stable-prefab-object-and-component/native-handle-remap',
            WholeColonyFluidAuthority=False,SourceFixtureRequired=not restored,SourceFixtureVerified=not restored,
            Restored=restored,RestoredBindingsVerified=restored,RestoredContinuationVerified=restored,
            PendingRestores=0,UnsavedActors=0,Native=dict(Size=64,Version=1,State=1,Session=1,CompletedFrame=30,LiveActors=3,ConsumerSamples=30),
            SavedActors=actors,CurrentActors=copy.deepcopy(actors),BoundObjects=bindings,CurrentImageSha256=('b' if restored else 'a')*64)

    def test_saved_native_references_require_an_explicit_successful_unmodified_audit(self):
        self.set_saved_colony()
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True))
        missing=copy.deepcopy(self.report);missing.pop('NativeOrbitalReferences')
        self.assertFalse(runner.native_domain_passed(missing,self.observation,30,saved=True))
        for key,value in [('Required',False),('Verified',False),('Error','missing object'),('Source','scalar guess'),
                          ('Inspections',0),('Inspections',True),('RewrittenReferences',1),('RewrittenReferences',False),
                          ('GeneralManagedReferenceIntegrityVerified',True),('Stage','unknown')]:
            report=copy.deepcopy(self.report);report['NativeOrbitalReferences'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,saved=True),(key,value))

    def test_saved_native_references_reject_late_missing_or_wrong_world_targets_and_unregistered_roots(self):
        self.set_saved_colony()
        for collection,key,value in [('References','SavedId',3),('References','TargetId',-1),('References','OrbitingWorldId',9),
                                     ('References','Prefab','Asteroid'),('References','Tracked',False),('References','SaveRegistrations',0),
                                     ('References','SaveRegistrations',2),('References','RootInitialized',False),('References','RootSpawned',False),
                                     ('References','OwnerId',5),('References','Slot',-1),('References','SavedId',-2),
                                     ('Worlds','Tracked',False),('Worlds','WorldId',-1),('Worlds','SaveRegistrations',2)]:
            report=copy.deepcopy(self.report);report['NativeOrbitalReferences'][collection][0][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,saved=True),(collection,key,value))
        for collection in ('Worlds','References'):
            report=copy.deepcopy(self.report);rows=report['NativeOrbitalReferences'][collection];rows.append(copy.deepcopy(rows[0]))
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,saved=True),collection)

    def test_orbital_admission_accepts_distinct_native_worlds_and_genuinely_empty_optional_references(self):
        self.set_saved_colony();audit=self.report['NativeOrbitalReferences']
        world=copy.deepcopy(audit['Worlds'][0]);world.update(OwnerId=10,WorldId=7);audit['Worlds'].append(world)
        reference=copy.deepcopy(audit['References'][0]);reference.update(OwnerId=10,SavedId=11,TargetId=11,OrbitingWorldId=7)
        audit['References'].append(reference)
        audit['References'].append(dict(OwnerId=10,Slot=1,SavedId=-1,TargetId=-1,OrbitingWorldId=-1))
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True))
        reference['OrbitingWorldId']=0
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,saved=True))

    def test_completed_native_save_root_lifecycle_keeps_unique_original_identities(self):
        self.set_saved_colony()
        lifecycle=dict(Source='existing-SaveLoadRoot.Spawn-before-OrbitalObject.OnSpawn',CompletedRootIds=[2],
            AlreadySpawnedRoots=0,Error='',ReferencesRewritten=0,ObjectsCreated=0)
        self.report['NativeOrbitalReferences']['SaveRootLifecycle']=lifecycle
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True))
        lifecycle.update(CompletedRootIds=[],AlreadySpawnedRoots=1)
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True))

    def test_later_registered_orbital_reference_cannot_hide_a_failed_or_fabricated_lifecycle(self):
        self.set_saved_colony()
        valid=dict(Source='existing-SaveLoadRoot.Spawn-before-OrbitalObject.OnSpawn',CompletedRootIds=[2],
            AlreadySpawnedRoots=0,Error='',ReferencesRewritten=0,ObjectsCreated=0)
        mutations=[('Source','direct registry insertion'),('Error','root registration failed'),
            ('ObjectsCreated',1),('ObjectsCreated',False),('ReferencesRewritten',1),('ReferencesRewritten',False),
            ('AlreadySpawnedRoots',-1),('AlreadySpawnedRoots',False),('CompletedRootIds',[2,2]),
            ('CompletedRootIds',[-1]),('CompletedRootIds',[2147483648]),('CompletedRootIds',[False]),
            ('CompletedRootIds',[2.0]),('CompletedRootIds',None)]
        for key,value in mutations:
            report=copy.deepcopy(self.report);receipt=copy.deepcopy(valid);receipt[key]=value
            report['NativeOrbitalReferences']['SaveRootLifecycle']=receipt
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,saved=True),(key,value))
        for key in valid:
            report=copy.deepcopy(self.report);receipt=copy.deepcopy(valid);receipt.pop(key)
            report['NativeOrbitalReferences']['SaveRootLifecycle']=receipt
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,saved=True),key)

    def test_restored_orbital_reference_admission_must_precede_positive_physical_time(self):
        self.set_restored_colony()
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        for key,value in [('RestoredBeforeAdvanceVerified',False),('RestoredPhysicalFrame',1),('RestoredPhysicalFrame',False),
                          ('RestoredPhysicalTime',1.4),('RestoredPhysicalTime',float('nan'))]:
            report=copy.deepcopy(self.report);report['NativeOrbitalReferences'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,restored=True),(key,value))

    def test_native_restore_lifecycle_hold_requires_the_unchanged_frame_zero_image(self):
        self.set_restored_colony()
        hold=dict(Source='waiting-for-native-KMonoBehaviour.Start-before-positive-time',Required=True,
            WaitInspections=1,PendingSaveRoots=0,MaximumPendingSaveRoots=10,PhysicalStateVerified=True,
            PhysicalFrame=0,PhysicalTime=self.report['ColonyCheckpoint']['SourceTime'],PhysicalSha256='a'*64)
        self.report['NativeOrbitalReferences']['RestoreLifecycleHold']=hold
        self.report['NativeOrbitalReferences']['Inspections']=2
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        for key,value in [('Source','manually registered objects'),('Required',False),('WaitInspections',False),
            ('WaitInspections',-1),('PendingSaveRoots',1),('PendingSaveRoots',False),('MaximumPendingSaveRoots',0),
            ('MaximumPendingSaveRoots',True),('PhysicalStateVerified',False),('PhysicalFrame',1),('PhysicalFrame',False),
            ('PhysicalTime',float('nan')),('PhysicalTime',9),('PhysicalTime',True),('PhysicalSha256','b'*64)]:
            changed=copy.deepcopy(self.report);changed['NativeOrbitalReferences']['RestoreLifecycleHold'][key]=value
            self.assertFalse(runner.native_domain_passed(changed,self.observation,30,restored=True),(key,value))
        for key in hold:
            changed=copy.deepcopy(self.report);changed['NativeOrbitalReferences']['RestoreLifecycleHold'].pop(key)
            self.assertFalse(runner.native_domain_passed(changed,self.observation,30,restored=True),key)

    def test_unused_native_restore_lifecycle_hold_cannot_claim_a_physical_witness(self):
        self.set_saved_colony()
        hold=dict(Source='waiting-for-native-KMonoBehaviour.Start-before-positive-time',Required=False,
            WaitInspections=0,PendingSaveRoots=0,MaximumPendingSaveRoots=0,PhysicalStateVerified=False,
            PhysicalFrame=0,PhysicalTime=0,PhysicalSha256='')
        self.report['NativeOrbitalReferences']['RestoreLifecycleHold']=hold
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True))
        for key,value in [('Required',True),('WaitInspections',1),('MaximumPendingSaveRoots',1),
            ('PhysicalStateVerified',True),('PhysicalTime',1),('PhysicalSha256','a'*64)]:
            changed=copy.deepcopy(self.report);changed['NativeOrbitalReferences']['RestoreLifecycleHold'][key]=value
            self.assertFalse(runner.native_domain_passed(changed,self.observation,30,saved=True),(key,value))

    def test_live_actor_save_and_restore_require_current_objects_callbacks_and_snapshot_identity(self):
        self.set_persistent_actors()
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True,persistent_actors=True))
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,persistent_actors=True))
        self.set_persistent_actors(restored=True)
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.report['Platform']='WindowsPlayer'
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True,expected_platform='WindowsPlayer'))

    def test_actor_mutations_cannot_hide_behind_exact_fluid_continuation(self):
        changes=[lambda a:a.update(PendingRestores=1),lambda a:a.update(UnsavedActors=1),
                 lambda a:a.update(RestoredBindingsVerified=False),lambda a:a.update(RestoredContinuationVerified=False),
                 lambda a:a.update(CurrentImageSha256='c'*64),lambda a:a['Native'].update(LiveActors=2),
                 lambda a:a['Native'].update(Session=2),lambda a:a['Native'].update(CompletedFrame=29),
                 lambda a:a['CurrentActors'][0].update(Remainder=-1),lambda a:a['CurrentActors'][0].update(Mass=float('nan')),
                 lambda a:a['CurrentActors'][1].update(Owner=101,Kind=1),lambda a:a['SavedActors'][0].update(Prefab=1),
                 lambda a:a['BoundObjects'][0].update(Handle=-1),lambda a:a['BoundObjects'][0].update(UnityInstance=0),
                 lambda a:a['BoundObjects'][0].update(PersistentConsumerCallbacks=21),
                 lambda a:a['BoundObjects'][0].update(PersistentConsumerMass=.19),
                 lambda a:a['BoundObjects'][0].update(PersistentOwner=999),
                 lambda a:a['BoundObjects'][2].update(EmitterBlocked=False),
                 lambda a:a['BoundObjects'].pop()]
        self.set_persistent_actors(restored=True)
        for change in changes:
            report=copy.deepcopy(self.report);change(report['PersistentActors'])
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,restored=True))

    def test_requested_actor_save_cannot_pass_using_an_actor_free_or_legacy_report(self):
        self.set_saved_colony()
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,saved=True,persistent_actors=True))
        self.report['PersistentActors']=dict(Required=False,PendingRestores=0,UnsavedActors=0,SavedActors=None,CurrentActors=None,BoundObjects=[])
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True))
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,saved=True,persistent_actors=True))
        self.report['PersistentActors']['BoundObjects']=[{}]
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,saved=True))

    def test_actual_colony_save_requires_one_paused_paired_section_and_source_clock(self):
        self.set_saved_colony()
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True))
        for key,value in [('Saved',False),('Loaded',True),('WholeColonyFluidCheckpoint',True),('SimSaveWrites',2),('SourceFrame',29),
                          ('SourceTime',0),('SourcePhysicalSha256',''),('SavedFileSha256',''),('SavedFile',''),('AutomaticSaveAdmissions',0),('SavePauseStateRestored',False),
                          ('SavePauseDepthBefore',-1),('SavePauseDepthBefore',False),('SavePauseDepthAfter',None),('SavePauseDepthAfter',1),
                          ('SavedFileReadbackVerified',False),('SerializedBodyCaptures',0),('SerializedBodyCaptures',2),
                          ('SerializedBodyBytes',0),('SerializedBodySha256',''),('SaveProblems',[]),('SerializedHeaderCaptures',0),('SerializedHeaderSha256','')]:
            report=copy.deepcopy(self.report);report['ColonyCheckpoint'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,saved=True),key)

    def set_natural_autosave(self):
        self.set_saved_colony()
        self.report.update(RenderedRevision=3300,Publications=3305)
        self.report['NativeDomain'].update(CompletedFrames=3300,NativeTicket=4000,Substeps=69300,TimeSeconds=660.0000098347664)
        self.report['ColonyCheckpoint'].update(SourceFrame=2990,SourceTime=598.0000089108944,SavedFrame=2990,
            SavedTime=598.0000089108944,SavedPhysicalSha256='a'*64,SavedAsAutoSave=True)
        self.report['ColonyClock']=dict(Cycle=1,NativeGameSeconds=662,AutoSaveCycleInterval=1)
        self.report['NaturalAutosave']=dict(Required=True,Configured=True,Verified=True,Source='GameClock.DoAutoSave',Requests=1,
            RequestedCycle=1,SavedNativeCycle=1,SavedNativeGameSeconds=600.03,ExactContinuationFrames=310,
            ExpectedPhysicalSha256='e'*64,CurrentPhysicalSha256='e'*64)

    def test_natural_day_rollover_requires_actual_autosave_and_exact_later_continuation(self):
        self.set_natural_autosave()
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,3300,natural_autosave=True))
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,3300,saved=True))
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,natural_autosave=True))
        self.report['Platform']='WindowsPlayer'
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,3300,natural_autosave=True,expected_platform='WindowsPlayer'))

    def test_live_actor_day_save_compares_the_later_complete_image_and_allows_exhausted_local_oxygen(self):
        self.set_persistent_actors();actors=copy.deepcopy(self.report['PersistentActors'])
        self.set_natural_autosave();self.report['PersistentActors']=actors
        actors['Native']['CompletedFrame']=3300
        actors['CurrentImageSha256']='e'*64
        actors['SavedActors'][0]['Remainder']=0
        actors['CurrentActors'][0]['Remainder']=0
        self.report['NativeDomain']['CurrentOxygenKg']=0
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,3300,natural_autosave=True,persistent_actors=True))
        actors['CurrentImageSha256']='a'*64
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,3300,natural_autosave=True,persistent_actors=True))
        actors['CurrentImageSha256']='e'*64
        actors['BoundObjects'][0]['PersistentConsumerCallbacks']+=1
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,3300,natural_autosave=True,persistent_actors=True))

    def test_natural_autosave_rejects_manual_calls_missing_rollover_and_lost_intervals(self):
        for key,value in [('Verified',False),('Source','manual-save'),('Requests',2),('RequestedCycle',0),
                          ('SavedNativeGameSeconds',599),('ExactContinuationFrames',309),('CurrentPhysicalSha256','f'*64)]:
            self.set_natural_autosave();self.report['NaturalAutosave'][key]=value
            self.assertFalse(runner.native_domain_passed(self.report,self.observation,3300,natural_autosave=True),key)
        for key,value in [('SavedAsAutoSave',False),('SavedFrame',3300),('SavedPhysicalSha256','f'*64),('SavePauseStateRestored',False)]:
            self.set_natural_autosave();self.report['ColonyCheckpoint'][key]=value
            self.assertFalse(runner.native_domain_passed(self.report,self.observation,3300,natural_autosave=True),key)

    def test_declined_disk_write_requires_cleanup_resumed_frames_and_a_later_verified_save(self):
        self.set_saved_colony();colony=self.report['ColonyCheckpoint'];colony['AutomaticSaveAdmissions']=2
        colony['SaveProblems']={'IoControlRequired':True,'IoControlVerified':True,'AdvancedAfterIoControl':True,
            'UnsuccessfulWrites':1,'DeclinedBeforeSerialization':1,'IoControlFrame':8,'NativeIoDialogObserved':True,
            'DraggingRestored':True,'IoControlStage':3,'BeforePhysicalSha256':'f'*64,'AfterPhysicalSha256':'f'*64,
            'PreCallbacks':1,'ActiveCallbacks':1,'PostCallbacks':1}
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,saved=True))
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,saved=True,save_io_recovery=True))
        for key,value in [('IoControlRequired',False),('IoControlVerified',False),('AdvancedAfterIoControl',False),
                          ('UnsuccessfulWrites',0),('UnsuccessfulWrites',True),('DeclinedBeforeSerialization',0),('IoControlFrame',30),('IoControlFrame',0),
                          ('NativeIoDialogObserved',False),('DraggingRestored',False),('IoControlStage',2),('AfterPhysicalSha256','e'*64),
                          ('PreCallbacks',0),('ActiveCallbacks',0),('PostCallbacks',0)]:
            report=copy.deepcopy(self.report);report['ColonyCheckpoint']['SaveProblems'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,saved=True,save_io_recovery=True),key)
        colony['AutomaticSaveAdmissions']=1
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,saved=True,save_io_recovery=True))

    def set_restored_colony(self):
        self.report['NativeFlowControlObserved']=False
        self.report['ColonyCheckpoint']={'Saved':False,'Loaded':True,'WholeColonyFluidCheckpoint':False,'ZeroTimeRestoreVerified':True,
            'ManagedSceneRestored':True,'RendererVerified':True,'NativeStartupBatchesDelivered':2,'SourceFrame':31,'SourceTime':1.2,'SourcePhysicalSha256':'a'*64,
            'RestoredInitialSha256':'a'*64,'CurrentPhysicalSha256':'b'*64,'ExactContinuationFrames':30,'ReplayedFrame':30,
            'ReplayedTime':self.report['NativeDomain']['TimeSeconds']}
        self.set_orbital_references(restored=True)

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

    def set_restored_autosave(self):
        self.set_restored_colony()
        colony=self.report['ColonyCheckpoint']
        colony.update(Saved=True,AutomaticSaveAdmissions=1,SimSaveWrites=1,SavePauseStateRestored=True,
            SavePauseDepthBefore=2,SavePauseDepthAfter=2,SavedFile='private/resaved-colony.sav',SavedFileSha256='c'*64,
            SavedFrame=0,SavedTime=colony['SourceTime'],SavedPhysicalSha256=colony['SourcePhysicalSha256'],
            FrameZeroResaveRequired=True,FrameZeroResaveVerified=True,FrameZeroResaveUsedGameDelayedSave=True,SavedAsAutoSave=True,SavedFileReadbackVerified=True,
            InitialPauseChallengeVerified=True,InitialPositiveTicksHeld=1,
            SerializedHeaderCaptures=1,SerializedHeaderSha256='f'*64,
            SerializedBodyCaptures=1,SerializedBodyBytes=160000,SerializedBodySha256='e'*64)

    def set_restored_readers(self,liquid=False):
        self.set_restored_autosave();pressure=self.pressure_reader(liquid)['NativePressureReader']
        pressure['DistinctFromLegacyScalarRequired']=False;self.report['NativePressureReader']=pressure
        source=self.breathing_reader()['BreathingCellReader'];samples=source['Before'];selection=source['SelectionBefore']
        for row in samples:row['CompletedPhysicalFrame']=30
        self.report['Scenario']='compressed-liquid' if liquid else 'mixed-air'
        if liquid:
            self.report['NativeDomain'].update(LiquidMassKg=1100,PressurePa=220000000,SuppressedLiquid=100,InitialOxygenKg=0,CurrentOxygenKg=0)
            for row in samples:row.update(OxygenMassKg=0,ScalarGridMassKg=1100,OxygenMoleFraction=0,OxygenPartialPressurePa=0,
                AdvertisedOxygenMassKg=0,ScalarElement=2,AdvertisedElement=0,Failures=1,Breathable=False)
            selection.update(ElementID=0,Mass=0,IsBreathable=False,TotalBreathableMass=0)
        self.report['RestoredNativeReaders']=dict(Required=True,Verified=True,NativeBreathingOutsideVerified=True,
            Source='actual-native-pressure-and-world-breathing-readers',BreathingReads=12,
            OxygenElementId=1,WaterElementId=2,VacuumElementId=0,Policy=source['Policy'],BreathingSamples=samples,BreathingSelection=selection)

    def test_restored_readers_require_current_session_pressure_and_native_breathing_selection(self):
        for liquid in (False,True):
            for platform in ('LinuxPlayer','WindowsPlayer'):
                self.set_restored_readers(liquid);self.report['Platform']=platform
                self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True,expected_platform=platform))

    def test_restored_reader_failures_cannot_hide_behind_exact_physical_continuation(self):
        for key,value in [('Verified',False),('BreathingReads',11),('NativeBreathingOutsideVerified',False),('Source','fixture')]:
            self.set_restored_readers();self.report['RestoredNativeReaders'][key]=value
            self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True),key)
        for key,value in [('CompletedPhysicalFrame',29),('AdvertisedOxygenMassKg',1.125),('Failures',4),('PublicCell',999)]:
            self.set_restored_readers();self.report['RestoredNativeReaders']['BreathingSamples'][-1][key]=value
            self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True),key)
        self.set_restored_readers(True);self.report['RestoredNativeReaders']['BreathingSamples'][-1]['OxygenMassKg']=.1
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.set_restored_readers();self.report['NativePressureReader']['CallSitePatchesVerified']=False
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True))

    def test_restored_frame_zero_resave_preserves_clock_state_and_nested_pause_depth(self):
        self.set_restored_autosave();colony=self.report['ColonyCheckpoint']
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        for key,value in [('SimSaveWrites',2),('AutomaticSaveAdmissions',2),('SavePauseDepthAfter',3),
                          ('SavePauseDepthAfter',1),('FrameZeroResaveVerified',False),('SavedAsAutoSave',False),('SavedFrame',1),
                          ('SavedFileReadbackVerified',False),('SerializedBodyCaptures',2),('FrameZeroResaveUsedGameDelayedSave',False),
                          ('InitialPauseChallengeVerified',False),('InitialPositiveTicksHeld',0),
                          ('SavedTime',colony['SourceTime']+.2),('SavedPhysicalSha256','d'*64)]:
            report=copy.deepcopy(self.report);report['ColonyCheckpoint'][key]=value
            self.assertFalse(runner.native_domain_passed(report,self.observation,30,restored=True),key)

    def test_same_process_reload_requires_retired_session_menu_exact_milestone_and_both_autosaves(self):
        self.set_restored_autosave()
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True,reload_in_process=True))
        self.report['NativeDomain']['Session']=2
        self.report['InProcessReload']={'Requested':True,'Verified':True,'Stage':3,'WholeColonyFluidAuthority':False,
            'ShutdownObserved':True,'MainMenuObserved':True,'MatchingFrameVerified':True,'FirstAutosaveVerified':True,'InputUnchanged':True,
            'FirstProcessId':4242,'CurrentProcessId':4242,'FirstNativeSession':1,'CurrentNativeSession':2,'MatchedSession':2,
            'FirstCompletedFrame':30,'FirstPhysicalSha256':'a'*64,'FirstReportSha256':'b'*64,'InputSha256':'c'*64}
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True,reload_in_process=True))
        for key,value in [('Verified',False),('Stage',2),('ShutdownObserved',False),('MainMenuObserved',False),
                ('MatchingFrameVerified',False),('FirstAutosaveVerified',False),('InputUnchanged',False),('CurrentProcessId',4243),
                ('FirstNativeSession',2),('MatchedSession',1),('CurrentNativeSession',1),('FirstCompletedFrame',31),
                ('FirstCompletedFrame',29),('FirstReportSha256',''),('InputSha256','x'*64)]:
            bad=copy.deepcopy(self.report);bad['InProcessReload'][key]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,restored=True,reload_in_process=True),key)
        self.report['ColonyCheckpoint']['FrameZeroResaveRequired']=False
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True,reload_in_process=True))

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
        water=copy.deepcopy(self.report)
        water["NativeDomain"].update(LiquidMassKg=1100.0,PressurePa=2.2e8,SuppressedLiquid=100,InitialOxygenKg=0,CurrentOxygenKg=0)
        water.update(LiquidHeatVerified=True,HeatEnergyScaleJoules=2e11,HeatEnergyBoundJoules=128*2.2204460492503131e-16*2e11,
            HeatEnergyResidualJoules=.0002)
        water["HeatCommands"]["CommittedHeatJoules"]=1500000
        self.assertTrue(runner.native_domain_passed(water,self.observation,30,liquid_heat=True))
        for section,key,value in ((None,"LiquidHeatVerified",False),(None,"HeatEnergyBoundJoules",1),
                (None,"HeatEnergyResidualJoules",.01),(None,"HeatEnergyScaleJoules",float("inf")),
                ("HeatCommands","CommittedHeatJoules",1500),("ThermalConduction","EntropyChange",0),
                ("NativeDomain","SuppressedLiquid",0)):
            bad=copy.deepcopy(water);(bad[section] if section else bad)[key]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,liquid_heat=True),(section,key))

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

    def test_radius_consumption_requires_one_real_callback_exceeding_the_centre_inventory(self):
        self.add_gas_transfers()
        self.report.update(TargetCell=100,RadialConsumptionMessagesRouted=1)
        injected=struct.unpack('f',struct.pack('f',.05))[0];consumed=struct.unpack('f',struct.pack('f',.4))[0]
        self.report['GasTransfers']['MassChangesKg']=[0,injected-consumed,0,0,0]
        self.report['ConsumptionResult'].update(mass=.4,callbackIdx=71)
        self.report['RadialConsumption']=dict(Required=True,Verified=True,Source='actual-SimMessages.ConsumeMass-radius-3',Radius=3,
            PublicCell=100,SourceFrame=4,SourceCellOxygenKg=.2,InjectedOxygenKg=injected,RequestedMassKg=consumed,
            Receipts=1,SingleCallbackForMultipleSourceCells=True,ActualDuplicantMetabolismVerified=False,
            Receipt=dict(Session=1,Kind=2,Species=1,PublicCell=100,MassKg=consumed,CompletedFrame=5,Callback=71))
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,radial_consumption=True))
        changes=[lambda r:r['RadialConsumption'].update(SourceCellOxygenKg=.5),
            lambda r:r['RadialConsumption'].update(Receipts=2),lambda r:r['RadialConsumption'].update(ActualDuplicantMetabolismVerified=True),
            lambda r:r['RadialConsumption']['Receipt'].update(Callback=72),lambda r:r['RadialConsumption']['Receipt'].update(MassKg=.02),
            lambda r:r['RadialConsumption'].update(SourceFrame=5),lambda r:r.update(RadialConsumptionMessagesRouted=0)]
        for change in changes:
            bad=copy.deepcopy(self.report);change(bad)
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,radial_consumption=True))

    def test_rectangular_consumption_requires_the_actual_footprint_callback_and_complete_transfer_account(self):
        self.add_gas_transfers();self.report.update(TargetCell=100,Width=3,Height=2)
        injected=struct.unpack('f',struct.pack('f',.05))[0];consumed=struct.unpack('f',struct.pack('f',.4))[0]
        self.report['GasTransfers']['MassChangesKg']=[0,injected-consumed,0,0,0]
        self.report['ConsumptionResult'].update(mass=.4,callbackIdx=71)
        self.report['RectangularConsumption']=dict(Required=True,Verified=True,AbiVersion=1,
            Source='actual-SimMessages.ConsumeMass-width-height',Order='bottom-to-top-left-to-right',Width=3,Height=2,
            PublicCell=100,SourceFrame=4,SourceCellOxygenKg=.2,InjectedOxygenKg=injected,RequestedMassKg=consumed,
            MessagesRouted=1,Receipts=1,SingleCallbackForMultipleSourceCells=True,WholeColonyDeviceConsumption=False,
            Receipt=dict(Session=1,Kind=2,Species=1,PublicCell=100,MassKg=consumed,CompletedFrame=5,Callback=71))
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,rectangular_consumption=True))
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30))
        changes=[lambda r:r['RectangularConsumption'].update(SourceCellOxygenKg=.5),
            lambda r:r['RectangularConsumption'].update(Receipts=2),lambda r:r['RectangularConsumption'].update(Width=2),
            lambda r:r['RectangularConsumption'].update(Order='breadth-first'),lambda r:r.update(Width=2),
            lambda r:r['RectangularConsumption']['Receipt'].update(Callback=72),lambda r:r['RectangularConsumption']['Receipt'].update(MassKg=.02),
            lambda r:r['RectangularConsumption'].update(SourceFrame=5),lambda r:r['RectangularConsumption'].update(MessagesRouted=0),
            lambda r:r['RectangularConsumption'].update(WholeColonyDeviceConsumption=True),lambda r:r.update(ConsumptionCallbacks=2)]
        for change in changes:
            bad=copy.deepcopy(self.report);change(bad)
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,rectangular_consumption=True))

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

    def add_liquid_transfers(self):
        self.add_gas_transfers()
        self.report.update(Scenario="compressed-liquid", LiquidTransfersVerified=True,
            TransferEnergyScaleJoules=2e11, TransferEnergyBoundJoules=128*2.2204460492503131e-16*2e11,
            TransferEnergyResidualJoules=-.0002,
            LiquidInjectionReceipt={"Session":1,"CompletedFrame":5,"Species":3,"Kind":1,
                "MassKg":.05000000074505806,"InternalEnergy":-792428,"KineticEnergy":0},
            LiquidExtractionReceipt={"Session":1,"CompletedFrame":6,"Species":3,"Kind":2,
                "MassKg":.019999999552965164,"InternalEnergy":-316971,"KineticEnergy":.001})
        self.report["NativeDomain"].update(LiquidMassKg=1100.005,PressurePa=220010000,SuppressedLiquid=100)
        self.report["FluidTransfers"]=copy.deepcopy(self.report["GasTransfers"])
        self.report["FluidTransfers"]["MassChangesKg"]=[0,0,0,.030000001192092896,0]
        self.report["FluidTransfers"]["MaterialEnergyChange"]=-475457

    def test_liquid_transfers_require_water_receipts_later_extraction_and_roundoff_account(self):
        self.add_liquid_transfers()
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,liquid=True,liquid_transfers=True))
        for section,key,value in ((None,"LiquidTransfersVerified",False),(None,"TransferEnergyBoundJoules",1),
                (None,"TransferEnergyScaleJoules",float("inf")),(None,"TransferEnergyResidualJoules",.01),
                ("LiquidExtractionReceipt","CompletedFrame",5),("LiquidExtractionReceipt","Species",1),
                ("LiquidExtractionReceipt","Session",2),("LiquidExtractionReceipt","KineticEnergy",0),
                ("LiquidInjectionReceipt","InternalEnergy",float("nan")),("LiquidInjectionReceipt","MassKg",.05),
                ("FluidTransfers","MassChangesKg",[0,.030000001192092896,0,0,0]),
                ("FluidTransfers","UnacknowledgedReceipts",1),("ConsumptionResult","elemIdx",42)):
            bad=copy.deepcopy(self.report)
            (bad[section] if section else bad)[key]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,liquid=True,liquid_transfers=True),(section,key))

    def test_liquid_gravity_requires_reference_energy_potential_and_water_receipts(self):
        self.add_liquid_transfers()
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,liquid_gravity=True))
        self.report.update(Scenario="compressed-liquid-gravity",LiquidGravityVerified=True,GravityVerified=True,
            EnergyRepresentation="water-anomaly-about-300K-plus-original-reference-from-mass",ProjectedPotentialJoules=64746.2,
            Gravity={"Size":96,"Version":1,"State":1,"Enabled":1,"Session":1,"CompletedFrame":30,
                "Gravity":9.81,"Potential":64746.2,"InitialPotential":64746.0,"PendingBoundaryPotential":0,
                "CommittedBoundaryPotential":.030000001192092896*9.81*.5,"BodyImpulseY":-70000,
                "InternalWork":.01,"MaximumEnergyResidual":.00025})
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,liquid_gravity=True))
        for section,key,value in ((None,"LiquidGravityVerified",False),(None,"EnergyRepresentation","full-internal-energy"),
                (None,"Scenario","compressed-liquid"),(None,"TransferEnergyBoundJoules",1),
                ("Gravity","MaximumEnergyResidual",.01),("Gravity","CommittedBoundaryPotential",0),
                ("NativeDomain","SuppressedLiquid",0),("LiquidExtractionReceipt","Species",1)):
            bad=copy.deepcopy(self.report);(bad[section] if section else bad)[key]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,liquid_gravity=True),(section,key))

    def test_loaded_water_transfer_state_requires_exact_saved_continuation(self):
        self.set_restored_colony()
        self.report["Scenario"]="compressed-liquid"
        self.report["NativeDomain"].update(LiquidMassKg=1100.005,PressurePa=220010000,SuppressedLiquid=100,
            InitialOxygenKg=0,CurrentOxygenKg=0)
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.report["ColonyCheckpoint"]["ExactContinuationFrames"]-=1
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True))

    def test_loaded_water_gravity_requires_its_energy_basis_and_exact_replay(self):
        self.set_restored_colony()
        self.report["Scenario"]="compressed-liquid-gravity"
        self.report["NativeDomain"].update(LiquidMassKg=1100.005,PressurePa=220010000,SuppressedLiquid=100,
            InitialOxygenKg=0,CurrentOxygenKg=0)
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.report["EnergyRepresentation"]="water-anomaly-about-300K-plus-original-reference-from-mass"
        self.assertTrue(runner.native_domain_passed(self.report,self.observation,30,restored=True))
        self.report["ColonyCheckpoint"]["ExactContinuationFrames"]-=1
        self.assertFalse(runner.native_domain_passed(self.report,self.observation,30,restored=True))

    def test_registered_actors_require_actual_components_callbacks_and_removal(self):
        self.assertFalse(runner.native_domain_passed(self.report, self.observation, 30, actors=True))
        self.report["RegisteredActors"] = {"Verified": True, "Stage": 8, "Native": {"LiveActors": 0,
            "ConsumerSamples": 16, "EmitterSamples": 24}, "ActualConsumerCallbacks": 16, "ConsumerPublications": 16,
            "EmitterPublications": 24, "RemovalConsumerSamples": 16, "RemovalEmitterSamples": 24, "RemovedAtFrame": 20,
            "EmitterGameStatusObserved": True, "ActualEmitterRegistrations": 1, "ActualBlockedCallbacks": 1,
            "ConfigurationBatchVerified": True, "NativeActorBatchVerified": True, "ConfigurationBatchesRouted": 1,
            "RegistrationBatchVerified":True,"RetirementBatchVerified":True,"ActualBatchEmitterRegistrations":1,
            "RegistrationBatchesRouted":1,"RetirementBatchesRouted":1,
            "InitialZeroEmitterVerified":True,"InitialZeroEmitterMass":0,"InitialZeroEmitterSamples":8,
            "ActualUnblockedCallbacks": 1, "EnergyResidualJoules": 1e-9, "MaximumRelativeMassError": 1e-15}
        self.assertTrue(runner.native_domain_passed(self.report, self.observation, 30, actors=True))
        for field, value in (("ActualConsumerCallbacks", 0), ("ActualBlockedCallbacks", 2), ("ActualUnblockedCallbacks", 0),
                ("EmitterGameStatusObserved", False), ("RemovalEmitterSamples", 23), ("RemovedAtFrame", 28),
                ("EnergyResidualJoules", float("nan")), ("MaximumRelativeMassError", 1e-8), ("Stage", 6)):
            bad = copy.deepcopy(self.report); bad["RegisteredActors"][field] = value
            self.assertFalse(runner.native_domain_passed(bad, self.observation, 30, actors=True), field)
        for field in ("ConfigurationBatchVerified", "NativeActorBatchVerified", "ConfigurationBatchesRouted",
                      "RegistrationBatchVerified", "RetirementBatchVerified", "ActualBatchEmitterRegistrations",
                      "RegistrationBatchesRouted", "RetirementBatchesRouted"):
            bad=copy.deepcopy(self.report);bad["RegisteredActors"].pop(field)
            self.assertFalse(runner.native_domain_passed(bad, self.observation, 30, actors=True), field)
        for key,value in (("InitialZeroEmitterVerified",False),("InitialZeroEmitterMass",.01),("InitialZeroEmitterMass",False),("InitialZeroEmitterSamples",0),
                          ("InitialZeroEmitterSamples",True)):
            bad=copy.deepcopy(self.report);bad["RegisteredActors"][key]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,actors=True),key)

        water=copy.deepcopy(self.report)
        water["NativeDomain"].update(LiquidMassKg=1100.1,PressurePa=220010000,SuppressedLiquid=100,InitialOxygenKg=0,CurrentOxygenKg=0)
        water["Scenario"]="compressed-liquid"
        water["RegisteredActors"].update(LiquidActors=True,MaterialId="water",ActualConsumerMass=.3,ActualEmitterMass=.5,
            EnergyScaleJoules=2e11,EnergyBoundJoules=128*2.2204460492503131e-16*2e11,EnergyResidualJoules=.0002)
        water["FluidTransfers"]={"MassChangesKg":[0,0,0,.2,0]}
        self.assertTrue(runner.native_domain_passed(water,self.observation,30,liquid=True,liquid_actors=True))
        for section,key,value in (("RegisteredActors","MaterialId","oxygen"),("RegisteredActors","LiquidActors",False),
                ("RegisteredActors","EnergyBoundJoules",1),("RegisteredActors","EnergyResidualJoules",.01),
                ("RegisteredActors","ActualConsumerMass",.31),("RegisteredActors","ActualEmitterMass",float("nan")),
                ("FluidTransfers","MassChangesKg",[0,.2,0,0,0]),("RegisteredActors","RemovalConsumerSamples",15)):
            bad=copy.deepcopy(water);bad[section][key]=value
            self.assertFalse(runner.native_domain_passed(bad,self.observation,30,liquid=True,liquid_actors=True),(section,key))


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

    def test_failure_words_in_evidence_paths_do_not_turn_success_messages_into_errors(self):
        for path in ('/tmp/original-failed-live/mods/MixedAtmospheres.IntegrationProbe/report.json',
                     r'C:\captures\original-failed-live\mods\MixedAtmospheres.IntegrationProbe\report.json',
                     '/tmp/error/MixedAtmospheres.IntegrationProbe/Exception/report.json'):
            log='[Mixed Atmospheres] Integration probe completed. Report: '+path
            self.assertEqual(runner.mod_failure_lines(log),[])
            self.assertTrue(runner.mod_failure_lines('[Mixed Atmospheres] Integration probe failed. Report: '+path))
            self.assertTrue(runner.mod_failure_lines('[Mixed Atmospheres] System.InvalidOperationException: sentinel at '+path))

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


class PrivateDisplayTests(unittest.TestCase):
    def test_window_manager_cleanup_failure_still_retires_the_private_xserver(self):
        wm={'pid':221};display={'pid':222,'window_manager':wm}
        def stop(record):
            if record is wm:raise OSError('window manager cleanup')
        with mock.patch.object(runner,'stop_process',side_effect=stop) as cleanup:
            with self.assertRaisesRegex(OSError,'window manager cleanup'):
                runner.stop_private_display(display)
        self.assertEqual(cleanup.call_args_list,[mock.call(wm),mock.call(display)])

    def test_default_display_keeps_the_shared_steam_session_and_does_not_retire_it(self):
        state=Path('/srv/oni-state');session={'display':':121','steam':{'pid':111},'xvfb':{'pid':112}}
        with mock.patch.object(runner,'stop_process') as stop:
            with runner.run_display(state,session,Path('/srv/run')) as (screen,env,record):
                self.assertIs(screen,session);self.assertEqual(env['DISPLAY'],':121')
                self.assertEqual(env['XAUTHORITY'],'/srv/oni-state/Xauthority');self.assertIsNone(record)
        stop.assert_not_called()

    def test_private_display_retires_only_its_own_process_when_game_launch_fails(self):
        session={'display':':121','steam':{'pid':111},'xvfb':{'pid':112}};record={'pid':222}
        with mock.patch.object(runner,'start_private_display',return_value=(record,':123',{'DISPLAY':':123'})), \
             mock.patch.object(runner,'stop_process') as stop:
            with self.assertRaisesRegex(RuntimeError,'game launch'):
                with runner.run_display(Path('/srv/state'),session,Path('/srv/run'),True) as (screen,env,owned):
                    self.assertEqual(screen['display'],':123');self.assertEqual(screen['xauthority'],'/srv/run/Xauthority')
                    self.assertIs(owned,record);self.assertEqual(env['DISPLAY'],':123')
                    raise RuntimeError('game launch failed')
        stop.assert_called_once_with(record);self.assertEqual(session['display'],':121')
        self.assertNotIn('xauthority',session)

    def test_early_xserver_failure_cleans_up_only_the_new_display_and_uses_private_auth(self):
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary);record={'pid':222}
            with mock.patch.object(runner,'spawn',return_value=record) as spawn, \
                 mock.patch.object(runner,'alive',return_value=False),mock.patch.object(runner,'stop_process') as stop, \
                 mock.patch.object(runner.subprocess,'run'):
                with self.assertRaisesRegex(RuntimeError,'Xvfb exited'):
                    runner.start_private_display(Path('/srv/state'),output)
            stop.assert_called_once_with(record)
            command=spawn.call_args.args[0]
            self.assertEqual(command[command.index('-auth')+1],str(output/'Xauthority'))
            self.assertEqual((output/'Xauthority').stat().st_mode & 0o777,0o600)
            self.assertEqual(spawn.call_args.args[2]['XAUTHORITY'],str(output/'Xauthority'))


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

    def test_archive_runner_cannot_inherit_its_enclosing_repositories_revision(self):
        subprocess.run(["git","init","-q",str(self.root)],check=True,capture_output=True)
        subprocess.run(["git","-C",str(self.root),"-c","user.name=Fixture","-c","user.email=fixture@invalid",
                        "commit","-q","--allow-empty","-m","enclosing checkout"],check=True,capture_output=True)
        archived=self.root/"frozen-runner";archived.mkdir()
        with mock.patch.object(runner,"ROOT",archived):
            fields=runner.revision_fields(self.manifest)
        self.assertIsNone(fields["runner_git_revision"])
        self.assertIsNone(fields["runner_git_status"])
        self.assertEqual(fields["source_revision"],self.manifest["source_revision"])

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
                         frames=1, timeout=30, keep_open=True, inspection_seconds=0, load_save=None, mod_id=None, save_colony=False, resave_before_resume=False)
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
