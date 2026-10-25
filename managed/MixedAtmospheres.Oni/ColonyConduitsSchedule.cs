using System;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Settle pending topology without advancing either conduit flow clock.
    // ForceRebuildNetworks only marks the manager dirty; Update performs the
    // synchronous rebuild and its normal registration callbacks.
    internal static class ColonyConduitsSchedule
    {
        private static readonly FieldInfo Manager = AccessTools.Field(typeof(ConduitFlow), "networkMgr");

        internal static int Flush(ConduitFlow flow)
        {
            var manager = Manager?.GetValue(flow) ?? throw new NotSupportedException("Missing conduit network manager.");
            var dirty = AccessTools.Property(manager.GetType(), "IsDirty") ??
                throw new NotSupportedException("Missing conduit topology readiness query.");
            var update = AccessTools.Method(manager.GetType(), "Update", Type.EmptyTypes) ??
                throw new NotSupportedException("Missing synchronous conduit topology update.");
            var updates = 0;
            while ((bool)dirty.GetValue(manager))
            {
                if (updates == 8) throw new InvalidOperationException("Conduit topology did not settle before capture or continuation.");
                update.Invoke(manager, null);
                ++updates;
            }
            return updates;
        }

        internal static int FlushTopology() => Flush(Game.Instance.gasConduitFlow) + Flush(Game.Instance.liquidConduitFlow);
    }
}
