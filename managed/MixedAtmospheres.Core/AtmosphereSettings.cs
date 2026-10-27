using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.RegularExpressions;

namespace MixedAtmospheres.Core
{
    public sealed class AtmosphereSettings
    {
        public bool HardMode { get; set; } = true;
        public bool EnforceCarbonDioxideLimits { get; set; } = true;
        public double MinimumOxygenFraction { get; set; } = 0.15;
        public double MaximumOxygenFraction { get; set; } = 0.35;
        public double MinimumOxygenPartialPressurePa { get; set; } = 15000;
        public double MaximumCo2Fraction { get; set; } = 0.005;
        public double MaximumCo2PartialPressurePa { get; set; } = 500;

        // This is deliberately a fixed, flat settings format. Unity's Linux
        // Mono depot omits the machine.config required by DataContract JSON.
        // Keep the standalone core independent of the game's JSON assembly.
        private static readonly Regex PropertyToken = new Regex(
            @"\G[ \t\r\n]*""(?<name>(?:[^""\\\x00-\x1f]|\\(?:[""\\/bfnrt]|u[0-9a-fA-F]{4}))*)""[ \t\r\n]*:[ \t\r\n]*(?<value>true|false|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)[ \t\r\n]*(?<end>[,}])",
            RegexOptions.CultureInvariant);
        private static readonly Regex NameEscape = new Regex(@"\\(?:u(?<hex>[0-9a-fA-F]{4})|(?<character>[""\\/bfnrt]))", RegexOptions.CultureInvariant);

        public void Validate()
        {
            foreach (var value in new[] { MinimumOxygenFraction, MaximumOxygenFraction, MinimumOxygenPartialPressurePa, MaximumCo2Fraction, MaximumCo2PartialPressurePa })
                if (double.IsNaN(value) || double.IsInfinity(value)) throw new ArgumentException("Atmosphere settings must be finite.");
            if (MinimumOxygenFraction <= 0 || MaximumOxygenFraction < MinimumOxygenFraction || MaximumOxygenFraction > 1 ||
                MinimumOxygenPartialPressurePa <= 0 || MaximumCo2Fraction < 0 || MaximumCo2Fraction > 1 || MaximumCo2PartialPressurePa < 0)
                throw new ArgumentException("Invalid atmosphere thresholds.");
        }

        internal NativeAtmosphere ToNative()
        {
            Validate();
            return new NativeAtmosphere
            {
                HardMode = HardMode ? 1U : 0U,
                MinimumOxygenFraction = MinimumOxygenFraction,
                MaximumOxygenFraction = MaximumOxygenFraction,
                MinimumOxygenPressure = MinimumOxygenPartialPressurePa,
                MaximumCo2Fraction = EnforceCarbonDioxideLimits ? MaximumCo2Fraction : 1,
                MaximumCo2Pressure = EnforceCarbonDioxideLimits ? MaximumCo2PartialPressurePa : double.MaxValue
            };
        }

        public string ToJson()
        {
            Validate();
            string Number(double value) => value.ToString("R", CultureInfo.InvariantCulture);
            return "{\"HardMode\":" + (HardMode ? "true" : "false") +
                ",\"EnforceCarbonDioxideLimits\":" + (EnforceCarbonDioxideLimits ? "true" : "false") +
                ",\"MinimumOxygenFraction\":" + Number(MinimumOxygenFraction) +
                ",\"MaximumOxygenFraction\":" + Number(MaximumOxygenFraction) +
                ",\"MinimumOxygenPartialPressurePa\":" + Number(MinimumOxygenPartialPressurePa) +
                ",\"MaximumCo2Fraction\":" + Number(MaximumCo2Fraction) +
                ",\"MaximumCo2PartialPressurePa\":" + Number(MaximumCo2PartialPressurePa) + "}";
        }

