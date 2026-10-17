using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool CarbonParticleMode => ColonyRuntimeValidation.Settings?.Mode == "native-carbon-particle-domain";
        private const float CarbonParticleMass = .03125f, CarbonParticleTemperature = 310;
        private static bool carbonParticleStarted, carbonParticleVerified, carbonParticleRetired;
        private static int carbonParticleRetiredAtUnityFrame = -1;
        private static CO2? carbonParticle;
        private static Receipt? carbonParticleReceipt;
        private static int carbonParticleReceipts;
        private static ulong carbonParticleSourceFrame;
        private static double carbonParticleBeforeEnergy, carbonParticleEnergyResidual, carbonParticleMassError;
        private static double[] carbonParticleBeforeMasses = Array.Empty<double>();
        private static List<CO2> CarbonParticles() => AccessTools.Field(typeof(CO2Manager), "co2Items").GetValue(CO2Manager.instance) as List<CO2>
            ?? throw new InvalidOperationException("Native CO2 particle collection is unavailable.");
        private static void StartCarbonParticleFixture()
        {
            if (carbonParticleStarted) return;
            var manager = CO2Manager.instance ?? throw new InvalidOperationException("Native CO2 particle manager is unavailable.");
            carbonParticleBeforeEnergy = PhysicalEnergy;
            carbonParticleBeforeMasses = Enumerable.Range(0, Ids.Length).Select(k => cells.Sum(cell => cell.Mass(k))).ToArray();
            carbonParticleSourceFrame = latest.CompletedFrames;
            var particles = CarbonParticles(); var before = particles.Count;
            var position = Grid.CellToPosCCC(Grid.XYToCell(x + 1, y + 1), Grid.SceneLayer.Front);
            manager.SpawnBreath(position, CarbonParticleMass, CarbonParticleTemperature, false);
            Require(particles.Count == before + 1, "Native breath did not create exactly one CO2 particle.");
            carbonParticle = particles[particles.Count - 1];
            Require(carbonParticle.mass == CarbonParticleMass && carbonParticle.temperature == CarbonParticleTemperature,
                "Native CO2 particle changed its declared parcel.");
            carbonParticleStarted = true;
        }
        private static void ObserveCarbonParticleReceipt(Receipt receipt)
        {
            if (!CarbonParticleMode || receipt.Callback != -1 || receipt.Kind != 1) return;
            Require(carbonParticleStarted && !carbonParticleReceipt.HasValue && receipt.Species == 4,
                "Native CO2 particle produced a duplicate or unexpected mass receipt.");
            carbonParticleReceipt = receipt; ++carbonParticleReceipts;
        }
        internal static void ObserveCarbonParticleManagerUpdate()
        {
            if (!CarbonParticleMode || !carbonParticleStarted || carbonParticleRetired) return;
            // Observe removal before another breath reuses the pooled object.
            // A Unity component's identity is not its particle incarnation.
            if (carbonParticle == null || !CarbonParticles().Contains(carbonParticle))
            {
                carbonParticleRetired = true;
                carbonParticleRetiredAtUnityFrame = Time.frameCount;
            }
        }
        private static void VerifyCarbonParticleFixture()
        {
            Require(carbonParticleStarted && carbonParticleReceipt.HasValue, "Native CO2 particle did not reach the physical owner.");
            var receipt = carbonParticleReceipt!.Value;
            carbonParticleMassError = 0;
            for (var species = 0; species < Ids.Length; ++species)
                carbonParticleMassError = Math.Max(carbonParticleMassError, Math.Abs(cells.Sum(cell => cell.Mass(species)) -
                    carbonParticleBeforeMasses[species] - (species == 4 ? CarbonParticleMass : 0)));
            carbonParticleEnergyResidual = PhysicalEnergy - carbonParticleBeforeEnergy - transferStatus.MaterialEnergyChange - transferStatus.KineticEnergyChange;
            carbonParticleVerified = cellMassEditsRouted == 1 && cellMassEditsAvoidedNativeDispatch && carbonParticleReceipts == 1 &&
                receipt.CompletedFrame > carbonParticleSourceFrame && receipt.MassKg == CarbonParticleMass && receipt.Temperature == CarbonParticleTemperature &&
                receipt.InternalEnergy == transferStatus.MaterialEnergyChange && receipt.KineticEnergy == 0 && transferStatus.KineticEnergyChange == 0 &&
                transferStatus.AcceptedBatches == 1 && transferStatus.CommittedBatches == 1 && transferStatus.PendingBatches == 0 &&
                transferStatus.UnacknowledgedReceipts == 0 && transferStatus.MassChangesKg[4] == CarbonParticleMass &&
                carbonParticleRetired && carbonParticleRetiredAtUnityFrame > 0 &&
                carbonParticleMassError <= 1e-10 && Math.Abs(carbonParticleEnergyResidual) <= 1e-6;
            Require(carbonParticleVerified, "Native CO2 particle deposition, retirement or physical mass/energy account failed.");
        }
        private static object CarbonParticleReport() => new {
            Required = CarbonParticleMode, Started = carbonParticleStarted, Verified = carbonParticleVerified,
            Source = "CO2Manager.SpawnBreath", MassKg = CarbonParticleMass, TemperatureK = CarbonParticleTemperature,
            SourceFrame = carbonParticleSourceFrame, CompletedFrame = carbonParticleReceipt?.CompletedFrame,
            Receipts = carbonParticleReceipts, Receipt = carbonParticleReceipt,
            NativeDispatchAvoided = cellMassEditsAvoidedNativeDispatch, MassErrorKg = carbonParticleMassError,
            EnergyResidualJoules = carbonParticleEnergyResidual, EnergyBoundJoules = 1e-6,
            ParticleRetired = carbonParticleRetired, ParticleRetirementUnityFrame = carbonParticleRetiredAtUnityFrame,
            RetirementObservedBy = "CO2Manager.Sim33ms-postfix",
            ParticleReusedAfterRetirement = carbonParticleRetired && carbonParticle != null && CarbonParticles().Contains(carbonParticle) };
    }

    [HarmonyPatch(typeof(CO2Manager), nameof(CO2Manager.Sim33ms))]
    internal static class NativeCarbonParticleRetirementPatch
    {
        private static void Postfix()
        {
            NativeSealedDomainValidation.ObserveCarbonParticleManagerUpdate();
            NativeSealedDomainValidation.ObserveTransientParticleManagerUpdate();
        }
    }
}
