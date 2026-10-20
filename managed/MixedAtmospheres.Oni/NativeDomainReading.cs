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
        internal static MixtureThermodynamicReading Create(OniNativeDomain.Cell cell, FluidMaterialCatalog catalog, long revision, int publicCell, bool voidPhase = false)
        {
            if (double.IsNaN(cell.LiquidFraction) || double.IsInfinity(cell.LiquidFraction) || cell.LiquidFraction < 0)
                throw new InvalidDataException("Completed native liquid volume is invalid.");
            var constituents = new List<MixtureThermodynamicConstituent>(); var gasVolume = 0.0;
            for (var k = 0; k < ids.Length; ++k)
            {
                var mass = cell.Mass(k); if (mass == 0) continue;
                var material = catalog.Materials[ids[k]];
                if (material.Phase != (k == 3 ? NativeMatterPhase.Liquid : NativeMatterPhase.Gas))
                    throw new InvalidDataException("Completed native material phase differs from the presentation catalogue.");
                var volume = k == 3 ? cell.LiquidFraction :
                    mass / material.MolarMassKgPerMol * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * cell.Temperature / cell.Pressure;
                constituents.Add(new MixtureThermodynamicConstituent(ids[k], material.Phase, mass, material.MolarMassKgPerMol, volume));
                if (k != 3) gasVolume += volume;
            }
            return new MixtureThermodynamicReading(constituents, cell.TotalMass, cell.InternalEnergy, cell.ResolvedKineticEnergy,
                cell.Temperature, gasVolume > 0 ? cell.Pressure : 0, cell.Pressure, 1, gasVolume, cell.LiquidFraction,
                voidPhase ? Math.Max(0, 1 - (gasVolume + cell.LiquidFraction)) : 0,
                catalog.Version, catalog.Fingerprint, revision, "native-domain:cell:" + publicCell,
                momentumX: cell.MomentumX, momentumY: cell.MomentumY, resolvedVacuumInterface: voidPhase);
        }
    }
}
