using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Oni;
using MixedAtmospheres.Core;

static partial class Proof
{
    // Literal clean-fef29c2 rich path, not a digest generated from the scalar
    // analyzer. Runtime value/error comparison also uses that unchanged rich
    // implementation; ReadingEquivalenceTests separately checks its original
    // public-property/rejection golden values across platforms.
    private const string OriginalConstructor = "c95ebcac2ddfb8fb1fb1407fe68bb928ebbb0910b7b6478777517fa6cd7c56c1";
    private const string OriginalColorLoop = "9d2d77be39c0333d52e7d61342277c68f68dc5feab617e54dd8599b5d6727c2a";

    private static string Resource(string name)
    {
        using var stream = typeof(Proof).Assembly.GetManifestResourceStream(name)
            ?? throw new InvalidOperationException("Missing original-path source fixture " + name);
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd().Replace("\r\n", "\n", StringComparison.Ordinal);
    }

    private static string Between(string source, string begin, string end)
    {
        var start = source.IndexOf(begin, StringComparison.Ordinal);
        if (start < 0)
            throw new InvalidOperationException("Original-path fixture start missing.");
        var finish = source.IndexOf(end, start, StringComparison.Ordinal);
        if (finish < 0)
            throw new InvalidOperationException("Original-path fixture end missing.");
        return source.Substring(start, finish - start);
    }

    private static void VerifyOriginal(string source, string expected)
    {
        var actual = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(source))).ToLowerInvariant();
        if (actual != expected)
            throw new InvalidOperationException("Independent original rich oracle changed.");
    }

    private static void OriginalOracleControls()
    {
        var constructor = Between(Resource("OriginalRichConstructor.cs"),
            "        public MixtureThermodynamicReading(IEnumerable", "\n        internal double GasPartialPressure");
        var color = Between(Resource("OriginalRichPresentation.cs"),
            "        private MixtureSrgbColor Blend(double x", "\n    }\n\n    /// <summary>One source");
        VerifyOriginal(constructor, OriginalConstructor);
        VerifyOriginal(color, OriginalColorLoop);
        Check(Error(() => VerifyOriginal(constructor.Replace("FluidChecks.Nonnegative(totalMassKg", "FluidChecks.Nonnegative(storedInternalEnergyJoules", StringComparison.Ordinal), OriginalConstructor)).Contains("Independent original rich oracle changed"), "original constructor mutation refused");
        Check(Error(() => VerifyOriginal(color.Replace("species.GasFraction", "species.LiquidFraction", StringComparison.Ordinal), OriginalColorLoop)).Contains("Independent original rich oracle changed"), "original color mutation refused");
        Console.WriteLine("PASS unchanged original rich constructor/color oracle and independent mutation negatives; portable original-value goldens remain in ReadingEquivalenceTests");
    }

    private static void OptionalRangeControls(MixturePresentationModel model, List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases)
    {
        var calls = 0;
        Check(MixtureWorldRendering.TryBindVisibleRange(() => null, _ => { ++calls; throw new Exception("binding missing method"); }) == null && calls == 0, "missing optional range falls back without binding");
        var wrong = typeof(Proof).GetMethod(nameof(WrongRange), BindingFlags.Static | BindingFlags.NonPublic)!;
        Check(MixtureWorldRendering.TryBindVisibleRange(() => wrong) == null, "changed optional range signature falls back");
        Check(MixtureWorldRendering.TryBindVisibleRange(() => throw new MissingMethodException("fixture")) == null, "missing lookup exception falls back");
        var getter = typeof(PropertyTextures).GetMethod("GetVisibleCellRange", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)!;
        Check(getter != null && MixtureWorldRendering.TryBindVisibleRange(() => getter) != null, "inspected supported getter binds without invoking it");
        Check(MixtureWorldRendering.TryBindVisibleRange(() => getter, _ => throw new ArgumentException("fixture binding failure")) == null, "incompatible optional delegate falls back");
        var input = cases[0];
        var frame = Runtime(input.Table, input.Cell, model, (_, _) => { });
        MixtureWorldRendering.VisibleRange unavailable = (PropertyTextures owner, out int x0, out int y0, out int x1, out int y1) => throw new NullReferenceException("camera is not ready");
        Check(!MixtureWorldRendering.TryPrepareOptionalTextures(unavailable, null!, frame), "optional camera/reset lookup failure uses original renderer");
        Check(!MixtureWorldRendering.TryPrepareOptionalTextures(null, null!, frame), "unavailable optional delegate uses original renderer");
        MixturePresentationRuntime.ResetProviders();
        Console.WriteLine("PASS optional visible-range missing/signature/binding fallback controls");
    }

    private static void WrongRange()
    {
    }
}
