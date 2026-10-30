using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;

static partial class Proof
{
    private static void ChannelBytes(MixtureTextureScalars value, MixturePresentation rich, double x, double y)
    {
        foreach (var range in new[] {
            new MixtureWorldRendering.OpacityParameters(15, 200, 0),
            new MixtureWorldRendering.OpacityParameters(15, 200, .1),
            new MixtureWorldRendering.OpacityParameters(0, 500, .8),
            new MixtureWorldRendering.OpacityParameters(1, 2, 1),
            new MixtureWorldRendering.OpacityParameters(-200, 0, .01) })
        {
            var complete = MixtureWorldRendering.Sample(value, range);
            var originalRich = MixtureWorldRendering.Sample(rich, x, y, range);
            var opacity = MixtureWorldRendering.CompactOpacityByte(value, range);
            var hazard = MixtureWorldRendering.CompactHazardByte(value);
            Check(opacity == complete.Opacity && opacity == originalRich.Opacity, "compact pressure channel differs from original scalar/rich bytes");
            Check(hazard == complete.Hazard && hazard == originalRich.Hazard, "compact hazard channel differs from original scalar/rich bytes");
        }
    }
}
