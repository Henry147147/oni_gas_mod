using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using MixedAtmospheres.Oni;

// Settings model, files, presets and the colony settings save record
// (docs/settings.md). No game files are needed.
static class Program
{
    private static int checks;
    private static void Check(bool ok, string why) { ++checks; if (!ok) throw new Exception(why); }
    private static void Throws<T>(Action action, string why) where T : Exception
    {
        try { action(); } catch (T) { ++checks; return; }
        throw new Exception("Expected " + typeof(T).Name + ": " + why);
    }

    // The native refusal ranges (native/src/oni_colony.cpp validate_options).
    private static bool NativeAccepts(NativeColonyOptions o) =>
        o.Size == 88 && o.Version == 2 && o.EvaporationBelowBoiling <= 1 && o.Reserved == 0 && o.PressureMaximumIterations >= 400 && o.PressureMaximumIterations <= 65536 &&
        o.HeatMaximumIterations >= 800 && o.HeatMaximumIterations <= 65536 && o.FlowParts >= 1 && o.FlowParts <= 8 && o.SharpenLiquidInterfaces <= 1 &&
        (o.MaximumTimestepSeconds == (double).2f || o.MaximumTimestepSeconds == (double).1f || o.MaximumTimestepSeconds == (double).05f) &&
        o.TraceSinkThresholdKg >= 1e-11 && o.TraceSinkThresholdKg <= 1e-6 && o.LiquidStiffnessRatio >= .01 && o.LiquidStiffnessRatio <= 1 &&
        o.PressureRelativeTolerance >= 1e-12 && o.PressureRelativeTolerance <= 1e-4 && o.GasHeatMixing >= 0 && o.GasHeatMixing <= 32 &&
        o.InterfaceDetailFraction >= 0 && o.InterfaceDetailFraction <= .5 && o.PhaseRelaxationSeconds >= .1 && o.PhaseRelaxationSeconds <= 10;

