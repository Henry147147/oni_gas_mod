using System;
using System.Globalization;
using System.IO;
using Newtonsoft.Json;

namespace MixedAtmospheres.Oni
{
    internal static class GasDiffusionOptions
    {
        private sealed class Options
        {
            public string Mode = "physical";
            public double AcceleratedMultiplier = 100;
        }
        private static Options options = new Options();
        private static string destination = "";
        private static uint? pending;
        internal static OniNativeDomain.DiffusionStatus Current = OniNativeDomain.DiffusionStatus.Empty;
        internal static string Error = "";
        private static uint DefaultMode => options.Mode == "physical" ? 1U : options.Mode == "accelerated" ? 2U : 0U;
        internal static string Description => "Gas diffusion: " +
            (Current.State == 1 ? Current.Mode == 0 ? "off (saved/reference setting)" : Current.Mode == 1 ? "physical · 1×" :
                "accelerated · " + Current.Multiplier.ToString("G5", CultureInfo.InvariantCulture) + "×" :
                options.Mode + " (new gas regions)") + (pending.HasValue ? " · change pending" : "") +
            (Error.Length == 0 ? "" : " · " + Error);
        internal static void Initialize()
        {
            destination = Path.Combine(Util.RootFolder(), "mods", "config", "MixedAtmospheres", "diffusion.json");
            if (!File.Exists(destination)) return;
            var value = JsonConvert.DeserializeObject<Options>(File.ReadAllText(destination), new JsonSerializerSettings
            { TypeNameHandling = TypeNameHandling.None, MissingMemberHandling = MissingMemberHandling.Error });
            if (value == null || (value.Mode != "physical" && value.Mode != "accelerated" && value.Mode != "off") ||
                double.IsNaN(value.AcceleratedMultiplier) || double.IsInfinity(value.AcceleratedMultiplier) || value.AcceleratedMultiplier < 1)
                throw new InvalidDataException("Invalid Mixed Atmospheres diffusion settings.");
            options = value;
        }
        // Restores never call this: the saved mode remains authoritative.
        internal static void NewDomain(OniNativeDomain.Status status, bool gasOnly)
        { pending = gasOnly ? DefaultMode : (uint?)null; Refresh(status); }
        internal static void Reset()
        { pending = null; Current = OniNativeDomain.DiffusionStatus.Empty; Error = ""; }
        internal static void Cycle()
        {
            var mode = Current.State == 1 ? Current.Mode : DefaultMode;
            pending = mode == 1 ? 2U : mode == 2 ? 0U : 1U;
            if (Current.State != 1) { Save(pending.Value); pending = null; }
        }
        internal static void Refresh(OniNativeDomain.Status completed)
        {
            if (pending.HasValue && completed.State == 1)
            {
                var mode = pending.Value;
                NativeSealedDomainValidation.BeforeGasDiffusionChange();
                var code = OniNativeDomain.ConfigureDiffusion(completed.Session, completed.CompletedFrames, mode,
                    mode == 2 ? options.AcceleratedMultiplier : 1);
                if (code == 0) {
                    NativeSealedDomainValidation.AfterGasDiffusionChange(mode, mode == 2 ? options.AcceleratedMultiplier : 1);
                    Save(mode); pending = null; Error = "";
                }
                else if (code != 3) { Error = "mode change rejected (gas-only domain required)"; pending = null; }
            }
            var value = OniNativeDomain.DiffusionStatus.Empty;
            if (OniNativeDomain.CopyDiffusionStatus(ref value) != 0)
                throw new InvalidOperationException("Could not read native diffusion diagnostics.");
            Current = value;
        }
        private static void Save(uint mode)
        {
            options.Mode = mode == 1 ? "physical" : mode == 2 ? "accelerated" : "off";
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            var temporary = destination + ".tmp";
            File.WriteAllText(temporary, ReportJson.Serialize(options));
            if (File.Exists(destination)) File.Replace(temporary, destination, null);
            else File.Move(temporary, destination);
        }
    }
}
