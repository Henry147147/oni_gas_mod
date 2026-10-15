using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Core
{
    public readonly struct MixtureSrgbColor
    {
        public double R { get; }
        public double G { get; }
        public double B { get; }
        public MixtureSrgbColor(double r, double g, double b)
        {
            Check(r); Check(g); Check(b); R = r; G = g; B = b;
        }
        private static void Check(double value)
        { FluidChecks.Nonnegative(value, "sRGB channel"); if (value > 1) throw new ArgumentOutOfRangeException(nameof(value)); }
        internal static double ToLinear(double value) => value <= 0.04045 ? value / 12.92 : Math.Pow((value + 0.055) / 1.055, 2.4);
        internal static double FromLinear(double value)
        {
            value = Math.Max(0, Math.Min(1, value));
            return value <= 0.0031308 ? 12.92 * value : 1.055 * Math.Pow(value, 1 / 2.4) - 0.055;
        }
        internal static MixtureSrgbColor Linear(double r, double g, double b) => new MixtureSrgbColor(FromLinear(r), FromLinear(g), FromLinear(b));
    }

    public sealed class MixtureSpeciesStyle
    {
        public string MaterialId { get; }
        public string DisplayName { get; }
        public MixtureSrgbColor Color { get; }
        public MixtureSpeciesStyle(string materialId, string displayName, MixtureSrgbColor color)
        {
            FluidChecks.Identity(materialId, nameof(materialId)); FluidChecks.Identity(displayName, nameof(displayName));
            MaterialId = materialId; DisplayName = displayName; Color = color;
        }
    }

    public sealed class MixtureVisualOptions
    {
        public bool PatternEnabled { get; set; } = true;
        public double PatternContrast { get; set; } = 0.04;
        public double MeaningfulGasMoleFraction { get; set; } = 0.01;
        internal MixtureVisualOptions Copy()
        {
            FluidChecks.Nonnegative(PatternContrast, nameof(PatternContrast));
            FluidChecks.Nonnegative(MeaningfulGasMoleFraction, nameof(MeaningfulGasMoleFraction));
            if (PatternContrast > 0.25 || MeaningfulGasMoleFraction > 1) throw new ArgumentOutOfRangeException(nameof(PatternContrast));
            return new MixtureVisualOptions { PatternEnabled = PatternEnabled, PatternContrast = PatternContrast, MeaningfulGasMoleFraction = MeaningfulGasMoleFraction };
        }
    }

    public sealed class MixtureConstituentPresentation
    {
        private readonly bool otherGasConstituentsPresent;
        public string MaterialId { get; }
        public string DisplayName { get; }
        public NativeMatterPhase Phase { get; }
        public double MassKg { get; }
        public double Moles { get; }
        /// <summary>Fraction of gas-phase moles. Condensed constituents have no gas fraction or partial pressure.</summary>
        public double? GasMoleFraction { get; }
        public double? PartialPressurePa { get; }
        public double? ContaminantMoleFractionLimit { get; }
        public bool IsTrace { get; }
        public bool IsContaminant { get; }
        public string MolePercentText => GasMoleFraction.HasValue ?
            GasMoleFraction.Value == 1 && otherGasConstituentsPresent ? "<100%" : MixturePresentationText.Percent(GasMoleFraction.Value, true) : "not a gas";
        public string MassTextKg => MixturePresentationText.PositiveNumber(MassKg, true) + " kg";
        public string MolesText => MixturePresentationText.PositiveNumber(Moles, MassKg > 0) + " mol";
        public string PartialPressureText => PartialPressurePa.HasValue ? MixturePresentationText.PositiveNumber(PartialPressurePa.Value, MassKg > 0) + " Pa" : "not a gas";
        internal MixtureConstituentPresentation(string id, string name, NativeMatterPhase phase, double mass,
            double moles, double? fraction, double? partialPressure, double? limit, bool trace, bool contaminant, bool otherGases)
        {
            MaterialId = id; DisplayName = name; Phase = phase; MassKg = mass; Moles = moles;
            GasMoleFraction = fraction; PartialPressurePa = partialPressure; ContaminantMoleFractionLimit = limit;
            IsTrace = trace; IsContaminant = contaminant;
            otherGasConstituentsPresent = otherGases;
        }
    }

    internal static class MixturePresentationText
    {
        internal static string PositiveNumber(double number, bool knownPresent)
            => number == 0 && knownPresent ? "below numeric range" : number.ToString("G8", CultureInfo.InvariantCulture);
        internal static string Percent(double fraction, bool knownPresent = false)
        {
            if (fraction == 0 && knownPresent) return "below numeric range";
            var text = (fraction * 100).ToString("G6", CultureInfo.InvariantCulture);
            if (fraction < 1 && text == "100") return "<100%";
            return text + "%";
        }
    }

    /// <summary>Immutable read-only projection. Composition and warnings are independent of rendering colors.</summary>
    internal sealed class MixturePresentationData
    {
        private readonly MixturePresentationModel model;
        internal FluidInventory Inventory { get; }
        internal MixtureConstituentValue[] Values { get; }
        internal int GasConstituentCount { get; }
        private readonly bool patternEnabled;
        private readonly double patternContrast;
        private string? displayName;
        private IReadOnlyList<MixtureConstituentPresentation>? constituents, gasConstituents;
        private IReadOnlyList<string>? warnings;
        public string MaterialModelVersion => model.MaterialModelVersion;
        public string MaterialModelFingerprint => model.MaterialModelFingerprint;
        public string DisplayName
        {
            get
            {
                var existing = Volatile.Read(ref displayName); if (existing != null) return existing;
                var created = model.Name(Constituents, Failures); return Interlocked.CompareExchange(ref displayName, created, null) ?? created;
            }
        }
        public IReadOnlyList<MixtureConstituentPresentation> Constituents
        {
            get
            {
                var existing = Volatile.Read(ref constituents); if (existing != null) return existing;
                var created = model.BuildRows(this); return Interlocked.CompareExchange(ref constituents, created, null) ?? created;
            }
        }
        public IReadOnlyList<MixtureConstituentPresentation> GasConstituents
        {
            get
            {
                var existing = Volatile.Read(ref gasConstituents); if (existing != null) return existing;
                var all = Constituents;
                var created = all.Count == GasConstituentCount ? all : Array.AsReadOnly(all.Where(row => row.Phase == NativeMatterPhase.Gas).ToArray());
                return Interlocked.CompareExchange(ref gasConstituents, created, null) ?? created;
            }
        }
        public double TotalMassKg => Inventory.TotalMassKg;
        public double GasMassKg { get; }
        public double GasMoles { get; }
        public double TemperatureKelvin { get; }
        /// <summary>Total ideal-gas headspace pressure; this projection does not assign liquid mechanical pressure.</summary>
        public double TotalPressurePa { get; }
        public double GasPressurePa => TotalPressurePa;
        public double GasVolumeCubicMetres { get; }
        public double GasDensityKgPerCubicMetre { get; }
        public double LiquidVolumeFraction { get; }
        public double OxygenMoleFraction { get; }
        public double OxygenPartialPressurePa { get; }
        public double Co2MoleFraction { get; }
        public double Co2PartialPressurePa { get; }
        public BreathingFailure Failures { get; }
        public bool IsBreathable => Failures == BreathingFailure.None;
        public IReadOnlyList<string> Warnings
        {
            get
            {
                if (Failures == BreathingFailure.None) return Array.Empty<string>();
                var existing = Volatile.Read(ref warnings); if (existing != null) return existing;
                IReadOnlyList<string> created = new ReadOnlyCollection<string>(model.Warnings(Failures, OxygenMoleFraction, Co2MoleFraction, TotalPressurePa, Constituents));
                return Interlocked.CompareExchange(ref warnings, created, null) ?? created;
            }
        }
        public MixtureSrgbColor BaseColor { get; }
        public bool PatternEnabled => patternEnabled;
        public double PatternContrast => patternContrast;

        internal MixturePresentationData(MixturePresentationModel model, FluidInventory inventory,
            double gasMass, double gasMoles, FluidProperties properties,
            double gasVolume, double oxygen, double co2, BreathingFailure failures,
            MixtureConstituentValue[] values, int gasConstituentCount, MixtureVisualOptions options)
        {
            this.model = model; Inventory = inventory;
            GasMassKg = gasMass; GasMoles = gasMoles; TemperatureKelvin = properties.Temperature;
            TotalPressurePa = properties.GasPressure; LiquidVolumeFraction = properties.LiquidFraction; GasVolumeCubicMetres = gasVolume;
            GasDensityKgPerCubicMetre = gasMass == 0 ? 0 : gasMass / gasVolume;
            FluidChecks.Nonnegative(GasDensityKgPerCubicMetre, nameof(GasDensityKgPerCubicMetre));
            OxygenMoleFraction = oxygen; OxygenPartialPressurePa = oxygen * TotalPressurePa;
            Co2MoleFraction = co2; Co2PartialPressurePa = co2 * TotalPressurePa;
            Failures = failures;
            Values = values; GasConstituentCount = gasConstituentCount;
            patternEnabled = options.PatternEnabled; patternContrast = options.PatternContrast;
            BaseColor = Blend(0, 0, false);
        }

        /// <summary>Smooth species-weighted 4-by-4-cell tile, anchored in world coordinates. No dominant-species switch.</summary>
        public MixtureSrgbColor ColorAt(double worldX, double worldY)
        {
            FluidChecks.Finite(worldX, nameof(worldX)); FluidChecks.Finite(worldY, nameof(worldY));
            return patternEnabled && patternContrast != 0 ? Blend(worldX % 4, worldY % 4, true) : BaseColor;
        }

        private MixtureSrgbColor Blend(double x, double y, bool textured)
        {
            var r = 0.0; var g = 0.0; var b = 0.0;
            foreach (var species in Values)
            {
                var style = species.Definition.Pattern; if (style == null) continue;
                var modulation = textured ? 1 + patternContrast * style.Signal(x, y) : 1;
                r += species.GasFraction * style.R * modulation; g += species.GasFraction * style.G * modulation; b += species.GasFraction * style.B * modulation;
            }
            return MixtureSrgbColor.Linear(r, g, b);
        }
    }

    /// <summary>One source/revision label over shared immutable composition, warning and appearance analysis.</summary>
    public sealed class MixturePresentation
    {
        private readonly MixturePresentationData data;
        private string? sourceId;
        private readonly int cellIndex;
        public long Revision { get; }
        public string SourceId
        {
            get
            {
                var existing = Volatile.Read(ref sourceId); if (existing != null) return existing;
                var created = "owned-cell:" + cellIndex.ToString(CultureInfo.InvariantCulture);
                return Interlocked.CompareExchange(ref sourceId, created, null) ?? created;
            }
        }
        public bool IsReadOnly => true;
        public string MaterialModelVersion => data.MaterialModelVersion;
        public string MaterialModelFingerprint => data.MaterialModelFingerprint;
        public string DisplayName => data.DisplayName;
        public IReadOnlyList<MixtureConstituentPresentation> Constituents => data.Constituents;
        public IReadOnlyList<MixtureConstituentPresentation> GasConstituents => data.GasConstituents;
        public double TotalMassKg => data.TotalMassKg;
        public double GasMassKg => data.GasMassKg;
        public double GasMoles => data.GasMoles;
        public double TemperatureKelvin => data.TemperatureKelvin;
        public double TotalPressurePa => data.TotalPressurePa;
        public double GasPressurePa => data.GasPressurePa;
        public double GasVolumeCubicMetres => data.GasVolumeCubicMetres;
        public double GasDensityKgPerCubicMetre => data.GasDensityKgPerCubicMetre;
        public double LiquidVolumeFraction => data.LiquidVolumeFraction;
        public double OxygenMoleFraction => data.OxygenMoleFraction;
        public double OxygenPartialPressurePa => data.OxygenPartialPressurePa;
        public double Co2MoleFraction => data.Co2MoleFraction;
        public double Co2PartialPressurePa => data.Co2PartialPressurePa;
        public BreathingFailure Failures => data.Failures;
        public bool IsBreathable => data.IsBreathable;
        public IReadOnlyList<string> Warnings => data.Warnings;
        public MixtureSrgbColor BaseColor => data.BaseColor;
        public bool PatternEnabled => data.PatternEnabled;
        public double PatternContrast => data.PatternContrast;
        internal MixturePresentation(MixturePresentationData data, long revision, string sourceId)
        { this.data = data; Revision = revision; this.sourceId = sourceId; cellIndex = -1; }

        internal MixturePresentation(MixturePresentationData data, long revision, int cellIndex)
        { this.data = data; Revision = revision; this.cellIndex = cellIndex; }

        public MixtureSrgbColor ColorAt(double worldX, double worldY) => data.ColorAt(worldX, worldY);
    }

    internal sealed class MixturePatternStyle
    {
        internal double R { get; }
        internal double G { get; }
        internal double B { get; }
        private readonly double phaseX, phaseY;
        private readonly int harmonicX, harmonicY;
        internal MixturePatternStyle(string materialId, MixtureSrgbColor color)
        {
            R = MixtureSrgbColor.ToLinear(color.R); G = MixtureSrgbColor.ToLinear(color.G); B = MixtureSrgbColor.ToLinear(color.B);
            // FNV-1a over UTF-16 code units is fixed across processes and platforms,
            // unlike string.GetHashCode. Pattern identity never depends on rank.
            uint hash = 2166136261;
            foreach (var character in materialId) { hash ^= character; hash = unchecked(hash * 16777619); }
            phaseX = (hash & 1023) * (2 * Math.PI / 1024);
            phaseY = ((hash >> 10) & 1023) * (2 * Math.PI / 1024);
            harmonicX = 1 + (int)((hash >> 20) & 1); harmonicY = 1 + (int)((hash >> 21) & 1);
        }
        internal double Signal(double x, double y) => Math.Cos(x * Math.PI / 2 * harmonicX + phaseX) * Math.Cos(y * Math.PI / 2 * harmonicY + phaseY);
    }

    internal sealed class MixtureResolvedMaterial
    {
        internal FluidMaterial Material { get; }
        internal MixtureSpeciesStyle Style { get; }
        internal MixturePatternStyle? Pattern { get; }
        internal double? ContaminantLimit { get; }
        internal bool IsOxygen { get; }
        internal bool IsCo2 { get; }
        internal MixtureResolvedMaterial(FluidMaterial material, MixtureSpeciesStyle style, double? limit, string oxygenId, string co2Id)
        {
            Material = material; Style = style; ContaminantLimit = limit;
            if (material.Phase == NativeMatterPhase.Gas) Pattern = new MixturePatternStyle(material.Id, style.Color);
            IsOxygen = material.Id == oxygenId; IsCo2 = material.Id == co2Id;
        }
    }

    internal readonly struct MixtureConstituentValue
    {
        internal MixtureResolvedMaterial Definition { get; }
        internal double MassKg { get; }
        internal double Moles { get; }
        internal double GasFraction { get; }
        internal MixtureConstituentValue(MixtureResolvedMaterial definition, double mass, double moles, double fraction = 0)
        { Definition = definition; MassKg = mass; Moles = moles; GasFraction = fraction; }
    }

    public sealed class MixturePresentationModel
    {
        private readonly FluidMaterialCatalog catalog;
        private readonly IReadOnlyDictionary<string, MixtureSpeciesStyle> styles;
        private readonly IReadOnlyDictionary<string, double> contaminantLimits;
        private readonly AtmosphereSettings settings;
        private readonly MixtureVisualOptions options;
        private readonly string oxygenId, co2Id;
        private readonly Dictionary<string, MixtureResolvedMaterial> resolvedMaterials = new Dictionary<string, MixtureResolvedMaterial>(StringComparer.Ordinal);
        private readonly ConditionalWeakTable<FluidInventory, CachedAnalysis> analysisCache = new ConditionalWeakTable<FluidInventory, CachedAnalysis>();
        private static readonly ConditionalWeakTable<FluidInventory, CachedAnalysis>.CreateValueCallback CreateAnalysisCache = _ => new CachedAnalysis();
        public string MaterialModelVersion => catalog.Version;
        public string MaterialModelFingerprint => catalog.Fingerprint;
        public string OxygenMaterialId => oxygenId;
        public string CarbonDioxideMaterialId => co2Id;
        public bool HardMode => settings.HardMode;
        public double MinimumOxygenFraction => settings.MinimumOxygenFraction;
        public double MaximumOxygenFraction => settings.MaximumOxygenFraction;
        public double MinimumOxygenPartialPressurePa => settings.MinimumOxygenPartialPressurePa;
        public double MaximumCo2Fraction => settings.MaximumCo2Fraction;
        public double MaximumCo2PartialPressurePa => settings.MaximumCo2PartialPressurePa;
        public IReadOnlyList<string> GasMaterialIds { get; }
        public IReadOnlyDictionary<string, MixtureSpeciesStyle> Styles => styles;
        public MixtureVisualOptions VisualOptions => options.Copy();
        public AtmosphereSettings BreathingSettings => AtmosphereSettings.FromJson(settings.ToJson());

        public MixturePresentationModel(FluidMaterialCatalog catalog, IEnumerable<MixtureSpeciesStyle> styles,
            AtmosphereSettings settings, string oxygenMaterialId, string carbonDioxideMaterialId,
            IReadOnlyDictionary<string, double>? contaminantMoleFractionLimits = null, MixtureVisualOptions? options = null)
        {
            this.catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
            if (styles == null) throw new ArgumentNullException(nameof(styles));
            if (settings == null) throw new ArgumentNullException(nameof(settings)); settings.Validate();
            this.settings = AtmosphereSettings.FromJson(settings.ToJson()); this.options = (options ?? new MixtureVisualOptions()).Copy();
            FluidChecks.Identity(oxygenMaterialId, nameof(oxygenMaterialId)); FluidChecks.Identity(carbonDioxideMaterialId, nameof(carbonDioxideMaterialId));
            if (oxygenMaterialId == carbonDioxideMaterialId || catalog.Get(oxygenMaterialId).Phase != NativeMatterPhase.Gas ||
                catalog.Get(carbonDioxideMaterialId).Phase != NativeMatterPhase.Gas) throw new ArgumentException("Distinct gas identities for oxygen and carbon dioxide are required.");
            oxygenId = oxygenMaterialId; co2Id = carbonDioxideMaterialId;
            var definitions = new Dictionary<string, MixtureSpeciesStyle>(StringComparer.Ordinal);
            foreach (var style in styles)
            {
                if (style == null || definitions.ContainsKey(style.MaterialId)) throw new ArgumentException("Null or duplicate species style.");
                catalog.Get(style.MaterialId); definitions.Add(style.MaterialId, style);
            }
            foreach (var material in catalog.Materials.Values)
                if (!definitions.ContainsKey(material.Id)) throw new ArgumentException("Missing explicit species style for " + material.Id);
            this.styles = new ReadOnlyDictionary<string, MixtureSpeciesStyle>(definitions);
            GasMaterialIds = Array.AsReadOnly(catalog.Materials.Values.Where(material => material.Phase == NativeMatterPhase.Gas)
                .Select(material => material.Id).OrderBy(id => id, StringComparer.Ordinal).ToArray());
            var limits = new Dictionary<string, double>(StringComparer.Ordinal);
            if (contaminantMoleFractionLimits != null)
                foreach (var entry in contaminantMoleFractionLimits)
                {
                    FluidChecks.Nonnegative(entry.Value, "contaminant mole-fraction limit");
                    if (entry.Value > 1 || catalog.Get(entry.Key).Phase != NativeMatterPhase.Gas) throw new ArgumentException("Contaminant limits require gas identities and fractions in [0,1].");
                    limits.Add(entry.Key, entry.Value);
                }
            contaminantLimits = new ReadOnlyDictionary<string, double>(limits);
            foreach (var material in catalog.Materials.Values)
                resolvedMaterials.Add(material.Id, new MixtureResolvedMaterial(material, definitions[material.Id],
                    limits.TryGetValue(material.Id, out var maximum) ? maximum : (double?)null, oxygenId, co2Id));
        }

        public MixturePresentationModel WithVisualOptions(MixtureVisualOptions visualOptions)
        {
            if (visualOptions == null) throw new ArgumentNullException(nameof(visualOptions));
            return new MixturePresentationModel(catalog, styles.Values, settings, oxygenId, co2Id, contaminantLimits, visualOptions);
        }

        internal void RequireCatalog(ColonyFrameSnapshot frame)
        {
            if (frame.MaterialModelVersion != catalog.Version || frame.MaterialModelFingerprint != catalog.Fingerprint)
                throw new InvalidOperationException("Presentation and inventory must use the same explicit material model.");
        }

        public MixturePresentation Describe(FluidInventory inventory, double volumeCubicMetres, long revision = 0, string sourceId = "inventory")
        {
            if (inventory == null) throw new ArgumentNullException(nameof(inventory));
            FluidChecks.Positive(volumeCubicMetres, nameof(volumeCubicMetres)); FluidChecks.Identity(sourceId, nameof(sourceId));
            if (revision < 0) throw new ArgumentOutOfRangeException(nameof(revision));
            return new MixturePresentation(analysisCache.GetValue(inventory, CreateAnalysisCache).Get(this, inventory, volumeCubicMetres), revision, sourceId);
        }

        private sealed class CachedAnalysis
        {
            private long firstVolume;
            private MixturePresentationData? first;
            private Dictionary<long, MixturePresentationData>? otherVolumes;
            internal MixturePresentationData Get(MixturePresentationModel model, FluidInventory inventory, double volume)
            {
                lock (this) return GetLocked(model, inventory, volume);
            }
            private MixturePresentationData GetLocked(MixturePresentationModel model, FluidInventory inventory, double volume)
            {
                var key = BitConverter.DoubleToInt64Bits(volume);
                if (first != null && firstVolume == key) return first;
                if (otherVolumes != null && otherVolumes.TryGetValue(key, out var cached)) return cached;
                var created = model.Analyze(inventory, volume);
                if (first == null) { firstVolume = key; first = created; }
                else if (otherVolumes != null && otherVolumes.Count == 3)
                { otherVolumes.Clear(); firstVolume = key; first = created; }
                else
                {
                    otherVolumes ??= new Dictionary<long, MixturePresentationData>(); otherVolumes.Add(key, created);
                }
                return created;
            }
        }

        internal MixturePresentation DescribeCell(FluidInventory inventory, double volume, long revision, int cell)
            => new MixturePresentation(analysisCache.GetValue(inventory, CreateAnalysisCache).Get(this, inventory, volume), revision, cell);

        private MixturePresentationData Analyze(FluidInventory inventory, double volumeCubicMetres)
        {
            // Preserve the catalog's summation order and constant-property equations,
            // but traverse the immutable sorted inventory only once. The captured
            // constituent values also serve rendering and lazy inspection.
            var values = inventory.MassesKg.Count == 0 ? Array.Empty<MixtureConstituentValue>() : new MixtureConstituentValue[inventory.MassesKg.Count];
            var gasMoles = 0.0; var gasMass = 0.0; var liquidVolume = 0.0;
            var gasConstituentCount = 0; var index = 0;
            var cv = 0.0; var offset = 0.0; var minimumTemperature = 0.0; var maximumTemperature = double.MaxValue;
            foreach (var entry in inventory.MassEntries)
            {
                if (!resolvedMaterials.TryGetValue(entry.Key, out var resolved)) throw new InvalidOperationException("Unsupported fluid material: " + entry.Key);
                var material = resolved.Material; var moles = entry.Value / material.MolarMassKgPerMol;
                FluidChecks.Nonnegative(moles, "constituent moles");
                values[index++] = new MixtureConstituentValue(resolved, entry.Value, moles);
                cv += entry.Value * material.SpecificHeatCvJoulesPerKgKelvin; offset += entry.Value * material.EnergyOffsetJoulesPerKg;
                minimumTemperature = Math.Max(minimumTemperature, material.MinimumTemperatureKelvin);
                maximumTemperature = Math.Min(maximumTemperature, material.MaximumTemperatureKelvin);
                if (material.Phase == NativeMatterPhase.Gas) { gasMoles += moles; gasMass += entry.Value; ++gasConstituentCount; }
                else liquidVolume += entry.Value / material.LiquidDensityKgPerCubicMetre;
            }
            FluidChecks.Nonnegative(gasMoles, "gas moles");
            var temperature = 0.0;
            if (inventory.TotalMassKg != 0)
            {
                temperature = (inventory.InternalEnergyJoules - offset) / cv;
                FluidChecks.Positive(temperature, "reconstructed temperature");
                if (temperature < minimumTemperature || temperature > maximumTemperature)
                    throw new InvalidOperationException("Temperature outside a declared material model.");
            }
            FluidChecks.Nonnegative(liquidVolume, "liquid volume");
            if (liquidVolume > volumeCubicMetres || (gasMoles > 0 && liquidVolume >= volumeCubicMetres))
                throw new InvalidOperationException("Fluid inventory exceeds cell volume or leaves no gas headspace.");
            var gasVolume = volumeCubicMetres - liquidVolume;
            var pressure = gasMoles == 0 ? 0 : gasMoles * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * temperature / gasVolume;
            FluidChecks.Nonnegative(pressure, "gas pressure");
            var oxygen = 0.0; var co2 = 0.0; var hasContaminant = false; var hasCo2 = false;
            for (var i = 0; i < values.Length; ++i)
            {
                var value = values[i]; var resolved = value.Definition;
                if (resolved.Material.Phase != NativeMatterPhase.Gas) continue;
                var fraction = gasMoles == 0 ? 0 : value.Moles / gasMoles;
                values[i] = new MixtureConstituentValue(resolved, value.MassKg, value.Moles, fraction);
                if (resolved.ContaminantLimit.HasValue && ExceedsLimit(resolved.ContaminantLimit.Value, fraction, value.MassKg > 0)) hasContaminant = true;
                if (resolved.IsOxygen) oxygen = fraction;
                if (resolved.IsCo2) { co2 = fraction; hasCo2 = value.MassKg > 0; }
            }
            var failures = Assess(gasMoles, oxygen, co2, pressure, hasContaminant, hasCo2);
            var properties = new FluidProperties(temperature, pressure, liquidVolume / volumeCubicMetres, null, NativeMatterPhase.Vacuum);
            return new MixturePresentationData(this, inventory, gasMass, gasMoles,
                properties, gasVolume, oxygen, co2, failures, values, gasConstituentCount, options);
        }

        internal IReadOnlyList<MixtureConstituentPresentation> BuildRows(MixturePresentationData data)
        {
            var rows = new List<MixtureConstituentPresentation>(data.Values.Length);
            foreach (var value in data.Values)
            {
                var resolved = value.Definition; var material = resolved.Material; var isGas = material.Phase == NativeMatterPhase.Gas;
                var limit = resolved.ContaminantLimit; var contaminant = isGas && limit.HasValue && ExceedsLimit(limit.Value, value.GasFraction, value.MassKg > 0);
                rows.Add(new MixtureConstituentPresentation(material.Id, resolved.Style.DisplayName, material.Phase, value.MassKg, value.Moles,
                    isGas ? value.GasFraction : (double?)null, isGas ? value.GasFraction * data.TotalPressurePa : (double?)null,
                    limit, isGas && value.GasFraction < options.MeaningfulGasMoleFraction, contaminant, data.GasConstituentCount > 1));
            }
            rows.Sort((left, right) =>
            {
                var phase = PhaseOrder(left.Phase).CompareTo(PhaseOrder(right.Phase)); if (phase != 0) return phase;
                var amount = left.Phase == NativeMatterPhase.Gas ? right.GasMoleFraction!.Value.CompareTo(left.GasMoleFraction!.Value) : right.MassKg.CompareTo(left.MassKg);
                return amount != 0 ? amount : StringComparer.Ordinal.Compare(left.MaterialId, right.MaterialId);
            });
            return new ReadOnlyCollection<MixtureConstituentPresentation>(rows);
        }

        private static int PhaseOrder(NativeMatterPhase phase) => phase == NativeMatterPhase.Gas ? 0 : phase == NativeMatterPhase.Liquid ? 1 : 2;
        // Tolerance follows the values' scale. An absolute unit-sized floor
        // would silently exempt sufficiently small hazardous traces.
        private static bool Below(double a, double b) => a < b && b - a > 16 * 2.2204460492503131e-16 * Math.Max(Math.Abs(a), Math.Abs(b));
        private static bool ExceedsLimit(double limit, double value, bool knownPresent) => knownPresent && (limit == 0 || Below(limit, value));

        private BreathingFailure Assess(double gasMoles, double oxygen, double co2, double pressure, bool hasContaminant, bool hasCo2)
        {
            if (gasMoles == 0) return BreathingFailure.Vacuum;
            var result = BreathingFailure.None;
            if (Below(oxygen, settings.MinimumOxygenFraction)) result |= BreathingFailure.OxygenFractionLow;
            if (settings.HardMode && Below(settings.MaximumOxygenFraction, oxygen)) result |= BreathingFailure.OxygenFractionHigh;
            if (Below(oxygen * pressure, settings.MinimumOxygenPartialPressurePa)) result |= BreathingFailure.OxygenPartialPressureLow;
            if (ExceedsLimit(settings.MaximumCo2Fraction, co2, hasCo2)) result |= BreathingFailure.CarbonDioxideFractionHigh;
            if (ExceedsLimit(settings.MaximumCo2PartialPressurePa, co2 * pressure, hasCo2)) result |= BreathingFailure.CarbonDioxidePartialPressureHigh;
            if (hasContaminant) result |= BreathingFailure.Contaminant;
            return result;
        }

        internal List<string> Warnings(BreathingFailure failures, double oxygen, double co2, double pressure, IReadOnlyList<MixtureConstituentPresentation> rows)
        {
            var result = new List<string>();
            bool Has(BreathingFailure flag) => (failures & flag) != 0;
            if (Has(BreathingFailure.Vacuum)) result.Add("No gas is present; there is no breathable atmosphere.");
            if (Has(BreathingFailure.OxygenFractionLow)) result.Add("Oxygen fraction is too low: " + MixturePresentationText.Percent(oxygen) + "; minimum " + MixturePresentationText.Percent(settings.MinimumOxygenFraction) + ".");
            if (Has(BreathingFailure.OxygenFractionHigh)) result.Add("Oxygen fraction is too high: " + MixturePresentationText.Percent(oxygen) + "; maximum " + MixturePresentationText.Percent(settings.MaximumOxygenFraction) + ".");
            if (Has(BreathingFailure.OxygenPartialPressureLow)) result.Add("Oxygen partial pressure is too low: " + Number(oxygen * pressure) + " Pa; minimum " + Number(settings.MinimumOxygenPartialPressurePa) + " Pa.");
            if (Has(BreathingFailure.CarbonDioxideFractionHigh)) result.Add("Carbon dioxide fraction exceeds its limit: " + MixturePresentationText.Percent(co2) + "; maximum " + MixturePresentationText.Percent(settings.MaximumCo2Fraction) + ".");
            if (Has(BreathingFailure.CarbonDioxidePartialPressureHigh)) result.Add("Carbon dioxide partial pressure exceeds its limit: " + Number(co2 * pressure) + " Pa; maximum " + Number(settings.MaximumCo2PartialPressurePa) + " Pa.");
            foreach (var row in rows.Where(row => row.IsContaminant))
                result.Add("Contaminant limit exceeded: " + row.DisplayName + " " + row.MolePercentText + "; maximum " + MixturePresentationText.Percent(row.ContaminantMoleFractionLimit!.Value) + ".");
            return result;
        }

        internal string Name(IReadOnlyList<MixtureConstituentPresentation> rows, BreathingFailure failures)
        {
            var status = failures == BreathingFailure.None ? "Breathable" :
                (failures & BreathingFailure.Vacuum) != 0 ? "Vacuum / no atmosphere" : "Unsafe atmosphere";
            var gases = rows.Where(row => row.Phase == NativeMatterPhase.Gas).ToArray();
            if (gases.Length == 0) return rows.Count == 0 ? status :
                (rows.Count == 1 ? rows[0].DisplayName + " (liquid)" : "Liquid mixture") + "\n" + status;
            if (gases.Length == 1) return gases[0].DisplayName + "\n" + status;
            var meaningful = gases.Where(row => !row.IsTrace).Take(2).ToArray();
            if (meaningful.Length == 0) meaningful = gases.Take(2).ToArray();
            var title = string.Join("–", meaningful.Select(row => row.DisplayName)) + " mixture";
            if (gases.Length > meaningful.Length) title += gases.Except(meaningful).All(row => row.IsTrace) ? " (+ traces)" : " (+ other gases)";
            return title + "\n" + status;
        }

        private static string Number(double value) => value.ToString("G8", CultureInfo.InvariantCulture);

        public MixturePresentationFrame CaptureFrame(ColonyFrameSnapshot frame, IEnumerable<MixtureContainerSample>? containers = null)
            => new MixturePresentationFrame(this, frame, containers);
    }
}
