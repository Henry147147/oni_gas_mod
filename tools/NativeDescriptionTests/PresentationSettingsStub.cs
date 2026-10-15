namespace MixedAtmospheres.Oni;

// This standalone contract runner never enables private game fixtures. The
// real opt-in configuration and Unity object creation are exercised in-game.
internal static class ColonyRuntimeValidation
{
    internal sealed class Configuration { internal string Mode = ""; }
    internal static Configuration? Settings => null;
}
