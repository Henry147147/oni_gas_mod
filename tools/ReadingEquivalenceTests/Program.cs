// The mixture reading and presentation of 60,000 pseudo-random cells (some invalid,
// permuted constituents, gases and liquids) hash to the value the original
// SortedDictionary-based reading produced: constituent order, every number to
// the bit, texts and rejection messages. Colours retain a separate local-only
// digest because Math.Pow/Cos use platform-specific runtime implementations.
// Guards the lower-allocation
// reading constructor (docs/colony-owner.md#main-thread-smoothness).
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
System.Globalization.CultureInfo.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
if (args.Contains("--allocations")) return ExtendedCases.Allocations();
const string ExpectedPortable = "4B21F08E1A3F913DD4713DF976A99699FB2B33756AB24F9D3DB058A5F0000E4C";
double[] Decades = { 1e-3, 1e-2, 1e-1, 1, 10, 100, 1000 };
var ids = new[] { "oxygen", "nitrogen", "carbon_dioxide", "hydrogen", "chlorine", "water", "zeta", "Alpha" };
var phases = new[] { NativeMatterPhase.Gas, NativeMatterPhase.Gas, NativeMatterPhase.Gas, NativeMatterPhase.Gas, NativeMatterPhase.Gas, NativeMatterPhase.Liquid, NativeMatterPhase.Gas, NativeMatterPhase.Gas };
var mm = new[] { 0.0319988, 0.0280134, 0.0440095, 0.002016, 0.0709, 0.01801528, 0.03, 0.05 };
var materials = new List<FluidMaterial>();
for (int i = 0; i < ids.Length; ++i) materials.Add(phases[i] == NativeMatterPhase.Gas ? new FluidMaterial(ids[i], phases[i], mm[i], 700, 0, 0, 1e6) : new FluidMaterial(ids[i], phases[i], mm[i], 4184, 1000, 0, 0, 1e6));
var catalog = new FluidMaterialCatalog("bench", materials);
var styles = ids.Select((id, i) => new MixtureSpeciesStyle(id, id.ToUpperInvariant(), new MixtureSrgbColor(.1 + .1 * i, .9 - .1 * i, .5))).ToArray();
var model = new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide");
var rng = new Random(12345); var sb = new StringBuilder(); var portable = new StringBuilder();
using var portableHash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
void D(double v) => sb.Append(BitConverter.DoubleToInt64Bits(v)).Append(',');
MixtureThermodynamicReading? Make(int cell, bool breakIt, out string error)
{
    error = "";
    var n = rng.Next(0, 6); var pick = Enumerable.Range(0, ids.Length).OrderBy(_ => rng.Next()).Take(n).ToList();
    double T = 100 + rng.NextDouble() * 600;
    var gasIdx = pick.Where(k => phases[k] == NativeMatterPhase.Gas).ToList();
    var masses = pick.ToDictionary(k => k, k => Decades[rng.Next(Decades.Length)] * (1 + rng.NextDouble()));
    double gasMoles = gasIdx.Sum(k => masses[k] / mm[k]);
    double liquidV = pick.Where(k => phases[k] == NativeMatterPhase.Liquid).Sum(k => masses[k] / 1000);
    if (liquidV > .9) { foreach (var k in pick.Where(k => phases[k] == NativeMatterPhase.Liquid).ToList()) masses[k] *= .5 / liquidV; liquidV = .5; }
    double headspace = 1 - liquidV; double p = gasMoles > 0 ? gasMoles * 8.31446261815324 * T / headspace : 0;
    var list = new List<MixtureThermodynamicConstituent>();
    foreach (var k in pick)
    {
        double vol = phases[k] == NativeMatterPhase.Gas ? headspace * (masses[k] / mm[k]) / gasMoles : masses[k] / 1000;
        list.Add(new MixtureThermodynamicConstituent(ids[k], phases[k], masses[k], mm[k], vol));
    }
    double total = list.Sum(c => c.MassKg);
    if (breakIt && list.Count > 1) { if (rng.Next(3) == 0) list.Add(list[0]); else if (rng.Next(2) == 0) total *= 1.5; else p *= 2; }
    if (n == 0) { T = 0; p = 0; headspace = 0; }
    try
    {
        return new MixtureThermodynamicReading(list, n == 0 ? 0 : total, n == 0 ? 0 : 1000 + rng.NextDouble(), 0, T, p, p, 1, n == 0 ? 0 : headspace, liquidV, n == 0 ? 1 : 0, catalog.Version, catalog.Fingerprint, 7, "colony:cell:" + cell, resolvedVacuumInterface: true);
    }
    catch (Exception e) { error = e.GetType().Name + ":" + e.Message; return null; }
}
string chain = ""; int ok = 0, bad = 0;
for (int cell = 0; cell < 60000; ++cell)
{
    var r = Make(cell, cell % 5 == 0, out var err);
    var begun = sb.Length;
    sb.Append(cell).Append('|');
    if (r == null) { sb.Append(err).Append('\n'); portable.Append(sb, begun, sb.Length - begun); ++bad; continue; }
    ++ok;
    foreach (var c in r.Constituents) { sb.Append(c.MaterialId).Append(c.Phase); D(c.MassKg); D(c.MolarMassKgPerMol); D(c.Moles); D(c.PartialVolumeCubicMetres); }
    foreach (var d in new[] { r.TotalMassKg, r.MassResidualKg, r.StoredInternalEnergyJoules, r.TemperatureKelvin, r.GasPressurePa, r.MechanicalPressurePa, r.VolumeCubicMetres, r.GasVolumeCubicMetres, r.LiquidVolumeCubicMetres, r.VacuumVolumeCubicMetres, r.GasMassKg, r.GasMoles, r.VolumeClosureResidualCubicMetres, r.GasPartialVolumeResidualCubicMetres, r.LiquidPartialVolumeResidualCubicMetres, r.IdealGasVolumeResidualCubicMetres, r.BulkKineticEnergyJoules }) D(d);
    sb.Append(r.SourceId).Append(r.Revision);
    var m = model.Describe(r);
    sb.Append(m.DisplayName).Append(m.BreathingStatus).Append(m.IsBreathable);
    foreach (var row in m.Constituents) sb.Append(row.MaterialId).Append(row.DisplayName).Append(row.MolePercentText).Append(row.MassTextKg).Append(row.PartialPressureText).Append(row.IsTrace);
    foreach (var w in m.Warnings) sb.Append(w);
    D(m.TotalPressurePa); D(m.GasMassKg); D(m.GasVolumeCubicMetres);
    portable.Append(sb, begun, sb.Length - begun).Append('\n');
    var col = m.WorldFluidColorAt(cell % 300, cell / 300); sb.Append((int)Math.Round(col.R * 255)).Append((int)Math.Round(col.G * 255)).Append((int)Math.Round(col.B * 255));
    sb.Append('\n');
    if (sb.Length > 1 << 22) { chain += Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(sb.ToString()))).Substring(0, 16); sb.Clear(); portableHash.AppendData(Encoding.UTF8.GetBytes(portable.ToString())); portable.Clear(); }
}
var final = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(chain + sb.ToString())));
portableHash.AppendData(Encoding.UTF8.GetBytes(portable.ToString()));
var portableFinal = Convert.ToHexString(portableHash.GetHashAndReset());
Console.WriteLine($"Reading portable {portableFinal} local-colour {final} ok {ok} rejected {bad}");
if (ok != 50631 || bad != 9369 || portableFinal != ExpectedPortable) { Console.Error.WriteLine("Reading equivalence failed: " + portableFinal); return 1; }
if (Environment.GetEnvironmentVariable("MA_READING_EQUIVALENCE_LOCAL_HASH") is string expectedLocal && final != expectedLocal)
{ Console.Error.WriteLine("Same-platform colour digest differs from its reference."); return 1; }
Console.WriteLine("PASS exact readings and non-colour presentations equal the original construction (60,000 cells)");
return ExtendedCases.Run();
