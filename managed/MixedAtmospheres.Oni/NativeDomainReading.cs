using System;
using System.Collections.Generic;
using System.IO;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Oni
{
    // Completed native facts only. Gas partial volumes retain traces even when
    // their headspace is below the rounding unit of the liquid-volume field.
    internal static class NativeDomainReading
    {
        private static readonly string[] ids = { "nitrogen", "oxygen", "helium", "water", "carbon_dioxide" };
        internal static MixtureThermodynamicReading Create(OniNativeDomain.Cell cell, FluidMaterialCatalog catalog, long revision, int publicCell, bool voidPhase = false, double? occupiedVolume = null)
        {
            if (double.IsNaN(cell.LiquidFraction) || double.IsInfinity(cell.LiquidFraction) || cell.LiquidFraction < 0)
                throw new InvalidDataException("Completed native liquid volume is invalid.");
            if (voidPhase && (!occupiedVolume.HasValue || double.IsNaN(occupiedVolume.Value) || double.IsInfinity(occupiedVolume.Value) ||
                occupiedVolume.Value < 0 || occupiedVolume.Value > 1 || (cell.TotalMass == 0) != (occupiedVolume.Value == 0)))
                throw new InvalidDataException("Completed native occupied volume is missing or invalid.");
            var gasMoles = 0.0;
            if (voidPhase) for (var k = 0; k < ids.Length; ++k)
                if (k != 3) gasMoles += cell.Mass(k) / catalog.Materials[ids[k]].MolarMassKgPerMol;
            var headspace = voidPhase ? occupiedVolume!.Value - cell.LiquidFraction : 0;
            var idealVolume = gasMoles > 0 ? gasMoles * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * cell.Temperature / cell.Pressure : 0;
            // Subtracting nearly equal liquid/material volumes can erase a
            // trace headspace. Use geometry only when it also satisfies the
            // reading's existing relative ideal-gas consistency contract.
            var geometricGas = voidPhase && gasMoles > 0 && headspace > 0 && Math.Abs(headspace - idealVolume) <=
                MixtureThermodynamicReading.MaximumVolumeClosureRelativeTolerance * Math.Max(headspace, idealVolume);
            var constituents = new List<MixtureThermodynamicConstituent>(); var gasVolume = 0.0;
            for (var k = 0; k < ids.Length; ++k)
            {
                var mass = cell.Mass(k); if (mass == 0) continue;
                var material = catalog.Materials[ids[k]];
                if (material.Phase != (k == 3 ? NativeMatterPhase.Liquid : NativeMatterPhase.Gas))
                    throw new InvalidDataException("Completed native material phase differs from the presentation catalogue.");
                // Geometry comes from the completed owner. Keep the EOS gas
                // partial volume when headspace is hidden by liquid rounding.
                var volume = k == 3 ? cell.LiquidFraction : geometricGas ?
                    headspace * (mass / material.MolarMassKgPerMol / gasMoles) :
                    mass / material.MolarMassKgPerMol * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * cell.Temperature / cell.Pressure;
                constituents.Add(new MixtureThermodynamicConstituent(ids[k], material.Phase, mass, material.MolarMassKgPerMol, volume));
                if (k != 3) gasVolume += volume;
            }
            if (geometricGas) gasVolume = headspace;
            return new MixtureThermodynamicReading(constituents, cell.TotalMass, cell.InternalEnergy, cell.ResolvedKineticEnergy,
                cell.Temperature, gasVolume > 0 ? cell.Pressure : 0, cell.Pressure, 1, gasVolume, cell.LiquidFraction,
                voidPhase ? 1 - occupiedVolume!.Value : 0,
                catalog.Version, catalog.Fingerprint, revision, "native-domain:cell:" + publicCell,
                momentumX: cell.MomentumX, momentumY: cell.MomentumY, resolvedVacuumInterface: voidPhase);
        }
    }
}
