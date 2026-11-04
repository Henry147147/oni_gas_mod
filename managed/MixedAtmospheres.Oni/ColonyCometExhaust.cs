using System;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Comet exhaust (docs/colony-owner.md#open-sky). A comet emits its exhaust
    // from Comet.Sim33ms through SimMessages.EmitMass with no callback; no
    // other EmitMass call runs inside that method. While it runs, an owned
    // emission is marked as comet exhaust, and with a game activity profile
    // the owner sinks it at once when it lands in vacuum open to the sky
    // (user decision 2026-11-04). Other emitters are not marked.
    internal static class ColonyCometExhaust
    {
        [ThreadStatic] private static int depth;
        internal static bool Emitting => depth > 0;
        internal static void Enter() => ++depth;
        internal static void Leave() { if (depth > 0) --depth; }
    }

    [HarmonyPatch(typeof(Comet), nameof(Comet.Sim33ms))]
    internal static class ColonyCometExhaustPatch
    {
        private static void Prefix() => ColonyCometExhaust.Enter();
        private static void Postfix() => ColonyCometExhaust.Leave();
        private static Exception? Finalizer(Exception? __exception) { if (__exception != null) ColonyCometExhaust.Leave(); return __exception; }
    }
}
