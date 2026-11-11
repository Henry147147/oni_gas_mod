using System;
using System.Collections.Generic;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    // The one place the mod reads an element's molar mass: the game's value
    // times the atoms in its molecule (MolecularMass; docs/material-catalogue.md).
    // The catalogue the solver gets, the mole shares of sensors and breathing,
    // the starting-air estimate and the validation arenas all use it, so no
    // reader can disagree with the solver's pressures.
    internal static class OniMolarMass
    {
        private static List<Element>? of;
        private static double[] table = Array.Empty<double>();

        // kg/mol; 0 for an element the game gives no molar mass.
        internal static double KgPerMol(Element element)
        {
            var elements = ElementLoader.elements;
            if (elements == null || element.idx >= elements.Count || !ReferenceEquals(elements[element.idx], element)) return Compute(element);
            if (!ReferenceEquals(elements, of) || table.Length != elements.Count)
            {
                var built = new double[elements.Count];
                for (var k = 0; k < built.Length; ++k) built[k] = elements[k] == null ? 0 : Compute(elements[k]);
                table = built; of = elements;
            }
            return table[element.idx];
        }

        private static double Compute(Element element) =>
            element.molarMass > 0 ? MolecularMass.KgPerMol(element.id.ToString(), element.molarMass) : 0;
    }
}
