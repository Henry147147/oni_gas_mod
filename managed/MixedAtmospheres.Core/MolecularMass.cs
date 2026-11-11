namespace MixedAtmospheres.Core
{
    /// <summary>
    /// The molar mass of a game element's molecule (docs/material-catalogue.md,
    /// user decision 2026-11-11). The game's element table gives oxygen,
    /// polluted oxygen, hydrogen and chlorine the mass of one atom (about 16, 16,
    /// 1 and 34.5 g/mol); their gases are molecules of two atoms. Every other
    /// element keeps the game's value. The rule multiplies the game's own
    /// number, so no game value is committed here.
    /// </summary>
    public static class MolecularMass
    {
        /// <summary>Atoms of the game's element in one molecule: 2 for the diatomic elements and their liquids, otherwise 1.</summary>
        public static int AtomsPerMolecule(string elementId)
        {
            switch (elementId)
            {
                case "Oxygen": case "ContaminatedOxygen": case "LiquidOxygen":
                case "Hydrogen": case "LiquidHydrogen":
                case "ChlorineGas": case "Chlorine":
                    return 2;
                default: return 1;
            }
        }

        /// <summary>kg/mol of the element's molecule from the game's g/mol.</summary>
        public static double KgPerMol(string elementId, double gameGramsPerMol) => gameGramsPerMol * AtomsPerMolecule(elementId) / 1000.0;
    }
}