    private static void Catalog()
    {
        NativeColonyOptions.CheckLayout();
        var keys = new HashSet<string>();
        foreach (var d in SettingCatalog.All)
        {
            Check(keys.Add(d.Key), "duplicate key " + d.Key);
            Check(Equals(d.Accept(d.Default), d.Default), "default rejected: " + d.Key);
            Check(!d.IsExperimental(d.Default), "default is experimental: " + d.Key);
            foreach (var step in d.Steps) d.Accept(step);
            Check(d.Key.StartsWith(d.Scope == SettingScope.Colony ? "solver." : "", StringComparison.Ordinal) || d.Key.StartsWith("breathing.", StringComparison.Ordinal) ||
                d.Key == SettingCatalog.EvaporationBelowBoiling || d.Key == SettingCatalog.RegionActivity || d.Key == SettingCatalog.SourceEnergy ||
                d.Scope == SettingScope.Preference,
                "colony keys are solver, breathing, activity, the phase rule or the source energy: " + d.Key);
        }
        // Default reproduces today's profile 5 exactly (oni_colony.cpp default_options_value).
        var native = SettingValues.Defaults().ToNative();
        Check(native.PressureMaximumIterations == 400 && native.HeatMaximumIterations == 800 && native.FlowParts == 2 && native.SharpenLiquidInterfaces == 1 &&
              native.MaximumTimestepSeconds == (double).2f && native.TraceSinkThresholdKg == 1e-11 && native.LiquidStiffnessRatio == .04 &&
              native.PressureRelativeTolerance == 1e-8 && native.GasHeatMixing == 8 && native.InterfaceDetailFraction == 0 && native.PhaseRelaxationSeconds == 1 &&
              native.Size == 88 && native.Version == 2 && native.EvaporationBelowBoiling == 0 && native.Reserved == 0,
              "Default native options differ from today's profile 6 with boil-only phase change");
        // Boil-only phase change: on by default (no evaporation below boiling), a basic Gameplay
        // colony setting saved in the physical image (not the breathing record), never experimental.
        var evaporation = SettingCatalog.Get(SettingCatalog.EvaporationBelowBoiling);
        Check(evaporation.Kind == SettingKind.Toggle && evaporation.Scope == SettingScope.Colony && !evaporation.Advanced &&
              evaporation.Group == SettingCatalog.PhaseChange && (bool)evaporation.Default == false && !evaporation.IsExperimental(true) &&
              evaporation.Label == "Evaporation below boiling", "phase rule definition");
        var realistic = SettingValues.Defaults().With(SettingCatalog.EvaporationBelowBoiling, true);
        Check(realistic.ToNative().EvaporationBelowBoiling == 1 && NativeAccepts(realistic.ToNative()), "evaporation below boiling native value");
        Check(SettingValues.Defaults().WithNative(realistic.ToNative()).Toggle(SettingCatalog.EvaporationBelowBoiling) &&
              !realistic.WithNative(SettingValues.Defaults().ToNative()).Toggle(SettingCatalog.EvaporationBelowBoiling), "phase rule native round trip");
        Check(ColonySettingsRecord.Encode(realistic).SequenceEqual(ColonySettingsRecord.Encode(SettingValues.Defaults())), "phase rule entered the breathing record");
        Check(realistic.WithGroup(SettingCatalog.Solver, BuiltInPresets.Find("precise")!.Values).Toggle(SettingCatalog.EvaporationBelowBoiling), "solver preset changed the phase rule");
        Check(BuiltInPresets.All.All(p => !p.Values.Toggle(SettingCatalog.EvaporationBelowBoiling)), "a built-in preset enables evaporation below boiling");
        var badRule = realistic.ToNative(); badRule.EvaporationBelowBoiling = 2;
        Throws<FormatException>(() => SettingValues.Defaults().WithNative(badRule), "invalid native phase rule");
        var activity = SettingCatalog.Get(SettingCatalog.RegionActivity);
        Check(activity.Kind == SettingKind.Choice && activity.Scope == SettingScope.Colony && !activity.Advanced &&
              activity.Group == SettingCatalog.Simulation && (string)activity.Default == "tiers", "activity policy definition");
        var always = SettingValues.Defaults().With(SettingCatalog.RegionActivity, "always");
        Check(always.Choice(SettingCatalog.RegionActivity) == "always" && always.ToNative().Reserved == 0, "activity policy leaves options ABI unchanged");
        Check(ColonySettingsRecord.Encode(always).SequenceEqual(ColonySettingsRecord.Encode(SettingValues.Defaults())), "activity policy entered breathing record");
        Check(always.WithGroup(SettingCatalog.Solver, BuiltInPresets.Find("precise")!.Values).Choice(SettingCatalog.RegionActivity) == "always", "solver preset changed activity policy");
        Throws<FormatException>(() => always.With(SettingCatalog.RegionActivity, "sometimes"), "invalid activity policy");
        // Gas source energy: compensated by default (user decision 2026-11-09), a basic
        // Gameplay colony setting saved in the colony record, not in the options ABI.
        var energy = SettingCatalog.Get(SettingCatalog.SourceEnergy);
        Check(energy.Kind == SettingKind.Choice && energy.Scope == SettingScope.Colony && !energy.Advanced && energy.Group == SettingCatalog.GasSources &&
              (string)energy.Default == "compensated" && ColonySettingsRecord.SourceEnergyMode(SettingValues.Defaults()) == 2, "gas source energy definition");
        Check(SettingCatalog.SourceEnergyChoices.Select((c, mode) => ColonySettingsRecord.SourceEnergyMode(SettingValues.Defaults().With(SettingCatalog.SourceEnergy, c)) == mode).All(x => x),
              "gas source energy native modes");
        foreach (var choice in SettingCatalog.SourceEnergyChoices)
        {
            var chosenEnergy = SettingValues.Defaults().With(SettingCatalog.SourceEnergy, choice);
            Check(chosenEnergy.ToNative().Reserved == 0 && NativeAccepts(chosenEnergy.ToNative()) &&
                  ColonySettingsRecord.Decode(ColonySettingsRecord.Encode(chosenEnergy), SettingValues.Defaults()).Choice(SettingCatalog.SourceEnergy) == choice &&
                  chosenEnergy.WithGroup(SettingCatalog.Solver, BuiltInPresets.Find("precise")!.Values).Choice(SettingCatalog.SourceEnergy) == choice,
                  "gas source energy record, options ABI or solver preset: " + choice);
        }
        Check(BuiltInPresets.All.All(p => p.Values.Choice(SettingCatalog.SourceEnergy) == "compensated"), "a built-in preset changes the gas source energy");
        Throws<FormatException>(() => ColonySettingsRecord.WithSourceEnergyMode(SettingValues.Defaults(), 3), "invalid native gas source energy");
        // Toxic gas limit: 5% by default, an advanced breathing threshold saved in
        // the colony record (version 5); 100% means no limit.
        var toxic = SettingCatalog.Get(SettingCatalog.ToxicLimit);
        Check(toxic.Kind == SettingKind.Number && toxic.Scope == SettingScope.Colony && toxic.Advanced && toxic.Group == SettingCatalog.Breathing &&
              (double)toxic.Default == .05 && toxic.Minimum == 0 && toxic.Maximum == 1 && toxic.Steps.Contains(1.0) && toxic.Label == "Maximum toxic gas share",
              "toxic gas limit definition");
        Check(BuiltInPresets.All.All(p => p.Values.Number(SettingCatalog.ToxicLimit) == .05), "a built-in preset changes the toxic gas limit");
        foreach (var invalid in new[] { -.01, 1.01, double.NaN })
            Throws<FormatException>(() => SettingValues.Defaults().With(SettingCatalog.ToxicLimit, invalid), "invalid toxic gas limit " + invalid);
        // Every offered step of every solver setting, and every built-in preset, is inside the native ranges.
        foreach (var d in SettingCatalog.All.Where(d => d.Key.StartsWith("solver.", StringComparison.Ordinal)))
            foreach (var step in d.Kind == SettingKind.Toggle ? new object[] { true, false } : d.Steps.Cast<object>())
                Check(NativeAccepts(SettingValues.Defaults().With(d.Key, step).ToNative()), "native refuses " + d.Key + " = " + step);
        foreach (var p in BuiltInPresets.All) Check(NativeAccepts(p.Values.ToNative()), "native refuses preset " + p.Id);
        // Experimental marks.
        Check(BuiltInPresets.All.Select(p => p.Id).SequenceEqual(new[] { "default", "robust", "fast", "precise", "relaxed-breathing", "strict-breathing" }), "built-in preset list");
        foreach (var id in new[] { "default", "robust", "relaxed-breathing", "strict-breathing" }) Check(!BuiltInPresets.Find(id)!.Experimental, id + " marked experimental");
        Check(!SettingValues.Defaults().Toggle(SettingCatalog.Co2Limits), "CO2 limits off by default");
        // When switched on, CO2 limits default to 4% and 5 kPa, which bind together in normal base air of
        // about 125 kPa (0.1.4, molecular oxygen; 2% and 5 kPa in 0.1.3, 0.5% and 500 Pa before).
        Check(SettingValues.Defaults().Number("breathing.maximumCo2Fraction") == .04 && SettingValues.Defaults().Number("breathing.maximumCo2PressurePa") == 5000,
              "CO2 threshold defaults");
        // Relaxed breathing lowers only the oxygen minimums and keeps the CO2 switch as the player set it;
        // Strict switches CO2 limits on below the defaults.
        var relaxed = BuiltInPresets.Find("relaxed-breathing")!;
        foreach (var co2 in new[] { false, true })
        {
            var before = SettingValues.Defaults().With(SettingCatalog.Co2Limits, co2).With("solver.flowParts", 4.0).With("breathing.maximumCo2Fraction", .05);
            var after = relaxed.ApplyTo(before);
            Check(after.Toggle(SettingCatalog.Co2Limits) == co2 && after.Number("breathing.minimumOxygenFraction") == .1 &&
                  after.Number("breathing.minimumOxygenPressurePa") == 10000 && after.Number("solver.flowParts") == 2 &&
                  after.Number("breathing.maximumCo2Fraction") == .04 && relaxed.Matches(after) && !BuiltInPresets.Default.Matches(after),
                  "relaxed breathing kept the CO2 switch " + co2);
        }
        var strict = BuiltInPresets.Find("strict-breathing")!;
        Check(strict.Values.Toggle(SettingCatalog.Co2Limits) && strict.Values.Number("breathing.maximumCo2Fraction") == .02 &&
              strict.Values.Number("breathing.maximumCo2PressurePa") == 2500 && strict.Kept.Count == 0 &&
              strict.ApplyTo(SettingValues.Defaults()).Toggle(SettingCatalog.Co2Limits), "strict breathing CO2 limits");
        Check(BuiltInPresets.All.Where(p => p.Id != "relaxed-breathing").All(p => p.Kept.Count == 0 && p.Matches(p.Values)) &&
              BuiltInPresets.Default.Matches(SettingValues.Defaults()), "presets match their own values");
        Check(SettingCatalog.Get(SettingCatalog.Co2Limits).Advanced == false &&
            SettingCatalog.All.Where(d => d.Group == SettingCatalog.Solver).All(d => d.Advanced), "basic gameplay and advanced solver classification");
        var chosen = SettingValues.Defaults().With(SettingCatalog.Co2Limits, true);
        Check(chosen.WithGroup(SettingCatalog.Solver, BuiltInPresets.Find("fast")!.Values).Toggle(SettingCatalog.Co2Limits), "solver preset preserves gameplay choice");
        Check(!SettingCatalog.All.Any(d => d.Key == "breathing.nitrogenAir") && BuiltInPresets.Find("hard-mode") == null, "removed nitrogen controls");
        // Development-region controls stay settings (saved files keep them) but the window lists them only in development sessions.
        Check(SettingCatalog.All.Where(d => d.DevelopmentOnly).Select(d => d.Key).SequenceEqual(new[] { "panel.showBoundary", "keys.findFluid", "keys.toggleBoundary" }) &&
              SettingCatalog.All.Where(d => d.DevelopmentOnly).All(d => d.Scope == SettingScope.Preference), "development-only controls");
        foreach (var id in new[] { "fast", "precise" }) Check(BuiltInPresets.Find(id)!.Experimental, id + " not marked experimental");
        Check(SettingValues.Defaults().With("solver.sharpenLiquidInterfaces", false).Experimental(SettingScope.Colony).Any(), "sharpening off not experimental");
        Check(SettingValues.Defaults().With("solver.pressureTolerance", 1e-6).Experimental(SettingScope.Colony).Any(), "looser tolerance not experimental");
        Check(!SettingValues.Defaults().With("solver.pressureIterationCap", 6400.0).Experimental(SettingScope.Colony).Any(), "higher cap marked experimental");
        var workers = SettingCatalog.Get("performance.workerThreads");
        Check(workers.Scope == SettingScope.Preference && workers.Integer && (double)workers.Default == 0 &&
              workers.Steps.SequenceEqual(Enumerable.Range(0, 257).Select(n => (double)n)), "worker preference defaults or offered counts");
        foreach (var invalid in new[] { -1.0, 1.5, 257.0 })
            Throws<FormatException>(() => SettingValues.Defaults().With(workers.Key, invalid), "invalid worker count");
        Check(ColonySettingsRecord.Encode(SettingValues.Defaults().With(workers.Key, 30.0)).SequenceEqual(ColonySettingsRecord.Encode(SettingValues.Defaults())),
              "worker preference entered the colony save");
        Check(BuiltInPresets.Find("robust")!.Values.ToNative().PressureMaximumIterations == 1600, "robust caps");
        Check(BuiltInPresets.Find("precise")!.Values.ToNative().MaximumTimestepSeconds == (double).1f, "precise steps");
        // Values outside ranges and of the wrong type are refused; lower caps are never offered.
        Throws<FormatException>(() => SettingValues.Defaults().With("solver.pressureIterationCap", 200.0), "lower cap");
        Throws<FormatException>(() => SettingValues.Defaults().With("solver.stepsPerFrame", 2.5), "fractional steps");
        Throws<FormatException>(() => SettingValues.Defaults().With("solver.stepsPerFrame", 3.0).ToNative(), "three steps");
        Throws<FormatException>(() => SettingValues.Defaults().With("solver.gasHeatMixing", "8"), "text number");
        Throws<FormatException>(() => SettingValues.Defaults().With("keys.findFluid", "Q"), "unknown key");
        Throws<FormatException>(() => SettingValues.Defaults().With("solver.pressureTolerance", double.NaN), "NaN");
        Throws<KeyNotFoundException>(() => SettingValues.Defaults().With("solver.kernel", 1.0), "no kernel selector");
        // No setting selects a solver, kernel, profile or pipe mode.
        Check(!SettingCatalog.All.Any(d => d.Key.Contains("kernel") || d.Key.Contains("profile") || d.Key.Contains("pipe") || d.Key.Contains("floor")), "a solver selector is exposed");
        // A running colony's options round trip.
        var precise = BuiltInPresets.Find("precise")!.Values;
        Check(SettingValues.Defaults().WithNative(precise.ToNative()).ScopeEquals(SettingScope.Colony, precise.WithScope(SettingScope.Colony, precise).With("breathing.minimumOxygenFraction", .15)), "native round trip");
    }

