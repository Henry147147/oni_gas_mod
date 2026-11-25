using System;

namespace MixedAtmospheres.Oni
{
    // The load boundary's finalizers hand an exception on through
    // ColonyFault.Pass (which, in the mod, also logs its first trace). These
    // tests link the boundary alone, so the pass is the identity here.
    internal static class ColonyFault
    {
        internal static Exception? Pass(Exception? error) => error;
    }
}
