using System;

namespace MixedAtmospheres.Oni
{
    // This fixture links rendering finalizers without the game's fault owner.
    internal static class ColonyFault
    {
        internal static Exception? Pass(Exception? error) => error;
    }
}