        public static AtmosphereSettings FromJson(string json)
        {
            if (json == null) throw new ArgumentNullException(nameof(json));
            if (json.Length > 65536) throw new ArgumentException("Atmosphere settings are too large.", nameof(json));
            var position = 0;
            void SkipSpace()
            {
                while (position < json.Length && (json[position] == ' ' || json[position] == '\t' || json[position] == '\r' || json[position] == '\n'))
                    ++position;
            }
            SkipSpace();
            if (position == json.Length || json[position++] != '{')
                throw new ArgumentException("Expected a JSON settings object.", nameof(json));
            var result = new AtmosphereSettings();
            var names = new HashSet<string>(StringComparer.Ordinal);
            while (true)
            {
                var token = PropertyToken.Match(json, position);
                if (!token.Success) throw new ArgumentException("Invalid or incomplete atmosphere settings JSON.", nameof(json));
                position += token.Length;
                var name = NameEscape.Replace(token.Groups["name"].Value, escape =>
                {
                    if (escape.Groups["hex"].Success)
                        return ((char)int.Parse(escape.Groups["hex"].Value, NumberStyles.HexNumber, CultureInfo.InvariantCulture)).ToString();
                    var character = escape.Groups["character"].Value[0];
                    return character switch { 'b' => "\b", 'f' => "\f", 'n' => "\n", 'r' => "\r", 't' => "\t", _ => character.ToString() };
                });
                if (!names.Add(name)) throw new ArgumentException("Duplicate atmosphere setting: " + name, nameof(json));
                var value = token.Groups["value"].Value;
                if (name == nameof(HardMode) || name == nameof(EnforceCarbonDioxideLimits))
                {
                    if (value != "true" && value != "false") throw new ArgumentException(name + " must be a JSON boolean.", nameof(json));
                    if (name == nameof(HardMode)) result.HardMode = value == "true";
                    else result.EnforceCarbonDioxideLimits = value == "true";
                }
                else
                {
                    if (!double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var number))
                        throw new ArgumentException("Atmosphere thresholds must be JSON numbers.", nameof(json));
                    switch (name)
                    {
                        case nameof(MinimumOxygenFraction): result.MinimumOxygenFraction = number; break;
                        case nameof(MaximumOxygenFraction): result.MaximumOxygenFraction = number; break;
                        case nameof(MinimumOxygenPartialPressurePa): result.MinimumOxygenPartialPressurePa = number; break;
                        case nameof(MaximumCo2Fraction): result.MaximumCo2Fraction = number; break;
                        case nameof(MaximumCo2PartialPressurePa): result.MaximumCo2PartialPressurePa = number; break;
                        default: throw new ArgumentException("Unknown atmosphere setting: " + name, nameof(json));
                    }
                }
                if (token.Groups["end"].Value == "}") break;
            }
            SkipSpace();
            if (position != json.Length || names.Count < 6 || names.Count > 7 ||
                !names.Contains(nameof(HardMode)) || !names.Contains(nameof(MinimumOxygenFraction)) ||
                !names.Contains(nameof(MaximumOxygenFraction)) || !names.Contains(nameof(MinimumOxygenPartialPressurePa)) ||
                !names.Contains(nameof(MaximumCo2Fraction)) || !names.Contains(nameof(MaximumCo2PartialPressurePa)))
                throw new ArgumentException("Expected all six atmosphere thresholds/flags, an optional CO2 switch and no trailing data.", nameof(json));
            result.Validate();
            return result;
        }
    }

    [Flags]
    public enum BreathingFailure : uint
    {
        None = 0, Vacuum = 1, OxygenFractionLow = 2, OxygenFractionHigh = 4,
        OxygenPartialPressureLow = 8, CarbonDioxideFractionHigh = 16,
        CarbonDioxidePartialPressureHigh = 32, Contaminant = 64
    }

    public sealed class Breathability
    {
        public BreathingFailure Failures { get; }
        public bool IsBreathable => Failures == BreathingFailure.None;
        public double PressurePa { get; }
        public double OxygenMoleFraction { get; }
        public double OxygenPartialPressurePa { get; }
        public double Co2MoleFraction { get; }
        public double Co2PartialPressurePa { get; }
        internal Breathability(NativeBreathing result)
        {
            Failures = (BreathingFailure)result.Failures;
            PressurePa = result.Pressure; OxygenMoleFraction = result.OxygenFraction;
            OxygenPartialPressurePa = result.OxygenPressure; Co2MoleFraction = result.Co2Fraction;
            Co2PartialPressurePa = result.Co2Pressure;
        }
    }
}
