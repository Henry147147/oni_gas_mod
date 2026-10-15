using System;
using System.Collections.Generic;
using System.Linq;
using System.Buffers.Binary;
using System.Globalization;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

if (args.Length == 0) NativeTestLibrary.PinFromEnvironment();
if (args.Length != 0)
{
    if (args.Length != 2 || args[0] != "--native-root") throw new ArgumentException("Usage: --native-root <staged mod folder>");
    MixedAtmospheres.Oni.NativeLibraryBootstrap.Load(args[1]);
}

var passed = 0;
void Test(string name, Action test)
{
    test(); ++passed; Console.WriteLine("PASS " + name);
}
void Check(bool condition) { if (!condition) throw new Exception("Assertion failed"); }
void Near(double actual, double expected) { Check(double.IsFinite(actual) && Math.Abs(actual - expected) <= 1e-8 * Math.Max(1, Math.Abs(expected))); }
void Reject(Action action)
{
    var caught = false;
    try { action(); } catch (Exception) { caught = true; }
    Check(caught);
}
FluidWorld Create() => new(new WorldConfiguration { Width = 3, Height = 2, Gravity = 0, Diffusivity = 0, ThermalConduction = false });
var air = new Dictionary<string, double> { ["oxygen"] = 0.21 * 0.0319988 * 40, ["nitrogen"] = 0.79 * 0.0280134 * 40 };

