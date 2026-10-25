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
        o.Size == 80 && o.Version == 1 && o.PressureMaximumIterations >= 400 && o.PressureMaximumIterations <= 65536 &&
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
            Check(d.Key.StartsWith(d.Scope == SettingScope.Colony ? "solver." : "", StringComparison.Ordinal) || d.Key.StartsWith("breathing.", StringComparison.Ordinal) || d.Scope == SettingScope.Preference,
                "colony keys are solver or breathing: " + d.Key);
        }
        // Default reproduces today's profile 5 exactly (oni_colony.cpp default_options_value).
        var native = SettingValues.Defaults().ToNative();
        Check(native.PressureMaximumIterations == 400 && native.HeatMaximumIterations == 800 && native.FlowParts == 2 && native.SharpenLiquidInterfaces == 1 &&
              native.MaximumTimestepSeconds == (double).2f && native.TraceSinkThresholdKg == 1e-11 && native.LiquidStiffnessRatio == .04 &&
              native.PressureRelativeTolerance == 1e-8 && native.GasHeatMixing == 8 && native.InterfaceDetailFraction == 0 && native.PhaseRelaxationSeconds == 1,
              "Default native options differ from today's profile 5");
        // Every offered step of every solver setting, and every built-in preset, is inside the native ranges.
        foreach (var d in SettingCatalog.All.Where(d => d.Key.StartsWith("solver.", StringComparison.Ordinal)))
            foreach (var step in d.Kind == SettingKind.Toggle ? new object[] { true, false } : d.Steps.Cast<object>())
                Check(NativeAccepts(SettingValues.Defaults().With(d.Key, step).ToNative()), "native refuses " + d.Key + " = " + step);
        foreach (var p in BuiltInPresets.All) Check(NativeAccepts(p.Values.ToNative()), "native refuses preset " + p.Id);
        // Experimental marks.
        Check(BuiltInPresets.All.Select(p => p.Id).SequenceEqual(new[] { "default", "robust", "fast", "precise", "relaxed-breathing", "strict-breathing" }), "built-in preset list");
        foreach (var id in new[] { "default", "robust", "relaxed-breathing", "strict-breathing" }) Check(!BuiltInPresets.Find(id)!.Experimental, id + " marked experimental");
        foreach (var id in new[] { "fast", "precise" }) Check(BuiltInPresets.Find(id)!.Experimental, id + " not marked experimental");
        Check(SettingValues.Defaults().With("solver.sharpenLiquidInterfaces", false).Experimental(SettingScope.Colony).Any(), "sharpening off not experimental");
        Check(SettingValues.Defaults().With("solver.pressureTolerance", 1e-6).Experimental(SettingScope.Colony).Any(), "looser tolerance not experimental");
        Check(!SettingValues.Defaults().With("solver.pressureIterationCap", 6400.0).Experimental(SettingScope.Colony).Any(), "higher cap marked experimental");
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
        var values = BuiltInPresets.Find("strict-breathing")!.Values.With("display.readingPrecision", "exact").With("keys.findFluid", "F7").With("panel.expanded", false);
        store.Save(values, "strict-breathing");
        loaded = store.Load();
        Check(loaded.Values.Equals(values) && loaded.PresetId == "strict-breathing" && loaded.Notices.Count == 0, "config round trip");
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
        var mine = BuiltInPresets.Find("robust")!.Values.With("breathing.minimumOxygenFraction", .12).With("keys.toggleDetails", "F2");
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
        var current = SettingValues.Defaults().With("display.readingPrecision", "exact").With("solver.flowParts", 4.0);
        var applied = BuiltInPresets.Find("strict-breathing")!.ApplyTo(current);
        Check(applied.Choice("display.readingPrecision") == "exact" && applied.Number("solver.flowParts") == 2 && applied.Number("breathing.minimumOxygenFraction") == .18, "built-in apply");
        applied = store.FindUserPreset("thin-air")!.ApplyTo(current);
        Check(applied.Choice("display.readingPrecision") == "friendly" && applied.Choice("keys.toggleDetails") == "F2" && applied.Number("solver.pressureIterationCap") == 1600, "saved preset apply");
        Check(ModSettingsStore.Slug("Ünïcode / ☃ name") == "n-code-name" && ModSettingsStore.Slug("***") == "preset", "slug");
    }

    private static void Record()
    {
        Check(ColonySettingsRecord.Encode(SettingValues.Defaults()) == null, "Default colonies write no record");
        Check(ColonySettingsRecord.Encode(BuiltInPresets.Find("precise")!.Values) == null, "solver-only presets write no record");
        var strict = BuiltInPresets.Find("strict-breathing")!.Values;
        var bytes = ColonySettingsRecord.Encode(strict)!;
        Check(bytes.Length == 48 && BitConverter.ToUInt32(bytes, 0) == 1 && BitConverter.ToUInt32(bytes, 4) == 5, "record layout");
        var decoded = ColonySettingsRecord.Decode(bytes, SettingValues.Defaults());
        Check(decoded.ScopeEquals(SettingScope.Colony, strict), "record round trip");
        var wrongVersion = (byte[])bytes.Clone(); wrongVersion[0] = 2;
        Throws<NotSupportedException>(() => ColonySettingsRecord.Decode(wrongVersion, SettingValues.Defaults()), "future record version");
        Throws<InvalidDataException>(() => ColonySettingsRecord.Decode(bytes.AsSpan(0, 20), SettingValues.Defaults()), "truncated record");
        var outside = (byte[])bytes.Clone(); BitConverter.GetBytes(2.0).CopyTo(outside, 8);
        Throws<InvalidDataException>(() => ColonySettingsRecord.Decode(outside, SettingValues.Defaults()), "out-of-range record");
    }

    private static int Main()
    {
        var root = Path.Combine(Path.GetTempPath(), "ma-settings-tests-" + Guid.NewGuid().ToString("N"));
        try
        {
            Catalog(); Json(); Store(root); Record();
            Console.WriteLine($"PASS settings model, JSON, files, presets and colony record ({checks} checks)");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine("FAIL " + error); return 1; }
        finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
    }
}
