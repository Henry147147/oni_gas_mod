#!/usr/bin/env python3
"""Portable acceptance for bounded native-domain Unity evidence.

This checks completed game reports; isolated DLL or Wine reports do not pass it.
It never promotes bounded scenes to a playable or whole-colony result.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import struct

def _sha256(value) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(char in "0123456789abcdef" for char in value)

def native_domain_passed(report: dict | None, observation: dict | None, frames: int, liquid: bool = False, heat: bool = False, transfers: bool = False, actors: bool = False, carbon: bool = False, expected_platform: str = "LinuxPlayer", conduction: bool = False, gravity: bool = False, checkpoint: bool = False, paired: bool = False, saved: bool = False, restored: bool = False, save_io_recovery: bool = False, liquid_transfers: bool = False, liquid_actors: bool = False, liquid_heat: bool = False, reload_in_process: bool = False, liquid_gravity: bool = False) -> bool:
    """Accept bounded physical ownership only; never promote it to a playable colony."""
    if not isinstance(report, dict) or not isinstance(observation, dict):
        return False
    if reload_in_process and not restored:
        return False
    if save_io_recovery and (not saved or restored):
        return False
    gravity = gravity or liquid_gravity
    liquid_transfers = liquid_transfers or liquid_gravity
    liquid = liquid or liquid_transfers or liquid_actors or liquid_heat
    if restored:
        liquid = report.get("Scenario") in ("compressed-liquid", "compressed-liquid-gravity")
        if (report.get("Scenario") == "compressed-liquid-gravity" and
                report.get("EnergyRepresentation") != "water-anomaly-about-300K-plus-original-reference-from-mass"):
            return False
    physical = report.get("NativeDomain")
    observed = observation.get("Observation")
    if not isinstance(physical, dict) or not isinstance(observed, dict):
        return False
    if ((liquid or liquid_transfers or liquid_actors) and
            (type(physical.get("SuppressedLiquid")) is not int or physical["SuppressedLiquid"] <= 0)):
        return False
    counters = ("Session", "CompletedFrames", "NativeTicket", "Substeps", "SolverThread", "StartThread")
    if any(type(physical.get(key)) is not int or physical[key] <= 0 for key in counters):
        return False
    bounds = (("MaximumRelativeMassError", 1e-8), ("MaximumRelativeEnergyError", 1e-6))
    if any(type(physical.get(key)) not in (int, float) or not 0 <= physical[key] <= limit for key, limit in bounds):
        return False
    values = ("TimeSeconds", "LastTimestepSeconds", "InitialOxygenKg", "CurrentOxygenKg", "PressurePa", "LiquidMassKg")
    if any(type(physical.get(key)) not in (int, float) or not math.isfinite(physical[key]) for key in values):
        return False
    phases = ("SuppressedPostProcess", "SuppressedTemperature")
    if any(type(physical.get(key)) is not int or physical[key] <= 0 for key in phases):
        return False
    if (type(report.get("RenderedRevision")) is not int or not 0 < report["RenderedRevision"] <= physical["CompletedFrames"]
            or type(report.get("Publications")) is not int or report["Publications"] < 2):
        return False
    primed = report.get("ProjectionPrimedAtNativeFrame")
    if type(primed) is not int or primed < 0 or physical["CompletedFrames"] - primed < frames:
        return False
    if heat or conduction or liquid_heat:
        account = report.get("HeatCommands")
        heat_bound = 1e-6
        if liquid_heat:
            scale = report.get("HeatEnergyScaleJoules")
            if (report.get("LiquidHeatVerified") is not True or type(scale) not in (int, float) or not 0 < scale < math.inf
                    or report.get("HeatEnergyBoundJoules") != 128 * 2.2204460492503131e-16 * scale):
                return False
            heat_bound = report["HeatEnergyBoundJoules"]
        if (not isinstance(account, dict) or report.get("HeatCommandsVerified") is not True or
                report.get("HeatAvoidedNativeDispatch") is not True or report.get("HeatPacketsRouted") != 2 or
                account.get("PendingBatches") != 0 or account.get("AcceptedBatches") != 2 or account.get("CommittedBatches") != 2 or
                account.get("CommittedHeatJoules") != (1500000 if liquid_heat else 1500) or
                type(report.get("HeatEnergyResidualJoules")) not in (int, float) or not abs(report["HeatEnergyResidualJoules"]) <= heat_bound):
            return False
    if conduction or liquid_heat:
        thermal = report.get("ThermalConduction")
        if (report.get("ThermalConductionVerified") is not True or not isinstance(thermal, dict)
                or thermal.get("Size") != 80 or thermal.get("Version") != 1 or thermal.get("Enabled") != 1 or thermal.get("State") != 1
                or thermal.get("Session") != physical["Session"] or thermal.get("CompletedFrame") != physical["CompletedFrames"]
                or thermal.get("Stages") != 2 * physical["Substeps"] or type(thermal.get("Contacts")) is not int or thermal["Contacts"] <= 0
                or type(thermal.get("Iterations")) is not int or thermal["Iterations"] <= 0
                or any(type(thermal.get(key)) not in (int, float) or not math.isfinite(thermal[key]) or thermal[key] <= 0
                       for key in ("RedistributedEnergy", "EntropyChange"))):
            return False
    if gravity:
        field = report.get("Gravity")
        gravity_bound = 1e-6
        if liquid_gravity:
            scale = report.get("TransferEnergyScaleJoules")
            if (report.get("LiquidGravityVerified") is not True or report.get("Scenario") != "compressed-liquid-gravity"
                    or report.get("EnergyRepresentation") != "water-anomaly-about-300K-plus-original-reference-from-mass"
                    or type(scale) not in (int, float) or not 0 < scale < math.inf
                    or report.get("TransferEnergyBoundJoules") != 128 * 2.2204460492503131e-16 * scale):
                return False
            gravity_bound = report["TransferEnergyBoundJoules"]
        if (report.get("GravityVerified") is not True or not isinstance(field, dict)
                or field.get("Size") != 96 or field.get("Version") != 1 or field.get("Enabled") != 1 or field.get("State") != 1
                or field.get("Session") != physical["Session"] or field.get("CompletedFrame") != physical["CompletedFrames"]
                or field.get("Gravity") != 9.81 or field.get("PendingBoundaryPotential") != 0
                or any(type(field.get(key)) not in (int, float) or not math.isfinite(field[key])
                       for key in ("Potential", "InitialPotential", "CommittedBoundaryPotential", "BodyImpulseY", "InternalWork", "MaximumEnergyResidual"))
                or field["Potential"] <= 0 or field["Potential"] == field["InitialPotential"] or field["BodyImpulseY"] >= 0
                or not 0 <= field["MaximumEnergyResidual"] <= gravity_bound
                or type(report.get("ProjectedPotentialJoules")) not in (int, float)
                or not abs(report["ProjectedPotentialJoules"] - field["Potential"]) < 1e-9
                or not abs(field["CommittedBoundaryPotential"] - (struct.unpack("<f", struct.pack("<f", .05))[0] - struct.unpack("<f", struct.pack("<f", .02))[0]) * 9.81 * .5) < 1e-15):
            return False
    if transfers or carbon or gravity or liquid_transfers:
        account = report.get("FluidTransfers" if liquid_transfers else "GasTransfers")
        emission, consumption = report.get("EmissionResult"), report.get("ConsumptionResult")
        injected = struct.unpack("f", struct.pack("f", .05))[0]
        consumed = struct.unpack("f", struct.pack("f", .02))[0]
        energy_bound = 1e-6
        if liquid_transfers:
            scale = report.get("TransferEnergyScaleJoules")
            if (type(scale) not in (int, float) or not math.isfinite(scale) or scale <= 0
                    or report.get("TransferEnergyBoundJoules") != 128 * 2.2204460492503131e-16 * scale):
                return False
            energy_bound = report["TransferEnergyBoundJoules"]
            incoming, outgoing = report.get("LiquidInjectionReceipt"), report.get("LiquidExtractionReceipt")
            if (not isinstance(incoming, dict) or not isinstance(outgoing, dict)
                    or any(packet.get("Species") != 3 or packet.get("Session") != physical["Session"]
                           or type(packet.get("CompletedFrame")) is not int for packet in (incoming, outgoing))
                    or not 0 < incoming["CompletedFrame"] < outgoing["CompletedFrame"] <= physical["CompletedFrames"]
                    or incoming.get("MassKg") != injected or outgoing.get("MassKg") != consumed
                    or incoming.get("Kind") != 1 or outgoing.get("Kind") != 2
                    or any(type(packet.get("InternalEnergy")) not in (int, float) or not math.isfinite(packet["InternalEnergy"])
                           for packet in (incoming, outgoing))
                    or type(outgoing.get("KineticEnergy")) not in (int, float) or not 0 < outgoing["KineticEnergy"] < math.inf
                    or not isinstance(account, dict) or account.get("KineticEnergyChange") != -outgoing["KineticEnergy"]
                    or type(account.get("MaterialEnergyChange")) not in (int, float)
                    or not abs(account["MaterialEnergyChange"] - incoming["InternalEnergy"] + outgoing["InternalEnergy"]) <=
                        64 * 2.2204460492503131e-16 * (abs(incoming["InternalEnergy"]) + abs(outgoing["InternalEnergy"]))):
                return False
        if (not isinstance(account, dict) or report.get("LiquidTransfersVerified" if liquid_transfers else "GasTransfersVerified") is not True or report.get("TransfersAvoidedNativeDispatch") is not True
                or report.get("TransferPacketsRouted") != 2 or report.get("ReceiptsPublished") != 2 or report.get("EmissionCallbacks") != 1 or report.get("ConsumptionCallbacks") != 1
                or account.get("PendingBatches") != 0 or account.get("AcceptedBatches") != 2 or account.get("CommittedBatches") != 2 or account.get("UnacknowledgedReceipts") != 0
                or type(report.get("TransferEnergyResidualJoules")) not in (int, float) or not abs(report["TransferEnergyResidualJoules"]) <= energy_bound
                or type(report.get("TransferMaximumRelativeMassError")) not in (int, float) or not 0 <= report["TransferMaximumRelativeMassError"] <= 1e-10):
            return False
        masses = account.get("MassChangesKg")
        expected_masses = [0, 0, 0, injected - consumed, 0] if liquid_transfers else [0, 0, 0, 0, injected - consumed] if carbon else None
        if (not isinstance(masses, list) or (masses != expected_masses if expected_masses is not None else masses not in ([0, injected - consumed, 0, 0], [0, injected - consumed, 0, 0, 0]))
                or type(account.get("KineticEnergyChange")) not in (int, float) or not math.isfinite(account["KineticEnergyChange"]) or account["KineticEnergyChange"] >= 0
                or type(account.get("MaterialEnergyChange")) not in (int, float) or not math.isfinite(account["MaterialEnergyChange"])
                or not isinstance(emission, dict) or not isinstance(consumption, dict)
                or emission.get("suceeded") != 1 or emission.get("mass") != .05 or consumption.get("mass") != .02
                or type(emission.get("elemIdx")) is not int or emission["elemIdx"] != consumption.get("elemIdx")
                or emission.get("temperature") != (310 if carbon else 300)
                or type(consumption.get("temperature")) not in (int, float) or not 0 < consumption["temperature"] < float("inf")
                or any(packet.get("diseaseIdx") != 255 or packet.get("diseaseCount") != 0 for packet in (emission, consumption))):
            return False
    if carbon:
        reading = report.get("TargetReading")
        if (report.get("CarbonDioxideTransfersVerified") is not True or report.get("Model") != "oni-sealed-domain-v2"
                or physical.get("Version") != 2 or not isinstance(reading, dict)
                or not isinstance(reading.get("Constituents"), list)
                or not any(isinstance(row, dict) and row.get("MaterialId") == "carbon_dioxide" and
                           type(row.get("MassKg")) in (int, float) and math.isfinite(row["MassKg"]) and row["MassKg"] > 0 for row in reading["Constituents"])):
            return False
    if actors or liquid_actors:
        actor = report.get("RegisteredActors")
        if not isinstance(actor, dict) or actor.get("Verified") is not True or actor.get("Stage") != 8:
            return False
        native = actor.get("Native")
        actor_energy_bound = 1e-6
        if liquid_actors:
            scale = actor.get("EnergyScaleJoules")
            fluid = report.get("FluidTransfers")
            if (actor.get("LiquidActors") is not True or actor.get("MaterialId") != "water"
                    or type(scale) not in (int, float) or not math.isfinite(scale) or scale <= 0
                    or actor.get("EnergyBoundJoules") != 128 * 2.2204460492503131e-16 * scale
                    or not isinstance(fluid, dict) or not isinstance(fluid.get("MassChangesKg"), list) or len(fluid["MassChangesKg"]) != 5
                    or any(fluid["MassChangesKg"][i] != 0 for i in (0, 1, 2, 4))
                    or type(fluid["MassChangesKg"][3]) not in (int, float) or not math.isfinite(fluid["MassChangesKg"][3])
                    or any(type(actor.get(key)) not in (int, float) or not 0 < actor[key] < math.inf for key in ("ActualConsumerMass", "ActualEmitterMass"))
                    or not abs(fluid["MassChangesKg"][3] - actor["ActualEmitterMass"] + actor["ActualConsumerMass"]) <= 1e-12):
                return False
            actor_energy_bound = actor["EnergyBoundJoules"]
        if (not isinstance(native, dict) or native.get("LiveActors") != 0 or actor.get("EmitterGameStatusObserved") is not True
                or actor.get("ConfigurationBatchVerified") is not True or actor.get("NativeActorBatchVerified") is not True
                or actor.get("RegistrationBatchVerified") is not True or actor.get("RetirementBatchVerified") is not True
                or actor.get("ActualBatchEmitterRegistrations") != 1
                or actor.get("InitialZeroEmitterVerified") is not True
                or type(actor.get("InitialZeroEmitterMass")) not in (int,float) or actor["InitialZeroEmitterMass"] != 0
                or type(actor.get("InitialZeroEmitterSamples")) is not int or actor["InitialZeroEmitterSamples"] <= 0
                or any(type(actor.get(key)) is not int or actor[key] <= 0 for key in ("RegistrationBatchesRouted", "RetirementBatchesRouted"))
                or type(actor.get("ConfigurationBatchesRouted")) is not int or actor["ConfigurationBatchesRouted"] <= 0
                or actor.get("ActualEmitterRegistrations") != 1 or actor.get("ActualBlockedCallbacks") != 1 or actor.get("ActualUnblockedCallbacks") != 1
                or type(actor.get("ActualConsumerCallbacks")) is not int or actor["ActualConsumerCallbacks"] <= 10
                or native.get("ConsumerSamples") != actor["ActualConsumerCallbacks"] or actor.get("ConsumerPublications") != native["ConsumerSamples"]
                or actor.get("EmitterPublications") != native.get("EmitterSamples") or actor.get("RemovalConsumerSamples") != native["ConsumerSamples"]
                or actor.get("RemovalEmitterSamples") != native.get("EmitterSamples")
                or type(actor.get("RemovedAtFrame")) is not int or physical["CompletedFrames"] - actor["RemovedAtFrame"] < 8
                or type(actor.get("EnergyResidualJoules")) not in (int, float) or not abs(actor["EnergyResidualJoules"]) <= actor_energy_bound
                or type(actor.get("MaximumRelativeMassError")) not in (int, float) or not 0 <= actor["MaximumRelativeMassError"] <= 1e-10):
            return False
    if checkpoint or paired:
        physical_checkpoint = report.get("PhysicalCheckpoint")
        if (not isinstance(physical_checkpoint, dict) or physical_checkpoint.get("Verified") is not True or physical_checkpoint.get("FormatVersion") != 2
                or physical_checkpoint.get("OrdinaryGameSaveIntegrated") is not False
                or any(type(physical_checkpoint.get(key)) is not int or physical_checkpoint[key] <= 0 for key in ("ImageBytes", "SourceFrame", "ResumedFrame"))
                or not 32 < physical_checkpoint["ImageBytes"] <= 1024**3
                or physical_checkpoint["ResumedFrame"] != physical_checkpoint["SourceFrame"] + 1 or physical_checkpoint["ResumedFrame"] > physical["CompletedFrames"]
                or any(type(physical_checkpoint.get(key)) not in (int, float) or not math.isfinite(physical_checkpoint[key]) or physical_checkpoint[key] <= 0
                       for key in ("SourcePhysicalTime", "ResumedPhysicalTime"))
                or not abs(physical_checkpoint["ResumedPhysicalTime"] - physical_checkpoint["SourcePhysicalTime"] - physical["LastTimestepSeconds"]) < 1e-12
                or any(not isinstance(physical_checkpoint.get(key), str) or len(physical_checkpoint[key]) != 64 or any(char not in "0123456789abcdef" for char in physical_checkpoint[key])
                       for key in ("SourceSha256", "ExpectedSha256", "ActualSha256"))
                or physical_checkpoint["SourceSha256"] == physical_checkpoint["ExpectedSha256"] or physical_checkpoint["ExpectedSha256"] != physical_checkpoint["ActualSha256"]):
            return False
    if paired:
        pair = report.get("PairedCheckpoint")
        if (not isinstance(pair, dict) or pair.get("Verified") is not True or pair.get("OrdinaryGameSaveIntegrated") is not False
                or pair.get("WholeColonyCheckpoint") is not False
                or any(pair.get(key) is not True for key in ("NativeCompatibilityCellsVerified", "ImmutableCopiesVerified", "ReleaseVerified"))):
            return False
        native = pair.get("Native")
        if (not isinstance(native, dict) or native.get("Size") != 112 or native.get("Version") != 1 or native.get("State") != 2
                or native.get("Session") != physical["Session"]
                or any(type(native.get(key)) is not int or native[key] <= 0 for key in ("Token", "CompletedFrame", "NativeEpoch", "CaptureThread", "NativeBytes", "PhysicalBytes"))
                or type(pair.get("RequestedFrame")) is not int or not 0 < pair["RequestedFrame"] <= native["CompletedFrame"] <= physical["CompletedFrames"]
                or type(native.get("PhysicalTime")) not in (int, float) or not math.isfinite(native["PhysicalTime"])
                or not 0 < native["PhysicalTime"] <= physical["TimeSeconds"]
                or type(pair.get("EncodedBytes")) is not int or pair["EncodedBytes"] <= native["NativeBytes"] + native["PhysicalBytes"] + 108
                or any(not isinstance(pair.get(key), str) or len(pair[key]) != 64 or any(char not in "0123456789abcdef" for char in pair[key])
                       for key in ("NativeSha256", "PhysicalSha256", "EnvelopeSha256"))):
            return False
        geometry = native.get("Configuration")
        if (not isinstance(geometry, dict) or geometry.get("Size") != 40 or geometry.get("Version") != 2 or geometry.get("Flags") != 1
                or any(type(geometry.get(key)) is not int or geometry[key] != report.get(key) for key in ("X", "Y", "Width", "Height"))):
            return False
    if saved or restored:
        colony = report.get("ColonyCheckpoint")
        if not isinstance(colony, dict) or colony.get("WholeColonyFluidCheckpoint") is not False:
            return False
        if (type(colony.get("SourceFrame")) is not int or colony["SourceFrame"] < 0
                or type(colony.get("SourceTime")) not in (int, float) or not math.isfinite(colony["SourceTime"]) or colony["SourceTime"] <= 0
                or not _sha256(colony.get("SourcePhysicalSha256"))):
            return False
        problems=colony.get("SaveProblems",{})
        if not isinstance(problems,dict):
            return False
        io_control=problems.get("IoControlRequired") is True
        if io_control != save_io_recovery:
            return False
        if save_io_recovery and (not saved or restored or problems.get("IoControlVerified") is not True
                or problems.get("AdvancedAfterIoControl") is not True or problems.get("NativeIoDialogObserved") is not True
                or problems.get("DraggingRestored") is not True or problems.get("IoControlStage") != 3
                or any(problems.get(key)!=1 for key in ("PreCallbacks","ActiveCallbacks","PostCallbacks"))
                or not _sha256(problems.get("BeforePhysicalSha256")) or problems.get("AfterPhysicalSha256")!=problems["BeforePhysicalSha256"]
                or problems.get("UnsuccessfulWrites") != 1
                or type(problems.get("UnsuccessfulWrites")) is not int or type(problems.get("DeclinedBeforeSerialization")) is not int
                or problems.get("DeclinedBeforeSerialization") != 1 or type(problems.get("IoControlFrame")) is not int
                or not 0 < problems["IoControlFrame"] < physical["CompletedFrames"]):
            return False
        if colony.get("Saved") is True:
            # ONI Pause/Unpause retain a count, so a true IsPaused flag does
            # not prove the save released the pause request it added.
            if (type(colony.get("SavePauseDepthBefore")) is not int or colony["SavePauseDepthBefore"] < 0
                    or type(colony.get("SavePauseDepthAfter")) is not int
                    or colony["SavePauseDepthAfter"] != colony["SavePauseDepthBefore"]):
                return False
            if (colony.get("SavedFileReadbackVerified") is not True or colony.get("SerializedBodyCaptures") != 1
                    or colony.get("SerializedHeaderCaptures") != 1 or not _sha256(colony.get("SerializedHeaderSha256"))
                    or type(colony.get("SerializedBodyBytes")) is not int or not 0 < colony["SerializedBodyBytes"] <= 256 << 20
                    or not _sha256(colony.get("SerializedBodySha256"))):
                return False
        if saved and (colony.get("Saved") is not True or colony.get("Loaded") is not False or colony.get("SimSaveWrites") != 1
                or colony.get("AutomaticSaveAdmissions") != (2 if save_io_recovery else 1) or colony.get("SavePauseStateRestored") is not True
                or (colony.get("PendingSaveRejectionRequired") is True and colony.get("PendingSaveRejectionVerified") is not True)
                or not _sha256(colony.get("SavedFileSha256")) or not colony.get("SavedFile")
                or colony["SourceFrame"] != physical["CompletedFrames"] or colony["SourceTime"] != physical["TimeSeconds"]):
            return False
        if restored and (colony.get("Loaded") is not True or type(colony.get("Saved")) is not bool
                or any(colony.get(key) is not True for key in ("ZeroTimeRestoreVerified", "ManagedSceneRestored", "RendererVerified"))
                or type(colony.get("NativeStartupBatchesDelivered")) is not int or colony["NativeStartupBatchesDelivered"] < 2
                or colony.get("RestoredInitialSha256") != colony["SourcePhysicalSha256"]
                or not _sha256(colony.get("CurrentPhysicalSha256")) or colony["CurrentPhysicalSha256"] == colony["SourcePhysicalSha256"]
                or colony.get("ExactContinuationFrames") != physical["CompletedFrames"]
                or colony.get("ReplayedFrame") != physical["CompletedFrames"] or colony.get("ReplayedTime") != physical["TimeSeconds"]
                or physical["TimeSeconds"] <= colony["SourceTime"]):
            return False
    if restored and colony.get("Saved"):
        if (colony.get("AutomaticSaveAdmissions") != 1 or colony.get("SimSaveWrites") != 1 or colony.get("SavePauseStateRestored") is not True
                or not _sha256(colony.get("SavedFileSha256")) or not colony.get("SavedFile")
                or type(colony.get("SavedFrame")) is not int or not 0 <= colony["SavedFrame"] <= physical["CompletedFrames"]
                or not _sha256(colony.get("SavedPhysicalSha256"))):
            return False
    if restored and colony.get("FrameZeroResaveRequired") is True:
        if (colony.get("Saved") is not True or colony.get("SavedAsAutoSave") is not True
                or colony.get("FrameZeroResaveVerified") is not True or colony.get("FrameZeroResaveUsedGameDelayedSave") is not True
                or colony.get("InitialPauseChallengeVerified") is not True
                or type(colony.get("InitialPositiveTicksHeld")) is not int or colony["InitialPositiveTicksHeld"]<1
                or colony.get("SavedFrame") != 0
                or colony.get("SavedTime") != colony["SourceTime"] or colony.get("SavedPhysicalSha256") != colony["SourcePhysicalSha256"]):
            return False
    if reload_in_process:
        workflow=report.get("InProcessReload")
        if (colony.get("FrameZeroResaveRequired") is not True or colony.get("FrameZeroResaveVerified") is not True
                or not isinstance(workflow,dict) or workflow.get("Requested") is not True or workflow.get("Verified") is not True
                or workflow.get("Stage")!=3 or workflow.get("WholeColonyFluidAuthority") is not False
                or any(workflow.get(key) is not True for key in ("ShutdownObserved","MainMenuObserved","MatchingFrameVerified","FirstAutosaveVerified","InputUnchanged"))
                or type(workflow.get("FirstProcessId")) is not int or workflow["FirstProcessId"]<=0
                or workflow.get("CurrentProcessId")!=workflow["FirstProcessId"]
                or type(workflow.get("FirstNativeSession")) is not int or not 0<workflow["FirstNativeSession"]<physical["Session"]
                or workflow.get("CurrentNativeSession")!=physical["Session"] or workflow.get("MatchedSession")!=physical["Session"]
                or type(workflow.get("FirstCompletedFrame")) is not int or not frames<=workflow["FirstCompletedFrame"]<=physical["CompletedFrames"]
                or any(not _sha256(workflow.get(key)) for key in ("FirstPhysicalSha256","FirstReportSha256","InputSha256"))):
            return False
    physics_exercised = (physical["LiquidMassKg"] > 1000 and physical["PressurePa"] > 0 if liquid_actors else
                         physical["LiquidMassKg"] > 1000 and physical["PressurePa"] > 2.2e8 if liquid_transfers else
                         physical["LiquidMassKg"] == 1100 and abs(physical["PressurePa"] - 2.2e8) < 1 if liquid else
                         physical["InitialOxygenKg"] > 0 and physical["CurrentOxygenKg"] > 0 and
                         physical["CurrentOxygenKg"] != physical["InitialOxygenKg"])
    if restored:
        # Exact complete-state replay and the saved-clock checks above cover
        # valid equilibrium restarts as well as moving mixtures.
        physics_exercised = physical["TimeSeconds"] > report["ColonyCheckpoint"]["SourceTime"] and physical["PressurePa"] > 0
    return (report.get("Kind") == "actual-game-sealed-native-fluid-domain" and report.get("Status") == "passed"
            and report.get("Platform") == expected_platform and report.get("BoundedFluidAuthority") is True
            and report.get("WholeColonyFluidAuthority") is False and report.get("FirstPlayableColonyPassed") is False
            and report.get("SavesAllowed") is False and physical.get("State") == 1
            and physical["CompletedFrames"] >= frames and physical["NativeTicket"] >= physical["CompletedFrames"]
            and physical["SolverThread"] != physical["StartThread"]
            and physical["TimeSeconds"] > 0 and 0 < physical["LastTimestepSeconds"] <= 1 and physics_exercised
            and (restored or report.get("NativeFlowControlObserved") is True)
            and all(report.get(key) is True for key in ("NativeCellsUnchanged", "NativeSnapshotCopied", "NativeSolidsAdvanced",
                "RetainedFrameVerified", "ProjectionPrimed", "NativeFlowFenceVerified",
                "NativeRendererAdvanced", "SaveGuardVerified", "NativeSaveGuardVerified", "GameGridProjectionVerified"))
            and observation.get("BoundedNativeDomainActive") is True and observed.get("Attached") is True
            and observed.get("ForwardingOnly") is False and observed.get("RecoveryRequired") is False
            and observed.get("AuthoritativeFluidOwner") is False and observed.get("OniBuild") == 744825)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--domain", type=Path, required=True)
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--mode", choices=("gas", "water", "water-transfer", "water-actor", "water-heat", "heat", "transfer", "actor", "carbon", "conduction", "gravity", "water-gravity", "checkpoint", "pair", "save", "load"), required=True)
    parser.add_argument("--saved", action="store_true", help="Require an ordinarily admitted colony save as well as the selected scene")
    parser.add_argument("--reload-in-process", action="store_true", help="Require a second load through the same process and main-menu lifecycle")
    parser.add_argument("--save-io-recovery", action="store_true", help="Require controlled write rejection and subsequent recovery")
    parser.add_argument("--platform", choices=("WindowsPlayer", "LinuxPlayer"), required=True)
    args = parser.parse_args()
    if not 1 <= args.frames <= 10000:
        parser.error("Expected 1..10000 native frames")
    if args.save_io_recovery and args.mode!="save":
        parser.error("--save-io-recovery requires mode save")
    if args.reload_in_process and args.mode!="load":
        parser.error("--reload-in-process requires mode load")
    try:
        report = json.loads(args.domain.read_text(encoding="utf-8-sig"))
        observation = json.loads(args.observation.read_text(encoding="utf-8-sig"))
        passed = native_domain_passed(report, observation, args.frames, liquid=args.mode in ("water", "water-transfer", "water-actor"), liquid_transfers=args.mode == "water-transfer",
            heat=args.mode == "heat", liquid_heat=args.mode == "water-heat", transfers=args.mode == "transfer", actors=args.mode in ("actor", "water-actor"), liquid_actors=args.mode == "water-actor",
            carbon=args.mode == "carbon", expected_platform=args.platform, conduction=args.mode == "conduction", gravity=args.mode == "gravity", liquid_gravity=args.mode == "water-gravity", checkpoint=args.mode == "checkpoint", paired=args.mode == "pair", saved=args.mode == "save" or args.saved, restored=args.mode == "load",save_io_recovery=args.save_io_recovery,reload_in_process=args.reload_in_process)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(json.dumps({"passed": False, "error": str(error)}))
        return 1
    print(json.dumps({"passed": passed, "mode": args.mode, "platform": args.platform,
        "wholeColonyFluidAuthority": False, "firstPlayableColonyPassed": False}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