Test("native ABI loading and species identity", () =>
{
    using var world = Create();
    Check(world.SpeciesIds.Contains("nitrogen")); Check(world.Width == 3 && world.Height == 2);
    world.InitializeCell(0, 0, air, 300);
    var sample = world.Sample(0, 0); Near(sample.TotalMassKg, air.Values.Sum()); Near(sample.TemperatureKelvin, 300);
    Near(world.AssessAtmosphere(0, 0).OxygenMoleFraction, 0.21); Check(world.AssessAtmosphere(0, 0).IsBreathable);
});
Test("mixture snapshot and deterministic continuation", () =>
{
    using var world = Create();
    for (uint y = 0; y < 2; ++y) for (uint x = 0; x < 3; ++x) world.InitializeCell(x, y, air, 290 + x * 10);
    var initial = world.Totals(); world.Step(0.002); var bytes = world.Save();
    using var restored = FluidWorld.Restore(bytes); Check(bytes.SequenceEqual(restored.Save()));
    world.Step(0.003); restored.Step(0.003); Check(world.Save().SequenceEqual(restored.Save()));
    Near(world.Totals().EnergyJoules, initial.EnergyJoules);
    foreach (var component in initial.MassesKg) Near(world.Totals().MassesKg[component.Key], component.Value);
});
Test("native errors preserve world and cross managed boundary", () =>
{
    using var world = Create(); world.InitializeCell(0, 0, air, 300); var before = world.Save();
    Reject(() => world.InitializeCell(0, 0, new Dictionary<string, double> { ["oxygen"] = -1 }, 300));
    Reject(() => world.InitializeCell(0, 0, new Dictionary<string, double> { ["unknown"] = 1 }, 300));
    Reject(() => world.SetWall(0, 0, true)); Reject(() => world.Step(1, 1));
    Check(before.SequenceEqual(world.Save()));
});
Test("breathing presets and settings persistence", () =>
{
    using var world = Create(); world.InitializeCell(0, 0, new Dictionary<string, double> { ["oxygen"] = 1 }, 300);
    Check(!world.AssessAtmosphere(0, 0).IsBreathable);
    var easy = AtmosphereSettings.FromJson(new AtmosphereSettings { HardMode = false }.ToJson());
    Check(world.AssessAtmosphere(0, 0, easy).IsBreathable); Near(easy.MinimumOxygenFraction, 0.15);
    Reject(() => AtmosphereSettings.FromJson("{}"));
});
Test("settings JSON is culture independent and accepts reordered escaped names", () =>
{
    var previous = CultureInfo.CurrentCulture;
    try
    {
        CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("de-DE");
        var custom = new AtmosphereSettings
        {
            HardMode = false, MinimumOxygenFraction = 0.17000000000000004, MaximumOxygenFraction = 0.32,
            MinimumOxygenPartialPressurePa = 16000.25, MaximumCo2Fraction = 0.004,
            MaximumCo2PartialPressurePa = 450.125
        };
        var restored = AtmosphereSettings.FromJson(custom.ToJson());
        Check(!restored.HardMode && restored.MinimumOxygenFraction == custom.MinimumOxygenFraction &&
            restored.MaximumOxygenFraction == custom.MaximumOxygenFraction &&
            restored.MinimumOxygenPartialPressurePa == custom.MinimumOxygenPartialPressurePa &&
            restored.MaximumCo2Fraction == custom.MaximumCo2Fraction &&
            restored.MaximumCo2PartialPressurePa == custom.MaximumCo2PartialPressurePa);
        var reordered = AtmosphereSettings.FromJson(" \r\n{\"MaximumCo2PartialPressurePa\":5e2,\"MaximumCo2Fraction\":0.005," +
            "\"MinimumOxygenPartialPressurePa\":1.5E+4,\"MaximumOxygenFraction\":0.35,\"MinimumOxygenFraction\":0.15," +
            "\"\\u0048ardMode\":true} \t");
        Check(reordered.HardMode); Near(reordered.MinimumOxygenPartialPressurePa, 15000);
    }
    finally { CultureInfo.CurrentCulture = previous; }
});
Test("settings JSON rejects missing duplicate unknown and trailing fields", () =>
{
    var valid = new AtmosphereSettings().ToJson();
    foreach (var invalid in new[]
    {
        "null", "[]", "{}", valid.Replace("\"HardMode\":true,", ""),
        valid.Replace("HardMode", "Unknown"), valid.Replace("HardMode", "hardMode"),
        valid.Insert(1, "\"HardMode\":false,"), valid.Insert(1, "\"\\u0048ardMode\":false,"),
        valid.Insert(1, "\"extra\":0,"), valid + "{}", valid + "/* comment */",
        valid.Insert(valid.Length - 1, ","), valid.Replace(",", " "),
        valid.Insert(1, "\"bad\nname\":0,"), valid.Insert(1, "\"bad\\xname\":0,"),
        new string(' ', 65537) + valid
    }) Reject(() => AtmosphereSettings.FromJson(invalid));
});
Test("settings JSON rejects invalid numeric types syntax and threshold values", () =>
{
    var valid = new AtmosphereSettings().ToJson();
    foreach (var number in new[] { "NaN", "Infinity", "1e999", "\"0.15\"", "true", "null", "[]", "{}", "+0.15", ".15", "01", "1.", "1e", "-1", "0", "1.01" })
        Reject(() => AtmosphereSettings.FromJson(valid.Replace("\"MinimumOxygenFraction\":0.15", "\"MinimumOxygenFraction\":" + number)));
    Reject(() => AtmosphereSettings.FromJson(valid.Replace("\"HardMode\":true", "\"HardMode\":1")));
    Reject(() => AtmosphereSettings.FromJson(valid.Replace("\"MaximumOxygenFraction\":0.35", "\"MaximumOxygenFraction\":0.1")));
    Reject(() => new AtmosphereSettings { MaximumCo2Fraction = double.NaN }.ToJson());
    Reject(() => new AtmosphereSettings { MinimumOxygenPartialPressurePa = double.PositiveInfinity }.ToJson());
});
Test("corrupted snapshots rejected", () =>
{
    using var world = Create(); var bytes = world.Save(); bytes[bytes.Length / 2] ^= 1;
    Reject(() => FluidWorld.Restore(bytes)); Reject(() => FluidWorld.Restore(Array.Empty<byte>()));
});
Test("disposed native handles cannot be used", () =>
{
    var world = Create(); world.Dispose(); world.Dispose(); Reject(() => world.Sample(0, 0));
});
var phases = new Dictionary<int, NativeMatterPhase>
{ [1] = NativeMatterPhase.Vacuum, [2] = NativeMatterPhase.Gas, [3] = NativeMatterPhase.Liquid, [4] = NativeMatterPhase.Solid };
byte[] NativeFixture()
{
    const int count = 16;
    var bytes = new byte[29 + 36 * count];
    new byte[] { 83, 73, 77, 83, 65, 86, 69, 0 }.CopyTo(bytes, 0);
    void Int(int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(offset, 4), value);
    void Float(int offset, float value) => Int(offset, BitConverter.SingleToInt32Bits(value));
    Int(8, 15); Int(12, 4); Int(16, 4); Int(20, 11); Int(24, 7); bytes[28] = 1;
    for (var i = 0; i < count; ++i)
    {
        var cell = 29 + i * 16; var wall = 29 + 24 * count + i * 12;
        Int(cell, 4); Float(cell + 4, 300); Float(cell + 8, 1000);
        Int(wall, 1);
    }
    Int(29 + 5 * 16, 2); Float(29 + 5 * 16 + 8, 2); Float(29 + 5 * 16 + 12, 17);
    Int(29 + 6 * 16, 3); Float(29 + 6 * 16 + 8, 100);
    Int(29 + 16 * count + 5 * 8, 123); Int(29 + 16 * count + 5 * 8 + 4, 456);
    return bytes;
}
Test("native fluid extraction preserves species disease radiation and placement", () =>
{
    var bytes = NativeFixture(); var original = (byte[])bytes.Clone();
    var result = NativeWorldBlob.Split(bytes, phases, 1);
    Check(result.Width == 2 && result.Height == 2 && result.OffsetX == 11 && result.OffsetY == 7);
    Check(result.Fluids.Count == 2); Near(result.Fluids.Sum(cell => (double)cell.MassKg), 102);
    var gas = result.Fluids[0];
    Check(gas.X == 11 && gas.Y == 7 && gas.ElementHash == 2 && gas.Phase == NativeMatterPhase.Gas);
    Check(gas.DiseaseHash == 123 && gas.DiseaseCount == 456); Near(gas.Radiation, 17); Near(gas.TemperatureKelvin, 300);
    Check(result.Fluids[1].X == 12 && result.Fluids[1].Y == 7 && result.Fluids[1].Phase == NativeMatterPhase.Liquid);
    Check(bytes.SequenceEqual(original));
    var shadow = result.NativeData.ToArray();
    foreach (var i in new[] { 5, 6 })
    {
        Check(BinaryPrimitives.ReadInt32LittleEndian(shadow.AsSpan(29 + i * 16, 4)) == 1);
        Check(shadow.AsSpan(29 + i * 16 + 4, 12).ToArray().All(value => value == 0));
        Check(shadow.AsSpan(29 + 16 * 16 + i * 8, 8).ToArray().All(value => value == 0));
    }
    // Every unrelated byte, including options and backwalls, is retained.
    foreach (var i in new[] { 5, 6 })
    {
        original.AsSpan(29 + i * 16, 16).CopyTo(shadow.AsSpan(29 + i * 16));
        original.AsSpan(29 + 16 * 16 + i * 8, 8).CopyTo(shadow.AsSpan(29 + 16 * 16 + i * 8));
    }
    Check(shadow.SequenceEqual(original));
});
Test("native blob rejects unsupported or corrupted data before migration", () =>
{
    var bytes = NativeFixture();
    Reject(() => NativeWorldBlob.Split(bytes.AsSpan(0, bytes.Length - 1), phases, 1));
    bytes[8] = 16; Reject(() => NativeWorldBlob.Split(bytes, phases, 1)); bytes[8] = 15;
    BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(12), int.MaxValue);
    Reject(() => NativeWorldBlob.Split(bytes, phases, 1));
    bytes = NativeFixture(); BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(29 + 5 * 16), 999);
    Reject(() => NativeWorldBlob.Split(bytes, phases, 1));
    bytes = NativeFixture(); BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(29 + 5 * 16 + 8), BitConverter.SingleToInt32Bits(float.NaN));
    Reject(() => NativeWorldBlob.Split(bytes, phases, 1));
});
Test("native migration refuses unowned guard and backwall fluids", () =>
{
    var bytes = NativeFixture(); BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(29), 2);
    Reject(() => NativeWorldBlob.Split(bytes, phases, 1));
    bytes = NativeFixture(); BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(29 + 24 * 16 + 5 * 12), 3);
    Reject(() => NativeWorldBlob.Split(bytes, phases, 1));
    Reject(() => NativeWorldBlob.Split(NativeFixture(), phases, 4));
});
OwnershipTests.Run(Test, Check, Reject, NativeFixture, phases);
ColonyStateTests.Run(Test, Check, Reject, NativeFixture, phases);
ColonyPlacementDiagnosticsTests.Run(Test, Check, Reject, NativeFixture, phases);
Console.WriteLine($"{passed} managed/native integration tests passed");
