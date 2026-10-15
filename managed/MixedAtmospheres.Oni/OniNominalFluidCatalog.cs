using System;
using System.Collections.Generic;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Oni
{
    /// <summary>Explicit private integration surrogate. ONI maxMass is gameplay data, never a physical liquid equation of state.</summary>
    internal static class OniNominalFluidCatalog
    {
        internal const string ModelVersion = "oni-744825-nominal-liquid-surrogate-v2";
        internal const string ModelDescription = "Local ONI maxMass per declared cell volume as a nominal incompressible liquid reference; constant heat capacities and ideal gases; not a physical equation of state or production catalogue.";

        internal static FluidMaterialCatalog Build(IEnumerable<Element> elements, IEnumerable<string> requiredMaterialIds,
            double cellVolumeCubicMetres = 1)
        {
            if (elements == null) throw new ArgumentNullException(nameof(elements));
            if (requiredMaterialIds == null) throw new ArgumentNullException(nameof(requiredMaterialIds));
            if (double.IsNaN(cellVolumeCubicMetres) || double.IsInfinity(cellVolumeCubicMetres) || cellVolumeCubicMetres <= 0)
                throw new ArgumentOutOfRangeException(nameof(cellVolumeCubicMetres));
            var missing = new HashSet<string>(StringComparer.Ordinal);
            foreach (var id in requiredMaterialIds)
            {
                if (string.IsNullOrWhiteSpace(id)) throw new ArgumentException("Missing required fluid identity.", nameof(requiredMaterialIds));
                missing.Add(id);
            }
            var materials = new List<FluidMaterial>();
            var found = new HashSet<string>(StringComparer.Ordinal);
            foreach (var element in elements)
            {
                if (element == null) throw new ArgumentException("Null local element definition.", nameof(elements));
                var id = element.id.ToString();
                if (!missing.Contains(id)) continue;
                if (!found.Add(id)) throw new ArgumentException("Duplicate local element definition: " + id, nameof(elements));
                if (!element.IsGas && !element.IsLiquid) throw new InvalidOperationException("Required material is not a fluid: " + id);
                // defaultValues.mass is the spawn mass and is deliberately not used.
                // maxMass is only the explicit nominal full-cell gameplay reference.
                // No pressure/compression relation or undisclosed native energy is inferred.
                materials.Add(new FluidMaterial(id, element.IsLiquid ? NativeMatterPhase.Liquid : NativeMatterPhase.Gas,
                    element.molarMass / 1000.0, element.specificHeatCapacity * 1000.0,
                    element.IsLiquid ? element.maxMass / cellVolumeCubicMetres : 0));
            }
            missing.ExceptWith(found);
            if (missing.Count != 0) throw new InvalidOperationException("Missing local fluid definitions: " + string.Join(", ", missing));
            return new FluidMaterialCatalog(ModelVersion, materials);
        }
    }
}