    private static void Json()
    {
        var parsed = (Dictionary<string, object?>)ModSettingsJson.Parse("﻿ { \"a\": 1e-11, \"b\": \"x\\\"\\u00e9\\n\", \"c\": true, \"d\": null, \"e\": {\"f\": -0.5} } ")!;
        Check((double)parsed["a"]! == 1e-11 && (string)parsed["b"]! == "x\"é\n" && (bool)parsed["c"]! && parsed["d"] == null &&
              (double)((Dictionary<string, object?>)parsed["e"]!)["f"]! == -.5, "parse");
        foreach (var bad in new[] { "", "{", "{\"a\":1,}", "{\"a\":1,\"a\":2}", "{\"a\":[1]}", "{\"a\":01}", "{\"a\":1} x", "{\"a\":NaN}", "{\"a\":1e999}",
                                    "{a:1}", "{\"a\":\"\u0001\"}", "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":{\"i\":{}}}}}}}}}}" })
            Throws<FormatException>(() => ModSettingsJson.Parse(bad), "bad JSON " + bad);
        Throws<FormatException>(() => ModSettingsJson.Parse(new string(' ', ModSettingsJson.MaximumCharacters + 1)), "oversized");
        var text = ModSettingsJson.Write(new[] { new KeyValuePair<string, object?>("n", 0.1 + 0.2), new KeyValuePair<string, object?>("s", "a\\\"\t") });
        var back = (Dictionary<string, object?>)ModSettingsJson.Parse(text)!;
        Check((double)back["n"]! == 0.1 + 0.2 && (string)back["s"]! == "a\\\"\t", "write round trip");
    }

    private static void Store(string root)
    {
        var store = new ModSettingsStore(root);
        var loaded = store.Load();
        Check(loaded.Values.Equals(SettingValues.Defaults()) && loaded.PresetId == "default" && loaded.Notices.Count == 0, "missing config is Default");
        // The older appearance.json supplies the pattern contrast once.
        Directory.CreateDirectory(root);
        File.WriteAllText(Path.Combine(root, "appearance.json"), "{\"PatternEnabled\":false,\"PatternContrast\":0.08}");
        Check(store.Load().Values.Number("display.patternContrast") == 0, "appearance off migrates to 0");
        File.WriteAllText(Path.Combine(root, "appearance.json"), "{\"PatternEnabled\":true,\"PatternContrast\":0.08}");
        Check(store.Load().Values.Number("display.patternContrast") == .08, "appearance contrast migrates");
        // Round trip of every value.
        var values = BuiltInPresets.Find("strict-breathing")!.Values.With("display.readingPrecision", "exact").With("keys.findFluid", "F7").With("panel.expanded", false).With("performance.workerThreads", 30.0);
        store.Save(values, "strict-breathing");
        loaded = store.Load();
        Check(loaded.Values.Equals(values) && loaded.PresetId == "strict-breathing" && loaded.Notices.Count == 0, "config round trip");
        var legacy = File.ReadAllText(store.ConfigPath).Replace("\"performance.workerThreads\": 30,", "");
        File.WriteAllText(store.ConfigPath, legacy);
        Check(store.Load().Values.Number("performance.workerThreads") == 0, "old config did not default to physical cores");
        store.Save(values, "strict-breathing");
        // Unknown keys are reported, not fatal.
        var withUnknown = File.ReadAllText(store.ConfigPath).Replace("\"Colony\": {", "\"Colony\": {\n    \"solver.futureThing\": 3,");
        File.WriteAllText(store.ConfigPath, withUnknown);
        loaded = store.Load();
        Check(loaded.Values.Equals(values) && loaded.Notices.Count == 0 && loaded.Warnings.Count == 1, "unknown key keeps the rest");
        // Invalid files fall back to Default with a notice and are kept aside on the next save.
        foreach (var bad in new[] { "not json", "{\"Format\":\"MixedAtmospheres.Settings\",\"Version\":2}", "{\"Format\":\"Other\",\"Version\":1}",
                                    File.ReadAllText(store.ConfigPath).Replace("\"solver.pressureIterationCap\": 400", "\"solver.pressureIterationCap\": 10"),
                                    File.ReadAllText(store.ConfigPath).Replace("\"keys.findFluid\": \"F7\"", "\"keys.findFluid\": 7") })
        {
            File.WriteAllText(store.ConfigPath, bad);
            loaded = store.Load();
            Check(loaded.Values.Equals(SettingValues.Defaults()) && loaded.PresetId == "default" && loaded.Notices.Count == 1, "invalid config falls back: " + bad.Substring(0, Math.Min(40, bad.Length)));
        }
        store.Save(SettingValues.Defaults(), "default");
        Check(File.Exists(store.ConfigPath + ".invalid") && store.Load().Notices.Count == 0, "invalid config kept aside");

        // Presets: save, list, overwrite, rename, delete, import, export.
        var mine = BuiltInPresets.Find("robust")!.Values.With("breathing.minimumOxygenFraction", .12).With("keys.toggleDetails", "F2").With("performance.workerThreads", 22.0);
        var id = store.SavePreset("My Colony: Robust/Thin air", "caps and thin air", mine, false);
        Check(id == "my-colony-robust-thin-air" && File.Exists(store.PathOf(id)), "preset saved under its slug");
        Throws<ArgumentException>(() => store.SavePreset("My colony robust thin air", "", mine, false), "duplicate refused without overwrite");
        store.SavePreset("My colony robust thin air", "replaced", mine.With("breathing.minimumOxygenFraction", .1), true);
        var listed = store.ListPresets();
        Check(listed.Count == 1 && listed[0].Preset!.Name == "My colony robust thin air" && listed[0].Preset!.Values.Number("breathing.minimumOxygenFraction") == .1 &&
              listed[0].Preset!.HasPreferences && listed[0].Preset!.Description == "replaced", "overwrite");
        Throws<ArgumentException>(() => store.SavePreset("Default", "", mine, true), "built-in name refused");
        Throws<ArgumentException>(() => store.SavePreset("  ", "", mine, true), "empty name refused");
        var renamed = store.RenamePreset(id, "Thin air");
        Check(renamed == "thin-air" && !File.Exists(store.PathOf(id)) && store.FindUserPreset("thin-air")!.Name == "Thin air", "rename");
        var exported = File.ReadAllText(store.PathOf(renamed));
        var imported = store.ImportPreset(exported, new List<string>());
        Check(imported == "thin-air-2" && store.FindUserPreset(imported)!.Values.Equals(store.FindUserPreset(renamed)!.Values), "import of an existing name");
        var builtInName = ModSettingsStore.PresetText("Fast", "", SettingValues.Defaults(), false);
        var importedBuiltIn = store.ImportPreset(builtInName, new List<string>());
        Check(importedBuiltIn == "my-fast" && !store.FindUserPreset(importedBuiltIn)!.HasPreferences, "import of a built-in name");
        Throws<FormatException>(() => store.ImportPreset("{\"Format\":\"MixedAtmospheres.Preset\",\"Version\":1,\"Colony\":{\"solver.flowParts\":99}}", new List<string>()), "invalid import refused");
        Throws<FormatException>(() => store.ImportPreset("{\"Format\":\"MixedAtmospheres.Settings\",\"Version\":1}", new List<string>()), "settings file is not a preset");
        // A broken preset file is listed with its error and breaks nothing.
        File.WriteAllText(Path.Combine(store.PresetDirectory, "broken.json"), "{\"Format\":\"MixedAtmospheres.Preset\",\"Version\":1,\"Colony\":{\"solver.gasHeatMixing\":-1}}");
        listed = store.ListPresets();
        Check(listed.Count == 4 && listed.Single(p => p.Id == "broken").Preset == null && listed.Single(p => p.Id == "broken").Error.Length != 0, "broken preset listed");
        store.DeletePreset("broken"); store.DeletePreset(importedBuiltIn);
        Throws<ArgumentException>(() => store.DeletePreset("default"), "built-in delete refused");
        Throws<ArgumentException>(() => store.PathOf("../x"), "path escape refused");
        Check(store.ListPresets().Count == 2, "delete");
        // Applying: a built-in sets colony settings only; a saved preset also its preferences.
        var current = SettingValues.Defaults().With("display.readingPrecision", "exact").With("solver.flowParts", 4.0).With("performance.workerThreads", 30.0);
        var applied = BuiltInPresets.Find("strict-breathing")!.ApplyTo(current);
        Check(applied.Choice("display.readingPrecision") == "exact" && applied.Number("solver.flowParts") == 2 && applied.Number("breathing.minimumOxygenFraction") == .18 && applied.Number("performance.workerThreads") == 30, "built-in apply");
        applied = store.FindUserPreset("thin-air")!.ApplyTo(current);
        Check(applied.Choice("display.readingPrecision") == "friendly" && applied.Choice("keys.toggleDetails") == "F2" && applied.Number("solver.pressureIterationCap") == 1600 && applied.Number("performance.workerThreads") == 22, "saved preset apply");
        Check(ModSettingsStore.Slug("Ünïcode / ☃ name") == "n-code-name" && ModSettingsStore.Slug("***") == "preset", "slug");
    }

    private static void Record()
    {
        foreach (var values in new[] { SettingValues.Defaults(), BuiltInPresets.Find("strict-breathing")!.Values,
            SettingValues.Defaults().With(SettingCatalog.Co2Limits, true), SettingValues.Defaults().With(SettingCatalog.ToxicLimit, 1.0),
            SettingValues.Defaults().With(SettingCatalog.ToxicLimit, .2) })
        {
            var bytes = ColonySettingsRecord.Encode(values);
            Check(bytes.Length == 72 && BitConverter.ToUInt32(bytes, 0) == 5 && BitConverter.ToUInt32(bytes, 4) == 8 &&
                  BitConverter.ToDouble(bytes, 56) == 2 && BitConverter.ToDouble(bytes, 64) == values.Number(SettingCatalog.ToxicLimit), "v5 record layout");
            // A version 4 record (gas source energy, 0.1.2) loads with the basis toxic gas limit.
            var v4 = bytes.AsSpan(0, 64).ToArray(); v4[0] = 4; v4[4] = 7;
            var fromV4 = ColonySettingsRecord.Decode(v4, SettingValues.Defaults().With(SettingCatalog.ToxicLimit, .5));
            Check(fromV4.ScopeEquals(SettingScope.Colony, values.With(SettingCatalog.ToxicLimit, .5)), "v4 record round trip");
            Check(ColonySettingsRecord.Decode(v4, SettingValues.Defaults()).Number(SettingCatalog.ToxicLimit) == .05, "a v4 record loads with the default toxic gas limit");
            // A version 3 record (0.1.2 and earlier) loads with the default source energy and toxic gas limit.
            var v3 = bytes.AsSpan(0, 56).ToArray(); v3[0] = 3; v3[4] = 6;
            var fromV3 = ColonySettingsRecord.Decode(v3, SettingValues.Defaults().With(SettingCatalog.SourceEnergy, "internal"));
            Check(fromV3.ScopeEquals(SettingScope.Colony, values.With(SettingCatalog.SourceEnergy, "internal").With(SettingCatalog.ToxicLimit, .05)), "v3 record round trip");
            var badEnergy = (byte[])bytes.Clone(); BitConverter.GetBytes(3.0).CopyTo(badEnergy, 56);
            Throws<InvalidDataException>(() => ColonySettingsRecord.Decode(badEnergy, SettingValues.Defaults()), "invalid gas source energy");
            foreach (var limit in new[] { 1.5, -.1, double.NaN })
            {
                var badToxic = (byte[])bytes.Clone(); BitConverter.GetBytes(limit).CopyTo(badToxic, 64);
                Throws<InvalidDataException>(() => ColonySettingsRecord.Decode(badToxic, SettingValues.Defaults()), "invalid toxic gas limit");
            }
            Check(ColonySettingsRecord.Decode(bytes, SettingValues.Defaults()).ScopeEquals(SettingScope.Colony, values), "record round trip");
            Check(ColonySettingsRecord.Decode(bytes, SettingValues.Defaults().With(SettingCatalog.Co2Limits, !values.Toggle(SettingCatalog.Co2Limits))).Toggle(SettingCatalog.Co2Limits) == values.Toggle(SettingCatalog.Co2Limits), "saved switch overrides preferences");
            Throws<InvalidDataException>(() => ColonySettingsRecord.Decode(bytes.AsSpan(0, 20), SettingValues.Defaults()), "truncated record");
            var outside = (byte[])bytes.Clone(); BitConverter.GetBytes(2.0).CopyTo(outside, 8);
            Throws<InvalidDataException>(() => ColonySettingsRecord.Decode(outside, SettingValues.Defaults()), "out-of-range record");
            foreach (var flag in new[] { .5, double.NaN, double.PositiveInfinity })
            {
                var bad = (byte[])bytes.Clone(); BitConverter.GetBytes(flag).CopyTo(bad, 48);
                Throws<InvalidDataException>(() => ColonySettingsRecord.Decode(bad, SettingValues.Defaults()), "invalid switch");
            }
            foreach (var version in new byte[] { 1, 2, 6 })
            {
                var bad = (byte[])bytes.Clone(); bad[0] = version;
                Throws<NotSupportedException>(() => ColonySettingsRecord.Decode(bad, SettingValues.Defaults()), "unsupported R&D record");
            }
            var wrongCount = (byte[])bytes.Clone(); wrongCount[4] = 5;
            Throws<InvalidDataException>(() => ColonySettingsRecord.Decode(wrongCount, SettingValues.Defaults()), "record count");
        }
    }

    // Changes in a loaded colony (docs/settings.md): which part of the owner
    // takes each key, what each game profile locks, and refusals.
    private static void LiveColony()
    {
        var profile12 = ColonyCapabilities.Of(true, true, true, true);
        var profile8 = ColonyCapabilities.Of(true, true, false, true);
        var older = ColonyCapabilities.Of(true, false, false, true);
        var development = ColonyCapabilities.Of(false, false, false, false);
        var colonyKeys = SettingCatalog.All.Where(d => d.Scope == SettingScope.Colony).Select(d => d.Key).ToArray();
        Check(colonyKeys.All(k => profile12.Lock(k) == null), "an activity profile locks a colony setting");
        Check(colonyKeys.All(k => (profile8.Lock(k) == null) == (k != SettingCatalog.RegionActivity)) &&
              profile8.Lock(SettingCatalog.RegionActivity)!.Contains("always simulates"), "profiles 6-8 lock only region simulation");
        Check(colonyKeys.All(k => (older.Lock(k) == null) == (k.StartsWith("breathing.", StringComparison.Ordinal) || k == SettingCatalog.SourceEnergy)) &&
              colonyKeys.All(k => (development.Lock(k) == null) == (k.StartsWith("breathing.", StringComparison.Ordinal) || k == SettingCatalog.SourceEnergy)),
              "older and development solvers keep only breathing and the gas source energy editable");
        Check(colonyKeys.All(k => ColonyCapabilities.None.Lock(k) == ColonyCapabilities.NotSimulated) &&
              SettingCatalog.All.Where(d => d.Scope == SettingScope.Preference).All(d => ColonyCapabilities.None.Lock(d.Key) == null && profile8.Lock(d.Key) == null),
              "without a colony every colony setting is locked and no preference is");
        var basis = SettingValues.Defaults();
        var plan = ColonyChange.Plan(basis, basis.With("solver.pressureIterationCap", 1600.0), profile12);
        Check(plan.Keys.SequenceEqual(new[] { "solver.pressureIterationCap" }) && plan.Options && !plan.Activity && !plan.Breathing && plan.Refusal == null,
              "a solver tunable is an owner option");
        Check(plan.Describe(basis, basis.With("solver.pressureIterationCap", 1600.0)) == "solver.pressureIterationCap 400 → 1600", "change log text");
        plan = ColonyChange.Plan(basis, basis.With(SettingCatalog.EvaporationBelowBoiling, true), profile8);
        Check(plan.Options && !plan.Activity && !plan.Breathing && plan.Refusal == null, "evaporation below boiling is an owner option on profile 8");
        plan = ColonyChange.Plan(basis, basis.With(SettingCatalog.RegionActivity, "always"), profile12);
        Check(plan.Activity && !plan.Options && !plan.Breathing && plan.Refusal == null, "region simulation is the activity policy");
        Check(ColonyChange.Plan(basis, basis.With(SettingCatalog.RegionActivity, "always"), profile8).Refusal ==
              "Region simulation: " + profile8.ActivityReason, "profile 8 refuses region simulation with its reason");
        foreach (var key in new[] { "breathing.minimumOxygenFraction", "breathing.maximumCo2PressurePa", SettingCatalog.Co2Limits, SettingCatalog.ToxicLimit })
        {
            var d = SettingCatalog.Get(key);
            plan = ColonyChange.Plan(basis, basis.With(key, d.Kind == SettingKind.Toggle ? (object)true : d.Steps[0]), development);
            Check(plan.Breathing && !plan.Options && !plan.Activity && plan.Refusal == null, "breathing applies in managed code: " + key);
        }
        Check(ColonyChange.Plan(basis, basis.With("solver.flowParts", 3.0), development).Refusal!.StartsWith("Flow parts per step: ", StringComparison.Ordinal),
              "a locked refusal names the setting");
        plan = ColonyChange.Plan(basis, basis.With(SettingCatalog.SourceEnergy, "enthalpy"), older);
        Check(plan.SourceEnergy && !plan.Options && !plan.Activity && !plan.Breathing && plan.Refusal == null &&
              plan.Describe(basis, basis.With(SettingCatalog.SourceEnergy, "enthalpy")) == "gases.sourceEnergy compensated → enthalpy",
              "gas source energy is its own owner change");
        // A solver preset applied in a colony changes only its solver keys.
        var chosen = basis.With(SettingCatalog.Co2Limits, true).With(SettingCatalog.RegionActivity, "always").With(SettingCatalog.EvaporationBelowBoiling, true);
        plan = ColonyChange.Plan(chosen, chosen.WithGroup(SettingCatalog.Solver, BuiltInPresets.Find("precise")!.Values), profile8);
        Check(plan.Keys.SequenceEqual(new[] { "solver.stepsPerFrame", "solver.pressureTolerance", "solver.pressureIterationCap", "solver.heatIterationCap" }) &&
              plan.Options && !plan.Activity && !plan.Breathing && plan.Refusal == null, "a solver preset in a colony");
        Check(ColonyChange.Plan(chosen, chosen.Copy(), profile12).Empty && ColonyChange.Plan(chosen, chosen.With("performance.workerThreads", 3.0), profile12).Empty,
              "unchanged colony settings or a preference are no colony change");
        // Changed thresholds enter the colony's next breathing record; values outside the ranges are refused before native.
        var thresholds = basis.With("breathing.minimumOxygenFraction", .12).With("breathing.maximumCo2PressurePa", 1000.0);
        var decoded = ColonySettingsRecord.Decode(ColonySettingsRecord.Encode(thresholds), basis);
        Check(decoded.Number("breathing.minimumOxygenFraction") == .12 && decoded.Number("breathing.maximumCo2PressurePa") == 1000, "changed thresholds in the record");
        Throws<FormatException>(() => basis.With("solver.flowParts", 9.0), "an out-of-range colony change");
        Throws<FormatException>(() => basis.WithNative(BuiltInPresets.Find("robust")!.Values.ToNative()).With("solver.stepsPerFrame", 3.0).ToNative(), "three steps in a colony");
    }

    // The ten-cycle bridge (docs/starting-air.md): 1,000 cells of 1.67 kg air at
    // 295 K, the game's 16 g/mol oxygen and 44 g/mol carbon dioxide.
    private static void Bridge()
    {
        const double cells = 1000, kg = 1.67, t = 295, mo = .0159994, mc = .04401;
        var k = mo * .02 / mc;
        bool Near(double a, double b) => Math.Abs(a - b) <= 1e-12 * Math.Max(1, Math.Abs(b));
        // Pure generated oxygen under easy mode: carbon dioxide binds first, by
        // its 500 Pa partial pressure (the gas is at about 256 kPa).
        var n = cells * kg / mo;
        var (o, c) = StartingAirBridge.BreathableMoles(n, 0, n, cells, t, k, .15, 15000, 1, .005, 500);
        var byPressure = 500 * cells / (StartingAirBridge.GasConstant * t) / k;
        // Pure oxygen never falls below 15% of the gas; its partial pressure falls to 15 kPa.
        Check(Near(o, n - 15000 * cells / (StartingAirBridge.GasConstant * t)) && Near(c, byPressure) && byPressure < .005 * n / (k * (1 - .005) + .005) && c < o,
              "easy-mode bridge on generated oxygen");
        var disabled = StartingAirBridge.BreathableMoles(n, .5 * n, n, cells, t, k, .15, 15000, 1, .005, 500, false);
        Check(double.IsPositiveInfinity(disabled.carbonDioxide) && Near(disabled.oxygen, o), "CO2 off removes only CO2 headroom limit");
        // Hard mode on generated oxygen breathes nothing; thin air binds by pressure.
        Check(StartingAirBridge.BreathableMoles(n, 0, n, cells, t, k, .15, 15000, .35, .005, 500).oxygen == 0, "hard mode on generated oxygen");
        // The duplicant wait counts game time only (2026-11-08: a paused start
        // counted publications and skipped the reserve).
        var start = double.NaN;
        Check(!StartingAirBridge.WaitExpired(ref start, 12.5, 120) && start == 12.5, "the wait starts at the first check");
        Check(Enumerable.Range(0, 1000).All(_ => !StartingAirBridge.WaitExpired(ref start, 12.5, 120)), "a paused start expired the duplicant wait");
        Check(!StartingAirBridge.WaitExpired(ref start, 132.4, 120) && StartingAirBridge.WaitExpired(ref start, 132.5, 120), "the wait ends after 120 game seconds");
        var none = double.NaN;
        Check(!StartingAirBridge.WaitExpired(ref none, double.NaN, 120) && double.IsNaN(none), "a missing game clock expired the wait");
        var thin = cells * .3 / mo;
        (o, _) = StartingAirBridge.BreathableMoles(thin, 0, thin, cells, t, k, .15, 15000, 1, .005, 500);
        Check(Near(o, thin - 15000 * cells / (StartingAirBridge.GasConstant * t)), "partial pressure binds in thin air");
        // Carbon dioxide already past its limit leaves no headroom.
        Check(StartingAirBridge.BreathableMoles(n, .01 * n, n, cells, t, k, .15, 15000, 1, .005, 500).carbonDioxide == 0, "no headroom past the limit");
    }

    // "What plays differently" (docs/starting-air.md): at most eight one-line
    // items, every number from the colony's settings.
    private static void Primer()
    {
        var defaults = ColonyPrimer.Lines(SettingValues.Defaults(), true);
        Check(defaults.Count == 8 && defaults.All(line => line.Length <= 140 && !line.Contains('\n') && line.EndsWith(".", StringComparison.Ordinal)), "primer shape");
        Check(defaults[1] == "Duplicants need air with at least 15% oxygen and 15 kPa of oxygen (polluted oxygen counts): see the Oxygen margin overlay." && defaults.Any(line => line.StartsWith("Chlorine, sour gas and nuclear fallout make air unbreathable once together they are 5% of it.", StringComparison.Ordinal)) &&
              !defaults.Any(line => line.Contains("carbon dioxide above")), "primer numbers from the default settings");
        var custom = SettingValues.Defaults().With("breathing.minimumOxygenFraction", .12).With("breathing.minimumOxygenPressurePa", 8000.0)
            .With(SettingCatalog.ToxicLimit, .02).With(SettingCatalog.Co2Limits, true);
        var lines = ColonyPrimer.Lines(custom, false);
        Check(lines.Count == 8 && lines[1].Contains("at least 12% oxygen and 8 kPa of oxygen") &&
              lines.Any(line => line == "Chlorine and sour gas make air unbreathable once together they are 2% of it, and so does carbon dioxide above 4% or 5 kPa."),
              "primer numbers from the colony's settings");
        var noToxic = ColonyPrimer.Lines(SettingValues.Defaults().With(SettingCatalog.ToxicLimit, 1.0), true);
        Check(noToxic.Count == 7 && !noToxic.Any(line => line.Contains("sour gas")), "no toxic gas line without a limit");
        Check(ColonyPrimer.Lines(SettingValues.Defaults().With(SettingCatalog.ToxicLimit, 1.0).With(SettingCatalog.Co2Limits, true), true)
              .Any(line => line == "Air with carbon dioxide above 4% or 5 kPa is unbreathable."), "CO2 line alone");
        Check(defaults.Contains("Gases layer by molecular weight: natural gas and steam rise above oxygen; chlorine sinks below carbon dioxide.") &&
              defaults.Any(line => line.Contains("base air is about 125 kPa")), "primer follows the molecular masses");
        Check(ColonyPrimer.Text(SettingValues.Defaults(), true).StartsWith(ColonyPrimer.Title + ":\n• Gases stay mixed.", StringComparison.Ordinal), "primer text");
        var evaporating = ColonyPrimer.Lines(SettingValues.Defaults().With(SettingCatalog.EvaporationBelowBoiling, true), true);
        Check(evaporating.Contains("Evaporation below boiling is on: air near water holds about 2% steam and wet rooms form dew (Gameplay → Boiling and condensation).") &&
              !evaporating.Any(line => line.StartsWith("Boiling follows pressure", StringComparison.Ordinal)) &&
              evaporating.Count <= 8 && evaporating.All(line => line.Length <= 140 && !line.Contains('\n')), "primer says evaporation below boiling is on");
        Check(defaults.Contains("Boiling follows pressure: water in vacuum or thin air boils at room temperature.") &&
              ColonyPrimer.Lines(SettingValues.Defaults().With(SettingCatalog.EvaporationBelowBoiling, false), true).SequenceEqual(defaults) &&
              !defaults.Any(line => line.Contains("Evaporation below boiling")), "primer unchanged with evaporation below boiling off");
    }

    private static int Main()
    {
        var root = Path.Combine(Path.GetTempPath(), "ma-settings-tests-" + Guid.NewGuid().ToString("N"));
        try
        {
            Catalog(); Json(); Store(root); Record(); LiveColony(); Bridge(); Primer();
            Console.WriteLine($"PASS settings model, JSON, files, presets and colony record ({checks} checks)");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine("FAIL " + error); return 1; }
        finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
    }
}
