using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;
static partial class Proof
{
    static void PairedControls(MixturePresentationModel model, List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases)
    {
        var first = cases.First(t => t.Cell.Constituents > 0);
        var frame = Runtime(first.Table, first.Cell, model, (_, _) => { });
        var range = new MixtureWorldRendering.OpacityParameters(15, 200, .1);
        var plan = new MixtureWorldRendering.CompactTexturePlan(frame, range, true);
        Check(ReferenceEquals(plan.Frame, frame) && plan.Admitted && plan.Parameters.Minimum == 15, "immutable worker plan captures frame/admission/opacity");
        foreach (var field in typeof(MixtureWorldRendering.CompactTexturePlan).GetFields(System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic))
            Check(field.IsInitOnly, "worker plan field immutable");
        var a = new MixtureWorldRendering.GasVisualSample(11, 22, 33, 255, 44, 55, 12345.67);
        foreach (var c in new[] { 0, 1, 2 })
        {
            var bytes = c == 0 ? new byte[] { 11, 22, 33, 255 } : new byte[] { c == 1 ? (byte)44 : (byte)55 };
            MixtureWorldRendering.VerifyCompactProofBytes(c, a, a, bytes);
            Check(true, "paired bytes accepted");
            bytes[0] ^= 1;
            Check(Error(() => MixtureWorldRendering.VerifyCompactProofBytes(c, a, a, bytes)).Contains("Private compact texture bytes differ"), "paired destination negative");
        }
        var b = new MixtureWorldRendering.GasVisualSample(11, 22, 33, 255, 44, 55, 12345.68);
        Check(Error(() => MixtureWorldRendering.VerifyCompactProofBytes(0, a, b, new byte[] { 11, 22, 33, 255 })).Contains("Private compact texture bytes differ"), "paired pressure negative");
        Check(Error(() => MixtureWorldRendering.VerifyCompactProofBytes(3, a, a, new byte[] { 11 })).Contains("Private compact texture bytes differ"), "paired channel negative");
        Check(Error(() => MixtureWorldRendering.VerifyCompactProofBytes(0, a, a, new byte[] { 11 })).Contains("Private compact texture bytes differ"), "paired destination extent negative");
        MixturePresentationRuntime.ResetProviders();
        Console.WriteLine("PASS immutable worker texture plan and paired CPU destination-byte controls, six rejection negatives");
    }
}
