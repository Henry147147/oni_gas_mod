using System;

namespace MixedAtmospheres.Oni
{
    // The ten-cycle bridge's arithmetic (docs/nitrogen.md), engine-free so
    // tools/SettingsTests checks it without the game.
    internal static class StartingAirBridge
    {
        internal const double GasConstant = 8.31446261815324;

        // Moles of a same-mass air cell made from `kg` of a source gas: the
        // source keeps `share` of them (oni_colony_import.cpp).
        internal static double AirMoles(double kg, double sourceMolar, double nitrogenMolar, double share) =>
            kg / (share * sourceMolar + (1 - share) * nitrogenMolar);

        // Moles of oxygen that well-mixed gas lets duplicants breathe before it
        // fails the rules (minimum share and partial pressure; none at all when
        // it starts above the maximum share), and before carbon dioxide passes
        // its share or partial pressure limit. Each consumed mole of oxygen
        // adds k moles of carbon dioxide.
        internal static (double oxygen, double carbonDioxide) BreathableMoles(double oxygenMol, double co2Mol, double gasMol, double volume,
            double temperature, double k, double minimumOxygenShare, double minimumOxygenPa, double maximumOxygenShare,
            double maximumCo2Share, double maximumCo2Pa)
        {
            if (!(gasMol > 0) || !(volume > 0) || !(temperature > 0) || !(k > 0)) return (0, 0);
            var molesPerPa = volume / (GasConstant * temperature);
            var byShare = (oxygenMol - minimumOxygenShare * gasMol) / (1 - minimumOxygenShare + minimumOxygenShare * k);
            var byPressure = oxygenMol - minimumOxygenPa * molesPerPa;
            var oxygen = oxygenMol > maximumOxygenShare * gasMol ? 0 : Math.Max(0, Math.Min(byShare, byPressure));
            var co2ByShare = (maximumCo2Share * gasMol - co2Mol) / (k * (1 - maximumCo2Share) + maximumCo2Share);
            var co2ByPressure = (maximumCo2Pa * molesPerPa - co2Mol) / k;
            return (oxygen, Math.Max(0, Math.Min(co2ByShare, co2ByPressure)));
        }
    }
}
