using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // Player settings (docs/settings.md). Engine-free: the standalone tests
    // compile this file with ModSettingsJson.cs and ModSettingsStore.cs.
    //
    // Colony settings are impactful: a new colony records them (the solver's
    // in its own physical image, breathing in MACOLP03 section 3) and a
    // loaded colony keeps its recorded values. Preferences are execution,
    // controls and display; they apply at once. The solver is always game
    // profile 5 with mixed pipes; only profile 5's own saved tunables vary.
    internal enum SettingScope { Colony, Preference }
    internal enum SettingKind { Number, Toggle, Choice }

    internal sealed class SettingDefinition
    {
        internal string Key { get; }
        internal SettingScope Scope { get; }
        internal SettingKind Kind { get; }
        internal string Group { get; }
        internal string Label { get; }
        internal string Description { get; }
        internal object Default { get; }
        internal double Minimum { get; }
        internal double Maximum { get; }
        internal bool Integer { get; }
        // Number steps the screen offers (any value within the range is valid in a file).
        internal double[] Steps { get; }
        // Choice ids (strings) and their labels.
        internal string[] Choices { get; }
        internal string[] ChoiceLabels { get; }
        internal string Unit { get; }
        // Values the profile was measured with; anything else is "experimental".
        private readonly Func<object, bool>? tested;

        private SettingDefinition(string key, SettingScope scope, SettingKind kind, string group, string label, string description, object value,
            double minimum, double maximum, bool integer, double[]? steps, string[]? choices, string[]? labels, string unit, Func<object, bool>? tested)
        {
            Key = key; Scope = scope; Kind = kind; Group = group; Label = label; Description = description; Default = value;
            Minimum = minimum; Maximum = maximum; Integer = integer; Steps = steps ?? Array.Empty<double>();
            Choices = choices ?? Array.Empty<string>(); ChoiceLabels = labels ?? Choices; Unit = unit; this.tested = tested;
        }
        internal static SettingDefinition Number(string key, SettingScope scope, string group, string label, string description, double value,
            double minimum, double maximum, double[] steps, string unit = "", bool integer = false, Func<double, bool>? tested = null) =>
            new SettingDefinition(key, scope, SettingKind.Number, group, label, description, value, minimum, maximum, integer, steps, null, null, unit,
                tested == null ? null : (Func<object, bool>)(v => tested((double)v)));
        internal static SettingDefinition Toggle(string key, SettingScope scope, string group, string label, string description, bool value,
            Func<bool, bool>? tested = null) =>
            new SettingDefinition(key, scope, SettingKind.Toggle, group, label, description, value, 0, 1, true, null, null, null, "",
                tested == null ? null : (Func<object, bool>)(v => tested((bool)v)));
        internal static SettingDefinition Choice(string key, SettingScope scope, string group, string label, string description, string value,
            string[] choices, string[] labels) =>
            new SettingDefinition(key, scope, SettingKind.Choice, group, label, description, value, 0, 0, false, null, choices, labels, "", null);

        internal bool Advanced => Group == SettingCatalog.Solver || Group == SettingCatalog.Performance ||
            Group == SettingCatalog.Breathing && Key != SettingCatalog.Co2Limits ||
            Key == "display.traceMoleFraction" || Key == "panel.showBoundary" || Key == "keys.toggleBoundary";
        internal bool IsExperimental(object value) => tested != null && !tested(value);

        // A file value converted to this setting's type, or an error.
        internal object Accept(object? value)
        {
            switch (Kind)
            {
                case SettingKind.Toggle:
                    if (value is bool b) return b;
                    throw new FormatException(Label + " must be true or false.");
                case SettingKind.Choice:
                    if (value is string s && Array.IndexOf(Choices, s) >= 0) return s;
                    throw new FormatException(Label + " must be one of " + string.Join(", ", Choices) + ".");
                default:
                    if (!(value is double d) || double.IsNaN(d) || double.IsInfinity(d)) throw new FormatException(Label + " must be a number.");
                    if (Integer && d != Math.Floor(d)) throw new FormatException(Label + " must be a whole number.");
                    if (d < Minimum || d > Maximum)
                        throw new FormatException(Label + " " + ModSettingsText.Number(d) + " is outside " + ModSettingsText.Number(Minimum) + "–" + ModSettingsText.Number(Maximum) + ".");
                    return d;
            }
        }
    }

    internal static class ModSettingsText
    {
        internal static string Number(double value) => value.ToString(value != 0 && (Math.Abs(value) < 1e-3 || Math.Abs(value) >= 1e6) ? "0.###e0" : "0.####", CultureInfo.InvariantCulture);
    }

    // Every supported setting (docs/settings.md lists the same table).
    internal static class SettingCatalog
    {
        internal const string Solver = "Solver (profile 5)", Breathing = "Breathing", Performance = "Performance", Display = "Display", Panel = "Status panel", Keys = "Keyboard shortcuts";
        internal const string Co2Limits = "breathing.co2Limits";
        private static double[] Decades(int from, int to) { var r = new List<double>(); for (var e = from; e <= to; ++e) r.Add(double.Parse("1e" + e.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture)); return r.ToArray(); }
        internal static readonly string[] KeyChoices = { "None", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };

        internal static readonly IReadOnlyList<SettingDefinition> All = new[]
        {
            // Profile-5 solver tunables, saved in the colony's physical image.
            SettingDefinition.Number("solver.stepsPerFrame", SettingScope.Colony, Solver, "Solver steps per game tick",
                "How many solver steps cover each 0.2 s game tick. 2 or 4 resolve fast flow more finely and cost about 2 or 4 times the solver time.",
                1, 1, 4, new double[] { 1, 2, 4 }, "", true, v => v == 1),
            SettingDefinition.Number("solver.pressureTolerance", SettingScope.Colony, Solver, "Pressure solve tolerance",
                "Relative accuracy of each pressure solve. Looser is faster but lets pressures settle less exactly; tighter needs more iterations.",
                1e-8, 1e-12, 1e-4, Decades(-12, -4), "", false, v => v == 1e-8),
            SettingDefinition.Number("solver.pressureIterationCap", SettingScope.Colony, Solver, "Pressure iteration cap",
                "Most iterations one pressure solve may take. A converged solve stops early, so a higher cap changes no result; reaching the cap stops the simulation. Lower caps are never faster and are not offered.",
                400, 400, 65536, new double[] { 400, 800, 1600, 3200, 6400, 12800 }, "", true),
            SettingDefinition.Number("solver.heatIterationCap", SettingScope.Colony, Solver, "Heat iteration cap",
                "Most iterations one heat solve may take; as for the pressure cap, higher only adds room.",
                800, 800, 65536, new double[] { 800, 1600, 3200, 6400, 12800, 25600 }, "", true),
            SettingDefinition.Number("solver.flowParts", SettingScope.Colony, Solver, "Flow parts per step",
                "Transport sub-steps inside each solver step. More follow fast flow more closely and cost more.",
                2, 1, 8, new double[] { 1, 2, 3, 4, 6, 8 }, "", true, v => v == 2),
            SettingDefinition.Number("solver.gasHeatMixing", SettingScope.Colony, Solver, "Gas heat mixing",
                "How strongly moving gas mixes its temperature (energy is conserved). 8 is the measured choice; 0 mixes only by conduction.",
                8, 0, 32, new double[] { 0, 1, 2, 4, 8, 16, 32 }, "", false, v => v == 8),
            SettingDefinition.Number("solver.liquidStiffness", SettingScope.Colony, Solver, "Liquid stiffness",
                "Liquid pressure rise per volume squeezed, as a fraction of the catalogue bulk modulus. Higher makes sealed liquids stiffer.",
                .04, .01, 1, new double[] { .01, .02, .04, .1, .2, .5, 1 }, "× bulk modulus", false, v => v == .04),
            SettingDefinition.Toggle("solver.sharpenLiquidInterfaces", SettingScope.Colony, Solver, "Sharpen liquid surfaces",
                "Keeps liquid surfaces and liquid layers crisp instead of smearing over neighbouring cells.", true, v => v),
            SettingDefinition.Number("solver.interfaceDetail", SettingScope.Colony, Solver, "Thinnest sharpened film",
                "Liquid films thinner than this fraction of a cell are left unsharpened.",
                0, 0, .5, new double[] { 0, .01, .05, .1, .25, .5 }, "of a cell", false, v => v == 0),
            SettingDefinition.Number("solver.phaseRelaxationSeconds", SettingScope.Colony, Solver, "Boiling/condensation time",
                "Time constant with which a liquid and its vapour approach saturation: shorter boils and condenses faster.",
                1, .1, 10, new double[] { .1, .25, .5, 1, 2, 5, 10 }, "s", false, v => v == 1),
            SettingDefinition.Number("solver.traceThresholdKg", SettingScope.Colony, Solver, "Trace removal threshold",
                "A constituent below this mass in a cell leaves through the booked trace sink. 1e-11 kg is the smallest tested value; the ceiling is 1 mg.",
                1e-11, 1e-11, 1e-6, Decades(-11, -6), "kg", false, v => v == 1e-11),
            // Gameplay: CO2 toxicity is optional; oxygen requirements always apply.
            SettingDefinition.Toggle(Co2Limits, SettingScope.Colony, Breathing, "CO₂ breathing limits",
                "Off (default): carbon dioxide does not block breathing while enough oxygen remains. On: apply the CO₂ thresholds in Advanced. CO₂ still mixes, moves and carries heat.", false),
            SettingDefinition.Number("breathing.minimumOxygenFraction", SettingScope.Colony, Breathing, "Minimum oxygen share",
                "A duplicant can breathe a cell whose gas is at least this share oxygen by moles (polluted oxygen counts).",
                .15, .05, .25, new double[] { .05, .08, .1, .12, .15, .18, .2, .25 }),
            SettingDefinition.Number("breathing.minimumOxygenPressurePa", SettingScope.Colony, Breathing, "Minimum oxygen partial pressure",
                "…and at least this oxygen partial pressure.",
                15000, 1000, 50000, new double[] { 1000, 5000, 8000, 10000, 12000, 15000, 18000, 21000, 30000, 50000 }, "Pa"),
            SettingDefinition.Number("breathing.maximumCo2Fraction", SettingScope.Colony, Breathing, "Maximum carbon dioxide share",
                "Applied only when Gameplay → CO₂ breathing limits is On: maximum share by moles (0: any carbon dioxide fails).",
                .005, 0, .1, new double[] { 0, .001, .003, .005, .01, .02, .05, .1 }),
            SettingDefinition.Number("breathing.maximumCo2PressurePa", SettingScope.Colony, Breathing, "Maximum carbon dioxide partial pressure",
                "Applied only when Gameplay → CO₂ breathing limits is On: maximum carbon dioxide partial pressure.",
                500, 0, 10000, new double[] { 0, 100, 300, 500, 1000, 2000, 5000, 10000 }, "Pa"),
            SettingDefinition.Number("breathing.maximumOxygenFraction", SettingScope.Colony, Breathing, "Maximum oxygen share",
                "Cells above this oxygen share cannot be breathed (100%: no upper limit). Generated oxygen is pure; lowering this can make starting air unbreathable.",
                1, .3, 1, new double[] { .3, .35, .5, .75, 1 }),
            // Preferences: apply at once, everywhere.
            SettingDefinition.Number("performance.workerThreads", SettingScope.Preference, Performance, "Solver workers",
                "Physical cores (default) uses one worker per physical CPU core. Choose a number to use fewer workers or include SMT threads. More workers can be slower. Changes apply to the current colony and are not saved in the colony.",
                0, 0, 256, Enumerable.Range(0, 257).Select(n => (double)n).ToArray(), "", true),
            SettingDefinition.Choice("display.readingPrecision", SettingScope.Preference, Display, "Reading precision",
                "How the hover card and status panel print readings. Exact prints every digit and both units; the values are the same.",
                "friendly", new[] { "friendly", "exact" }, new[] { "4 significant figures", "Exact" }),
            SettingDefinition.Number("display.patternContrast", SettingScope.Preference, Display, "Mixture pattern contrast",
                "Contrast of the pattern that shows mixed gases on the map (0: off).",
                .04, 0, .15, new double[] { 0, .02, .04, .08, .12 }),
            SettingDefinition.Number("display.traceMoleFraction", SettingScope.Preference, Display, "Trace gas threshold",
                "Gases below this share are listed as traces in readings.",
                .01, 0, .1, new double[] { 0, .001, .005, .01, .02, .05, .1 }),
            SettingDefinition.Toggle("panel.expanded", SettingScope.Preference, Panel, "Show panel details",
                "Start with the status panel's details open (the details shortcut toggles them).", true),
            SettingDefinition.Toggle("panel.showBoundary", SettingScope.Preference, Panel, "Outline development regions",
                "Outline a bounded development region on the map (the boundary shortcut toggles it).", true),
            SettingDefinition.Choice("keys.cycleOverlay", SettingScope.Preference, Keys, "Cycle mixture overlays", "Shift or Ctrl plus this key.", "F5", KeyChoices, KeyChoices),
            SettingDefinition.Choice("keys.toggleDetails", SettingScope.Preference, Keys, "Status panel details", "Shift or Ctrl plus this key.", "F8", KeyChoices, KeyChoices),
            SettingDefinition.Choice("keys.findFluid", SettingScope.Preference, Keys, "Find simulated fluid", "Shift or Ctrl plus this key.", "F9", KeyChoices, KeyChoices),
            SettingDefinition.Choice("keys.toggleBoundary", SettingScope.Preference, Keys, "Region outline", "Shift or Ctrl plus this key.", "F10", KeyChoices, KeyChoices),
            SettingDefinition.Choice("keys.openSettings", SettingScope.Preference, Keys, "Open settings", "Shift or Ctrl plus this key.", "None", KeyChoices, KeyChoices),
        };
        private static readonly Dictionary<string, SettingDefinition> byKey = All.ToDictionary(d => d.Key, StringComparer.Ordinal);
        internal static SettingDefinition Get(string key) => byKey.TryGetValue(key, out var d) ? d : throw new KeyNotFoundException("Unknown setting " + key + ".");
        internal static bool TryGet(string key, out SettingDefinition definition) => byKey.TryGetValue(key, out definition!);
    }

    // One complete set of values. Missing keys read as their default.
    internal sealed class SettingValues : IEquatable<SettingValues>
    {
        private readonly SortedDictionary<string, object> values = new SortedDictionary<string, object>(StringComparer.Ordinal);
        internal static SettingValues Defaults()
        {
            var result = new SettingValues();
            foreach (var d in SettingCatalog.All) result.values[d.Key] = d.Default;
            return result;
        }
        internal SettingValues Copy() { var c = new SettingValues(); foreach (var p in values) c.values[p.Key] = p.Value; return c; }
        internal object this[string key] => values[SettingCatalog.Get(key).Key];
        internal double Number(string key) => (double)this[key];
        internal bool Toggle(string key) => (bool)this[key];
        internal string Choice(string key) => (string)this[key];
        internal SettingValues With(string key, object? value)
        {
            var c = Copy(); c.values[key] = SettingCatalog.Get(key).Accept(value); return c;
        }
        // This set with one scope replaced from another set.
        internal SettingValues WithScope(SettingScope scope, SettingValues from)
        {
            var c = Copy();
            foreach (var d in SettingCatalog.All) if (d.Scope == scope) c.values[d.Key] = from.values[d.Key];
            return c;
        }
        internal SettingValues WithGroup(string group, SettingValues from)
        {
            var c = Copy();
            foreach (var d in SettingCatalog.All) if (d.Group == group) c.values[d.Key] = from.values[d.Key];
            return c;
        }
        internal bool ScopeEquals(SettingScope scope, SettingValues other) =>
            SettingCatalog.All.Where(d => d.Scope == scope).All(d => Same(values[d.Key], other.values[d.Key]));
        internal bool IsDefault(SettingScope scope) => ScopeEquals(scope, Defaults());
        internal IEnumerable<SettingDefinition> Experimental(SettingScope scope) =>
            SettingCatalog.All.Where(d => d.Scope == scope && d.IsExperimental(values[d.Key]));
        private static bool Same(object a, object b) => a is double x && b is double y ? BitConverter.DoubleToInt64Bits(x) == BitConverter.DoubleToInt64Bits(y) : a.Equals(b);
        public bool Equals(SettingValues? other) => other != null && ScopeEquals(SettingScope.Colony, other) && ScopeEquals(SettingScope.Preference, other);
        public override bool Equals(object? other) => Equals(other as SettingValues);
        public override int GetHashCode() => values.Count;

        // The members of one scope for a file (only that scope's keys).
        internal IEnumerable<KeyValuePair<string, object?>> Members(SettingScope scope) =>
            SettingCatalog.All.Where(d => d.Scope == scope).Select(d => new KeyValuePair<string, object?>(d.Key, values[d.Key]));

        // Reads one scope's object. Unknown keys are reported (and ignored);
        // a known key with an invalid value rejects the whole object.
        internal static SettingValues Read(SettingValues basis, SettingScope scope, object? section, List<string> warnings)
        {
            if (section == null) return basis.Copy();
            if (!(section is Dictionary<string, object?> members)) throw new FormatException("\"" + scope + "\" must be an object.");
            var result = basis.Copy();
            foreach (var member in members)
            {
                if (!SettingCatalog.TryGet(member.Key, out var definition) || definition.Scope != scope)
                { warnings.Add("Ignored unknown setting \"" + member.Key + "\"."); continue; }
                result.values[member.Key] = definition.Accept(member.Value);
            }
            return result;
        }

        // Profile-5 options for the native owner (oni_colony.h MAOniColonyOptions).
        internal NativeColonyOptions ToNative() => new NativeColonyOptions
        {
            Size = NativeColonyOptions.Bytes, Version = 1,
            PressureMaximumIterations = (uint)Number("solver.pressureIterationCap"), HeatMaximumIterations = (uint)Number("solver.heatIterationCap"),
            FlowParts = (uint)Number("solver.flowParts"), SharpenLiquidInterfaces = Toggle("solver.sharpenLiquidInterfaces") ? 1U : 0U,
            MaximumTimestepSeconds = Number("solver.stepsPerFrame") switch { 1 => .2f, 2 => .1f, 4 => .05f,
                _ => throw new FormatException("Solver steps per game tick must be 1, 2 or 4.") },
            TraceSinkThresholdKg = Number("solver.traceThresholdKg"), LiquidStiffnessRatio = Number("solver.liquidStiffness"),
            PressureRelativeTolerance = Number("solver.pressureTolerance"), GasHeatMixing = Number("solver.gasHeatMixing"),
            InterfaceDetailFraction = Number("solver.interfaceDetail"), PhaseRelaxationSeconds = Number("solver.phaseRelaxationSeconds")
        };
        // The solver settings a running colony reports, with this set's other values.
        internal SettingValues WithNative(NativeColonyOptions o)
        {
            if (o.Size != NativeColonyOptions.Bytes || o.Version != 1) throw new FormatException("Unknown native colony options.");
            var steps = o.MaximumTimestepSeconds == (double).2f ? 1 : o.MaximumTimestepSeconds == (double).1f ? 2 : o.MaximumTimestepSeconds == (double).05f ? 4 : 0;
            var c = Copy();
            // A loaded colony reports what it saved, even outside today's ranges.
            c.values["solver.stepsPerFrame"] = (double)steps; c.values["solver.pressureIterationCap"] = (double)o.PressureMaximumIterations;
            c.values["solver.heatIterationCap"] = (double)o.HeatMaximumIterations; c.values["solver.flowParts"] = (double)o.FlowParts;
            c.values["solver.sharpenLiquidInterfaces"] = o.SharpenLiquidInterfaces != 0; c.values["solver.traceThresholdKg"] = o.TraceSinkThresholdKg;
            c.values["solver.liquidStiffness"] = o.LiquidStiffnessRatio; c.values["solver.pressureTolerance"] = o.PressureRelativeTolerance;
            c.values["solver.gasHeatMixing"] = o.GasHeatMixing; c.values["solver.interfaceDetail"] = o.InterfaceDetailFraction;
            c.values["solver.phaseRelaxationSeconds"] = o.PhaseRelaxationSeconds;
            return c;
        }
        internal static string Describe(object value) => value switch
        {
            bool b => b ? "on" : "off", double d => ModSettingsText.Number(d), _ => value.ToString() ?? ""
        };
    }

    // MAOniColonyOptions (native/include/mixed_atmospheres/oni_colony.h), 80 bytes.
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeColonyOptions
    {
        internal const uint Bytes = 80;
        public uint Size, Version, PressureMaximumIterations, HeatMaximumIterations, FlowParts, SharpenLiquidInterfaces;
        public double MaximumTimestepSeconds, TraceSinkThresholdKg, LiquidStiffnessRatio, PressureRelativeTolerance, GasHeatMixing,
            InterfaceDetailFraction, PhaseRelaxationSeconds;
        internal static void CheckLayout()
        {
            if (Marshal.SizeOf<NativeColonyOptions>() != Bytes || Marshal.OffsetOf<NativeColonyOptions>(nameof(MaximumTimestepSeconds)).ToInt32() != 24 ||
                Marshal.OffsetOf<NativeColonyOptions>(nameof(PhaseRelaxationSeconds)).ToInt32() != 72)
                throw new InvalidOperationException("Colony options layout differs from the native library.");
        }
    }

    internal sealed class SettingsPreset
    {
        internal string Id { get; }
        internal string Name { get; }
        internal string Description { get; }
        internal bool BuiltIn { get; }
        internal SettingValues Values { get; }
        // A built-in preset sets colony settings only; a saved preset also
        // carries the preferences it was saved with.
        internal bool HasPreferences { get; }
        internal SettingsPreset(string id, string name, string description, bool builtIn, SettingValues values, bool hasPreferences)
        { Id = id; Name = name; Description = description; BuiltIn = builtIn; Values = values; HasPreferences = hasPreferences; }
        internal bool Experimental => Values.Experimental(SettingScope.Colony).Any();
        internal SettingValues ApplyTo(SettingValues current)
        {
            var result = current.WithScope(SettingScope.Colony, Values);
            return HasPreferences ? result.WithScope(SettingScope.Preference, Values) : result;
        }
    }

    internal static class BuiltInPresets
    {
        internal const string DefaultId = "default";
        private static SettingsPreset Make(string id, string name, string description, params (string key, object value)[] changes)
        {
            var values = SettingValues.Defaults();
            foreach (var (key, value) in changes) values = values.With(key, value);
            return new SettingsPreset(id, name, description, true, values, false);
        }
        internal static readonly IReadOnlyList<SettingsPreset> All = new[]
        {
            Make(DefaultId, "Default (recommended)", "Exactly what a new colony gets without any settings: the measured solver, normal oxygen requirements and CO₂ breathing limits off."),
            Make("robust", "Robust", "Higher pressure and heat iteration caps. Identical results wherever Default completes; a difficult solve that would stop Default gets more room.",
                ("solver.pressureIterationCap", 1600.0), ("solver.heatIterationCap", 3200.0)),
            Make("fast", "Fast", "A looser pressure tolerance: fewer pressure iterations, pressures settle less exactly.",
                ("solver.pressureTolerance", 1e-6)),
            Make("precise", "Precise", "Two solver steps per game tick and a tighter pressure tolerance, with the Robust caps. About twice the solver time.",
                ("solver.stepsPerFrame", 2.0), ("solver.pressureTolerance", 1e-10), ("solver.pressureIterationCap", 1600.0), ("solver.heatIterationCap", 3200.0)),
            Make("relaxed-breathing", "Relaxed breathing", "At least 10% and 10 kPa oxygen. Enables CO₂ breathing limits: up to 1% and 1 kPa carbon dioxide.",
                ("breathing.minimumOxygenFraction", .1), ("breathing.minimumOxygenPressurePa", 10000.0),
                (SettingCatalog.Co2Limits, true), ("breathing.maximumCo2Fraction", .01), ("breathing.maximumCo2PressurePa", 1000.0)),
            Make("strict-breathing", "Strict breathing", "At least 18% and 18 kPa oxygen. Enables CO₂ breathing limits: below 0.3% and 300 Pa carbon dioxide.",
                ("breathing.minimumOxygenFraction", .18), ("breathing.minimumOxygenPressurePa", 18000.0),
                (SettingCatalog.Co2Limits, true), ("breathing.maximumCo2Fraction", .003), ("breathing.maximumCo2PressurePa", 300.0)),
        };
        internal static SettingsPreset Default => All[0];
        internal static SettingsPreset? Find(string id) => All.FirstOrDefault(p => string.Equals(p.Id, id, StringComparison.OrdinalIgnoreCase));
        internal static bool IsBuiltInId(string id) => Find(id) != null;
    }

    // MACOLP03 section 3, version 3 (docs/settings.md). Always explicit for
    // new colonies: older builds must refuse the new CO2 behavior rather than
    // silently applying their former defaults. Older records are refused during R&D.
    internal static class ColonySettingsRecord
    {
        internal const uint Tag = 3, Version = 3;
        internal static readonly string[] Keys =
        {
            "breathing.minimumOxygenFraction", "breathing.minimumOxygenPressurePa", "breathing.maximumCo2Fraction",
            "breathing.maximumCo2PressurePa", "breathing.maximumOxygenFraction"
        };
        internal static byte[] Encode(SettingValues values)
        {
            var bytes = new byte[8 + 8 * (Keys.Length + 1)];
            BinaryPrimitives.WriteUInt32LittleEndian(bytes, Version);
            BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4), (uint)(Keys.Length + 1));
            for (var k = 0; k < Keys.Length; ++k)
                BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(8 + 8 * k), BitConverter.DoubleToInt64Bits(values.Number(Keys[k])));
            BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(8 + 8 * Keys.Length), BitConverter.DoubleToInt64Bits(values.Toggle(SettingCatalog.Co2Limits) ? 1.0 : 0.0));
            return bytes;
        }
        internal static SettingValues Decode(ReadOnlySpan<byte> bytes, SettingValues basis)
        {
            if (bytes.Length < 8) throw new InvalidDataException("Truncated colony settings record.");
            var version = BinaryPrimitives.ReadUInt32LittleEndian(bytes);
            if (version != Version)
                throw new NotSupportedException("Colony settings record version " + version + " is not supported by this R&D build. Start a new colony.");
            var count = Keys.Length + 1;
            if (BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(4)) != count || bytes.Length != 8 + 8 * count)
                throw new InvalidDataException("Invalid colony settings record.");
            var result = basis;
            for (var k = 0; k < Keys.Length; ++k)
            {
                var value = BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(bytes.Slice(8 + 8 * k)));
                try { result = result.With(Keys[k], value); }
                catch (FormatException error) { throw new InvalidDataException("Invalid colony settings record: " + error.Message); }
            }
            var flag = BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(bytes.Slice(8 + 8 * Keys.Length)));
            if (flag != 1 && flag != 0) throw new InvalidDataException("Invalid colony settings switch.");
            result = result.With(SettingCatalog.Co2Limits, flag == 1);
            return result;
        }
    }
}
