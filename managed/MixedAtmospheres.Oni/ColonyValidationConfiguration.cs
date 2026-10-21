using System;
using System.IO;
using System.Linq;
using Newtonsoft.Json;

namespace MixedAtmospheres.Oni
{
    // Explicit private development request read from colony-validation.json.
    // Parsing and the limits that need no live game state stay engine-free so
    // the same rules can be tested without Unity or a native simulator.
    internal sealed class ColonyValidationConfiguration
    {
        public string Mode = "snapshot-copy";
        public string LoadSavePath = "";
        public bool AutoStartNewColony = false;
        public bool ReuseGeneratedWorld = false;
        public bool IdleReservoirLeaseOnly = false;
        public string PrivateFileProtocolAssemblySha256 = "";
        public bool PrivateOwnedLoadRejection = false;
        public bool QuitWhenComplete = false;
        public bool SaveOnComplete = false;
        // colony-owner with SaveOnComplete: save halfway and again at the end to the same file.
        public bool OverwriteSave = false;
        public bool SaveBeforeResume = false;
        public bool VerifySaveIoRecovery = false;
        public bool VerifyNaturalAutosave = false;
        public bool VerifyPersistentActors = false;
        public bool VerifyLiveBreathing = false;
        public bool VerifyTransientParticles = false;
        public bool SubnormalCarbonParticle = false;
        public bool ReloadInProcess = false;
        public int CompletedFrames = 30;
        public int TimeoutSeconds = 600;
        public string Seed = "147147";
        // A starting cluster layout ("clusters/<name>"); empty keeps the game's default (Terra).
        public string Cluster = "";
        public string ColonyName = "MA Visual Test";
        // colony-owner only: one in-game system check (ColonyScenarios).
        public string Scenario = "";
        // Ten ONI cycles are about 30,000 native frames of 0.2 s. A long run is
        // a separate explicit request with its own limits and bounded evidence,
        // never a silent widening of the short fixture controls above.
        public bool LongRun = false;
        public int TelemetryIntervalFrames = ColonyValidationLimits.DefaultTelemetryIntervalFrames;
    }

    internal static class ColonyValidationLimits
    {
        internal const int MaximumConfigurationCharacters = 8192;
        internal const int MinimumFrames = 1, MaximumFrames = 10000, MaximumLongRunFrames = 100000;
        internal const int MinimumTimeoutSeconds = 30, MaximumTimeoutSeconds = 7200, MaximumLongRunTimeoutSeconds = 48 * 3600;
        internal const int DefaultTelemetryIntervalFrames = 300, MinimumTelemetryIntervalFrames = 10, MaximumTelemetryIntervalFrames = 10000;
        internal static readonly string[] Scenarios = { "terrain", "heat", "phase", "freeze-melt", "pump", "deoxidizer", "pipes",
            "door", "sublimation", "falling", "breathing", "airflow", "electrolysis", "vent-overpressure", "space-heater",
            "bottle-emptier", "gas-generator", "germs", "puft", "plant", "geyser", "rocket-exhaust", "meteor", "sensor" };
        internal static readonly string[] Modes =
        {
            "snapshot-copy", "native-observer", "native-import-domain", "native-liquid-import-domain", "native-domain",
            "native-solid-heat-domain", "native-mixed-domain", "native-mixed-heat-domain", "native-viscous-domain",
            "native-liquid-domain", "native-liquid-transfer-domain", "native-heat-domain", "native-liquid-heat-domain",
            "native-transfer-domain", "native-radius-domain", "native-rectangular-domain", "native-actor-domain",
            "native-liquid-actor-domain", "native-pressure-read-domain", "native-liquid-pressure-read-domain",
            "native-breathing-read-domain", "native-carbon-particle-domain", "native-carbon-domain", "native-conduction-domain",
            "native-gravity-domain", "native-liquid-gravity-domain", "native-checkpoint-domain", "native-paired-checkpoint-domain",
            "native-save-domain", "native-load-domain", "presentation", "tile-hover", "coupled-presentation", "load-rejection",
            "native-configured-domain", "native-configured-water-domain", "native-configured-vacuum-domain", "colony-owner", "colony-owner-load"
        };
        // Scenes whose retained evidence was audited as bounded per frame.
        // Fixture variants keep their short, exact windows; saved-colony replay
        // keeps one journal per frame and single-save evidence, so it is not
        // admitted until those are streamed. Configured rooms keep per-cell
        // state only and their natural autosaves use the paired configured save.
        internal static readonly string[] LongRunModes =
            { "snapshot-copy", "native-observer", "native-domain", "native-liquid-domain", "native-mixed-domain",
              "native-configured-domain", "native-configured-water-domain", "native-configured-vacuum-domain", "colony-owner", "colony-owner-load" };

