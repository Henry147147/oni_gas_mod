using System.Collections.Generic;
using System.Globalization;

namespace MixedAtmospheres.Oni
{
    // "What plays differently" (docs/starting-air.md#what-plays-differently):
    // the start-of-colony dialog lists it, and Settings → Gameplay → How it
    // plays shows the same list again. At most eight one-line items, each
    // something the player acts on; every number comes from the colony's
    // settings. Engine-free: tools/SettingsTests compiles it.
    internal static class ColonyPrimer
    {
        internal const string Title = "What plays differently";

        // `fallout`: the loaded content has nuclear fallout (Spaced Out!).
        internal static IReadOnlyList<string> Lines(SettingValues values, bool fallout)
        {
            var share = values.Number("breathing.minimumOxygenFraction");
            var pascals = values.Number("breathing.minimumOxygenPressurePa");
            var toxic = values.Number(SettingCatalog.ToxicLimit);
            var lines = new List<string>
            {
                "Gases stay mixed. A Gas Filter takes out the gas you select and lets the rest through.",
                "Duplicants need air with at least " + Percent(share) + " oxygen and " + Kilopascals(pascals) +
                    " of oxygen (polluted oxygen counts): see the Oxygen margin overlay."
            };
            var gases = fallout ? "Chlorine, sour gas and nuclear fallout" : "Chlorine and sour gas";
            var co2 = values.Toggle(SettingCatalog.Co2Limits) ? "carbon dioxide above " + Percent(values.Number("breathing.maximumCo2Fraction")) +
                " or " + Kilopascals(values.Number("breathing.maximumCo2PressurePa")) : null;
            if (toxic < 1)
                lines.Add(gases + " make air unbreathable once together they are " + Percent(toxic) + " of it" + (co2 != null ? ", and so does " + co2 : "") + ".");
            else if (co2 != null)
                lines.Add("Air with " + co2 + " is unbreathable.");
            lines.Add("An electrolyzer releases twice as much hydrogen as oxygen, by moles, into the same air: filter or pump the hydrogen out.");
            lines.Add("A liquid lock holds back only about 10 kPa per tile of water height, and base air is about 125 kPa: seal off vacuum with airlocks.");
            lines.Add("Gases layer by molecular weight: natural gas and steam rise above oxygen; chlorine sinks below carbon dioxide.");
            // With evaporation below boiling on, air near water holds humidity: say so, and where to turn it off.
            lines.Add(values.Toggle(SettingCatalog.EvaporationBelowBoiling)
                ? "Evaporation below boiling is on: air near water holds about 2% steam and wet rooms form dew (Gameplay → Boiling and condensation)."
                : "Boiling follows pressure: water in vacuum or thin air boils at room temperature.");
            lines.Add("A Gas Element Sensor reacts to its gas from 0.1% of the air by mass; a Gas Share Sensor switches at a share you choose.");
            return lines;
        }

        // The list as the dialog and the settings window show it.
        internal static string Text(SettingValues values, bool fallout) => Title + ":\n• " + string.Join("\n• ", Lines(values, fallout));

        internal static string Percent(double fraction) => (fraction * 100).ToString("0.##", CultureInfo.InvariantCulture) + "%";
        internal static string Kilopascals(double pascals) => (pascals / 1000).ToString("0.##", CultureInfo.InvariantCulture) + " kPa";
    }
}
