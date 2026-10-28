using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // The settings the game uses (docs/settings.md). The player's choices live
    // in mods/config/MixedAtmospheres (config.json, presets/). A new colony
    // takes the chosen colony settings and records them; a loaded colony uses
    // what it recorded. Validation scenes never read the player's files: they
    // use Default, or the preset MA_COLONY_SETTINGS_PRESET names (a built-in
    // id or a preset file path), so harness results never depend on a
    // player's configuration.
    internal static class ModSettingsRuntime
    {
        private const string Library = "mixed_atmospheres_oni";
        [DllImport(Library, EntryPoint = "ma_oni_colony_options_version", CallingConvention = CallingConvention.Cdecl)] private static extern uint OptionsVersion();
        [DllImport(Library, EntryPoint = "ma_oni_colony_set_options", CallingConvention = CallingConvention.Cdecl)] private static extern int SetOptions(ref NativeColonyOptions options);
        [DllImport(Library, EntryPoint = "ma_oni_colony_set_options", CallingConvention = CallingConvention.Cdecl)] private static extern int ResetOptions(IntPtr none);
        [DllImport(Library, EntryPoint = "ma_oni_colony_copy_options", CallingConvention = CallingConvention.Cdecl)] private static extern int CopyOptions(ref NativeColonyOptions options);
        [DllImport(Library, EntryPoint = "ma_oni_last_error", CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr LastError();
        [DllImport(Library, EntryPoint = "ma_oni_colony_physical_core_count", CallingConvention = CallingConvention.Cdecl)] private static extern uint PhysicalCoreCount();
        [DllImport(Library, EntryPoint = "ma_oni_colony_set_worker_count", CallingConvention = CallingConvention.Cdecl)] private static extern int SetWorkerCount(uint workers);
        internal static uint PhysicalCores { get; private set; }
        private static uint? appliedWorkerCount;

        internal static ModSettingsStore? Store { get; private set; }
        // The player's values (colony settings for the next new colony, and preferences).
        internal static SettingValues Current { get; private set; } = SettingValues.Defaults();
        internal static string CurrentPresetId { get; private set; } = BuiltInPresets.DefaultId;
        internal static bool Validation => ColonyRuntimeValidation.Settings != null;
        internal static string ValidationPreset { get; private set; } = "";
        // Problems the player should see; the settings window lists them too.
        internal static readonly List<string> Notices = new List<string>();
        internal static event System.Action? PreferencesChanged;

        // The running colony's recorded colony settings; null without a colony.
        internal static SettingValues? Colony { get; private set; }
        internal static string ColonyDescription { get; private set; } = "";
        private static SettingValues? loadedRecord;   // from a save, until its owner restores
        private static SettingValues? startedWith;    // the values handed to the next start
        private static string startedPreset = BuiltInPresets.DefaultId;

        internal static void Initialize()
        {
            MixtureAppearanceSettings.ContrastChanged = (contrast, persist) => SetPreference("display.patternContrast", contrast, persist, apply: false);
            var root = Path.Combine(Util.RootFolder(), "mods", "config", "MixedAtmospheres");
            Store = new ModSettingsStore(root);
            if (Validation)
            {
                ValidationPreset = Environment.GetEnvironmentVariable("MA_COLONY_SETTINGS_PRESET") ?? "";
                var values = SettingValues.Defaults().With("display.readingPrecision", "exact");
                if (ValidationPreset.Length != 0)
                {
                    var preset = BuiltInPresets.Find(ValidationPreset) ?? (File.Exists(ValidationPreset) ?
                        ModSettingsStore.ParsePreset(Path.GetFileNameWithoutExtension(ValidationPreset), File.ReadAllText(ValidationPreset), new List<string>()) : null);
                    if (preset == null) throw new InvalidOperationException("MA_COLONY_SETTINGS_PRESET names no built-in preset or preset file: " + ValidationPreset);
                    // A preset file with preferences (e.g. friendly readings for captures) overrides exact.
                    values = preset.ApplyTo(values);
                    CurrentPresetId = preset.Id;
                }
                Current = values;
                Debug.Log("[Mixed Atmospheres] Validation scene: settings files ignored; colony settings " + CurrentPresetId + ".");
            }
            else
            {
                // Settings never stop the mod from loading.
                try
                {
                    var loaded = Store.Load();
                    Current = loaded.Values; CurrentPresetId = loaded.PresetId;
                    foreach (var warning in loaded.Warnings) Debug.LogWarning("[Mixed Atmospheres] Settings: " + warning);
                    foreach (var notice in loaded.Notices) Notice(notice);
                }
                catch (Exception error)
                {
                    Current = SettingValues.Defaults(); CurrentPresetId = BuiltInPresets.DefaultId;
                    Notice("Your settings could not be read, so Default applies (" + error.Message + ").");
                }
            }
            appliedWorkerCount = null;
            ApplyPreferences();
        }

        internal static void Notice(string message)
        {
            Debug.LogWarning("[Mixed Atmospheres] Settings: " + message);
            if (!Notices.Contains(message)) Notices.Add(message);
        }

        // Changes made in the settings window. Colony values affect only the
        // next new colony; preferences apply at once.
        internal static void Set(SettingValues values, string presetId)
        {
            Current = values; CurrentPresetId = presetId;
            ApplyPreferences();
            Persist();
        }
        // This gameplay switch can change in a running colony without changing
        // its fluid state or solver settings. The next save records the choice.
        internal static void SetCarbonDioxideLimits(bool enabled)
        {
            if (ColonyOwner.Active && Colony != null)
            {
                Colony = Colony.With(SettingCatalog.Co2Limits, enabled);
                var model = MixturePresentationRuntime.CurrentModel;
                if (model != null) MixturePresentationRuntime.Configure(model.WithBreathingSettings(Breathing));
                ColonyPresentation.InvalidateLifecycleReadings();
                ColonyDescription = "Breathing rules updated in this colony.";
                return;
            }
            Set(Current.With(SettingCatalog.Co2Limits, enabled), "custom");
        }

        internal static void Persist()
        {
            if (Validation || Store == null) return;
            try { Store.Save(Current, CurrentPresetId); }
            catch (Exception error) when (error is IOException || error is UnauthorizedAccessException)
            { Notice("Your settings could not be saved (" + error.Message + "). They apply until the game closes."); }
        }

        // Preferences that live elsewhere follow the current values.
        private static void ApplyPreferences()
        {
            ApplyWorkers();
            MixtureInspection.ExactReadings = Current.Choice("display.readingPrecision") == "exact";
            MixtureAppearanceSettings.Apply(Current.Number("display.patternContrast"), Current.Number("display.traceMoleFraction"));
            PreferencesChanged?.Invoke();
        }
        private static void ApplyWorkers()
        {
            var requested = (uint)Current.Number("performance.workerThreads");
            if (appliedWorkerCount == requested) return;
            try
            {
                PhysicalCores = PhysicalCoreCount();
                var result = SetWorkerCount(requested);
                if (result != 0) throw new InvalidOperationException(Marshal.PtrToStringAnsi(LastError()) ?? ("error " + result));
                appliedWorkerCount = requested;
            }
            catch (Exception error) when (!(error is OutOfMemoryException))
            { Notice("The solver worker setting could not be applied (" + error.Message + "). The previous worker count remains in use."); }
        }
        // The pattern contrast changed from the overlay legend.
        internal static void SetPreference(string key, object value, bool persist, bool apply = true)
        {
            Current = Current.With(key, value);
            if (apply) ApplyPreferences();
            if (persist) Persist();
        }

        // Right before a new colony's owner starts. Native refusal shows a
        // notice and starts the colony with Default.
        internal static SettingValues BeforeStart(uint flags)
        {
            var values = SettingValues.Defaults().WithScope(SettingScope.Colony, Current);
            startedPreset = CurrentPresetId;
            try
            {
                NativeColonyOptions.CheckLayout();
                if (OptionsVersion() != NativeColonyOptions.CurrentVersion) throw new NotSupportedException("the native library's settings version differs from this build");
                var native = values.ToNative();
                var result = SetOptions(ref native);
                if (result != 0) throw new InvalidOperationException(Marshal.PtrToStringAnsi(LastError()) ?? ("error " + result));
            }
            catch (Exception error) when (!(error is OutOfMemoryException))
            {
                try { _ = ResetOptions(IntPtr.Zero); } catch (Exception) { }
                values = SettingValues.Defaults(); startedPreset = BuiltInPresets.DefaultId;
                var message = "Your colony settings could not be applied (" + error.Message + "). This colony uses Default.";
                Notice(message);
                ColonyNotices.Show("Mixed Atmospheres settings", message);
            }
            if ((flags & OniNativeColony.GameSurface) == 0)
                Debug.LogWarning("[Mixed Atmospheres] A development selector chose an older profile; colony solver settings do not apply to it.");
            startedWith = values;
            return values;
        }
        internal static void AfterStart()
        {
            Colony = WithRunningSolver(startedWith ?? SettingValues.Defaults());
            ColonyDescription = "Started with " + PresetName(startedPreset) + ".";
            startedWith = null;
            Debug.Log("[Mixed Atmospheres] New colony settings: " + Describe(Colony) + ".");
        }

        // Every supported R&D save requires the current breathing record.
        internal static void LoadRecord(byte[]? bytes) =>
            loadedRecord = bytes == null ? throw new NotSupportedException("This R&D save predates the current breathing settings. Start a new colony.") : ColonySettingsRecord.Decode(bytes, SettingValues.Defaults());
        internal static void Restored()
        {
            Colony = WithRunningSolver(loadedRecord ?? throw new InvalidOperationException("The colony breathing settings were not loaded."));
            ColonyDescription = "Recorded in this colony's save.";
            loadedRecord = null;
            Debug.Log("[Mixed Atmospheres] Restored colony settings: " + Describe(Colony) + ".");
        }
        internal static byte[]? CaptureRecord() => Colony == null ? null : ColonySettingsRecord.Encode(Colony);
        internal static void Reset() { Colony = null; ColonyDescription = ""; loadedRecord = null; startedWith = null; }

        private static SettingValues WithRunningSolver(SettingValues basis)
        {
            var native = new NativeColonyOptions { Size = NativeColonyOptions.Bytes, Version = NativeColonyOptions.CurrentVersion };
            try { if (CopyOptions(ref native) == 0) return basis.WithNative(native); }
            catch (Exception error) when (error is EntryPointNotFoundException || error is FormatException)
            { Debug.LogWarning("[Mixed Atmospheres] The running colony's solver settings could not be read: " + error.Message); }
            return basis;
        }

        // Breathing of the running colony (easy-mode defaults without one).
        internal static AtmosphereSettings Breathing
        {
            get
            {
                var v = Colony ?? SettingValues.Defaults();
                var maximum = v.Number("breathing.maximumOxygenFraction");
                return new AtmosphereSettings
                {
                    HardMode = maximum < 1, MaximumOxygenFraction = maximum,
                    EnforceCarbonDioxideLimits = v.Toggle(SettingCatalog.Co2Limits),
                    MinimumOxygenFraction = v.Number("breathing.minimumOxygenFraction"),
                    MinimumOxygenPartialPressurePa = v.Number("breathing.minimumOxygenPressurePa"),
                    MaximumCo2Fraction = v.Number("breathing.maximumCo2Fraction"),
                    MaximumCo2PartialPressurePa = v.Number("breathing.maximumCo2PressurePa")
                };
            }
        }

        // A preset's display name; `known` avoids reading the preset files again.
        internal static string PresetName(string id, IEnumerable<ModSettingsStore.PresetEntry>? known = null)
        {
            if (BuiltInPresets.Find(id) is SettingsPreset builtIn) return builtIn.Name;
            if (id == "custom") return "custom settings";
            if (Validation) return id;
            try { return (known ?? Store?.ListPresets())?.FirstOrDefault(e => e.Id == id)?.Preset?.Name ?? id; }
            catch (Exception) { return id; }
        }
        internal static string Describe(SettingValues values) =>
            string.Join(", ", SettingCatalog.All.Where(d => d.Scope == SettingScope.Colony).Select(d => d.Key + "=" + SettingValues.Describe(values[d.Key])));
        // For reports: the recorded colony settings as a flat map.
        internal static object? Report() => Colony == null ? null : new
        {
            Preset = CurrentPresetId, ColonyDescription, Record = CaptureRecord() != null,
            Values = SettingCatalog.All.Where(d => d.Scope == SettingScope.Colony).ToDictionary(d => d.Key, d => Colony[d.Key])
        };

        internal static void OpenFolder()
        {
            if (Store == null) return;
            try
            {
                Directory.CreateDirectory(Store.Root);
                Application.OpenURL(new Uri(Path.GetFullPath(Store.Root) + Path.DirectorySeparatorChar).AbsoluteUri);
            }
            catch (Exception error) { Notice("The settings folder could not be opened (" + error.Message + "): " + Store.Root); }
        }
    }
}