        internal static ColonyValidationConfiguration Parse(string text)
        {
            if (text == null) throw new ArgumentNullException(nameof(text));
            if (text.Length > MaximumConfigurationCharacters) throw new InvalidDataException("Colony validation configuration is too large.");
            var configuration = JsonConvert.DeserializeObject<ColonyValidationConfiguration>(text, new JsonSerializerSettings
            { TypeNameHandling = TypeNameHandling.None, MissingMemberHandling = MissingMemberHandling.Error })
                ?? throw new InvalidDataException("Missing colony validation configuration.");
            Validate(configuration);
            return configuration;
        }

        internal static void Validate(ColonyValidationConfiguration configuration)
        {
            if (configuration == null) throw new ArgumentNullException(nameof(configuration));
            var longRun = configuration.LongRun;
            if (configuration.Mode == null || !Modes.Contains(configuration.Mode, StringComparer.Ordinal) ||
                configuration.CompletedFrames < MinimumFrames || configuration.CompletedFrames > MaximumLongRunFrames ||
                configuration.TimeoutSeconds < MinimumTimeoutSeconds || configuration.TimeoutSeconds > MaximumLongRunTimeoutSeconds)
                throw new InvalidDataException("Unsupported colony validation mode or limits.");
            if (configuration.OverwriteSave && (configuration.Mode != "colony-owner" || !configuration.SaveOnComplete))
                throw new InvalidDataException("OverwriteSave needs the colony-owner mode and SaveOnComplete.");
            if (!string.IsNullOrEmpty(configuration.Scenario) &&
                (configuration.Mode != "colony-owner" || configuration.Scenario.Split(',').Any(name => !Scenarios.Contains(name, StringComparer.Ordinal))))
                throw new InvalidDataException("A colony scenario needs the colony-owner mode and one of: " + string.Join(", ", Scenarios) + ".");
            if (!longRun && (configuration.CompletedFrames > MaximumFrames || configuration.TimeoutSeconds > MaximumTimeoutSeconds))
                throw new InvalidDataException("More than " + MaximumFrames + " frames or " + MaximumTimeoutSeconds +
                    " seconds requires the explicit LongRun opt-in.");
            if (configuration.TelemetryIntervalFrames < MinimumTelemetryIntervalFrames || configuration.TelemetryIntervalFrames > MaximumTelemetryIntervalFrames)
                throw new InvalidDataException("Development telemetry requires " + MinimumTelemetryIntervalFrames + ".." +
                    MaximumTelemetryIntervalFrames + " native frames per line.");
            if (!longRun && configuration.TelemetryIntervalFrames != DefaultTelemetryIntervalFrames)
                throw new InvalidDataException("A development telemetry interval requires the explicit LongRun opt-in.");
            if (!longRun) return;
            if (!LongRunModes.Contains(configuration.Mode, StringComparer.Ordinal))
                throw new InvalidDataException("Long runs admit only " + string.Join(", ", LongRunModes) +
                    "; fixture and saved-colony replay scenes retain short exact evidence windows.");
            if (configuration.SaveOnComplete || configuration.SaveBeforeResume || configuration.VerifySaveIoRecovery ||
                configuration.VerifyNaturalAutosave || configuration.VerifyPersistentActors || configuration.VerifyLiveBreathing ||
                configuration.VerifyTransientParticles || configuration.SubnormalCarbonParticle || configuration.ReloadInProcess ||
                configuration.IdleReservoirLeaseOnly || configuration.PrivateOwnedLoadRejection ||
                !string.IsNullOrEmpty(configuration.LoadSavePath) || !string.IsNullOrEmpty(configuration.PrivateFileProtocolAssemblySha256))
                throw new InvalidDataException("Long runs exclude fixed-window save, reload, actor, breathing and particle controls; " +
                    "natural autosaves still use the ordinary paired save path.");
        }
    }
}
