using System;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // A duplicant holding its breath or suffocating on the owned asteroid is
    // told why the air at its head fails easy-mode breathing (the same
    // warnings as the cell hover): too little oxygen, too much carbon dioxide,
    // or no gas.
    internal static class ColonyBreathingStatus
    {
        private static bool installed;
        internal static void Install()
        {
            if (installed || Db.Get() == null) return;
            installed = true;
            foreach (var item in new[] { Db.Get().DuplicantStatusItems.HoldingBreath, Db.Get().DuplicantStatusItems.Suffocating })
            {
                if (item == null) continue;
                var original = item.resolveTooltipCallback;
                item.resolveTooltipCallback = (text, data) => Explain(original != null ? original(text, data) : text, data);
            }
        }
        internal static string Explain(string text, object? data)
        {
            try
            {
                var go = data switch { StateMachine.Instance smi => smi.gameObject, Component c => c.gameObject, GameObject g => g, _ => null };
                if (go == null || !ColonyOwner.Active) return text;
                var cell = Grid.CellAbove(Grid.PosToCell(go));
                if (!Grid.IsValidCell(cell) || !MixturePresentationRuntime.TryGetCell(cell, out var reading) || reading == null || reading.Warnings.Count == 0) return text;
                return text + "\n\nThe air here: " + string.Join("; ", reading.Warnings) + ".";
            }
            catch (Exception) { return text; }
        }
    }
}
