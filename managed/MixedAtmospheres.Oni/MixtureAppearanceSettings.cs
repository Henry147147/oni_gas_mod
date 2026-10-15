using System;
using System.Globalization;
using System.IO;
using MixedAtmospheres.Core;
using Newtonsoft.Json;

namespace MixedAtmospheres.Oni
{
    internal static class MixtureAppearanceSettings
    {
        private sealed class SavedOptions
        {
            public bool PatternEnabled = true;
            public double PatternContrast = 0.04;
        }

        private static SavedOptions current = new SavedOptions();
        private static string destination = "";
        internal static double Contrast => current.PatternEnabled ? current.PatternContrast : 0;
        internal static MixtureVisualOptions Options => new MixtureVisualOptions
        { PatternEnabled = current.PatternEnabled, PatternContrast = current.PatternContrast };

        internal static void Initialize()
        {
            destination = Path.Combine(Util.RootFolder(), "mods", "config", "MixedAtmospheres", "appearance.json");
            if (!File.Exists(destination)) return;
            try
            {
                if (new FileInfo(destination).Length > 4096) throw new InvalidDataException("Appearance settings are too large.");
                var value = JsonConvert.DeserializeObject<SavedOptions>(File.ReadAllText(destination), new JsonSerializerSettings
                { TypeNameHandling = TypeNameHandling.None, MissingMemberHandling = MissingMemberHandling.Error });
                if (value == null || !Valid(value.PatternContrast)) throw new InvalidDataException("Invalid mixture appearance contrast.");
                current = value;
            }
            catch (Exception error)
            {
                UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Could not read appearance settings; using defaults: " + error.Message);
            }
        }

        internal static void SetContrast(double contrast, bool persist = true)
        {
            if (!Valid(contrast)) throw new ArgumentOutOfRangeException(nameof(contrast));
            current = new SavedOptions { PatternEnabled = contrast != 0, PatternContrast = contrast == 0 ? 0.04 : contrast };
            var model = MixturePresentationRuntime.CurrentModel;
            if (model != null) MixturePresentationRuntime.Configure(model.WithVisualOptions(Options));
            if (!persist) return;
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            var temporary = destination + ".tmp";
            File.WriteAllText(temporary, ReportJson.Serialize(current));
            if (File.Exists(destination)) File.Replace(temporary, destination, null);
            else File.Move(temporary, destination);
        }

        private static bool Valid(double contrast) => !double.IsNaN(contrast) && !double.IsInfinity(contrast) && contrast >= 0 && contrast <= 0.15;
    }
}
