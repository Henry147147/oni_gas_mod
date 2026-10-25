using System;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    // The mixture pattern and trace threshold (docs/settings.md). The
    // settings runtime applies the player's preferences here and records
    // contrast changes made from the overlay legend; the presentation
    // follows at once.
    internal static class MixtureAppearanceSettings
    {
        private static double contrast = 0.04, traceFraction = 0.01;
        // Set by the settings runtime: (contrast, persist).
        internal static Action<double, bool>? ContrastChanged { get; set; }
        internal static double Contrast => contrast;
        internal static MixtureVisualOptions Options => new MixtureVisualOptions
        { PatternEnabled = contrast != 0, PatternContrast = contrast == 0 ? 0.04 : contrast, MeaningfulGasMoleFraction = traceFraction };

        // The player's preferences; a running presentation takes them.
        internal static void Apply(double patternContrast, double traceMoleFraction)
        {
            if (!Valid(patternContrast) || double.IsNaN(traceMoleFraction) || traceMoleFraction < 0 || traceMoleFraction > 1)
                throw new ArgumentOutOfRangeException(nameof(patternContrast));
            contrast = patternContrast; traceFraction = traceMoleFraction;
            var model = MixturePresentationRuntime.CurrentModel;
            if (model == null) return;
            var current = model.VisualOptions; var next = Options;
            if (current.PatternEnabled == next.PatternEnabled && current.PatternContrast == next.PatternContrast &&
                current.MeaningfulGasMoleFraction == next.MeaningfulGasMoleFraction) return;
            MixturePresentationRuntime.Configure(model.WithVisualOptions(next));
        }

        internal static void SetContrast(double value, bool persist = true)
        {
            if (!Valid(value)) throw new ArgumentOutOfRangeException(nameof(value));
            contrast = value;
            ContrastChanged?.Invoke(value, persist);
            var model = MixturePresentationRuntime.CurrentModel;
            if (model != null) MixturePresentationRuntime.Configure(model.WithVisualOptions(Options));
        }

        private static bool Valid(double value) => !double.IsNaN(value) && !double.IsInfinity(value) && value >= 0 && value <= 0.15;
    }
}
