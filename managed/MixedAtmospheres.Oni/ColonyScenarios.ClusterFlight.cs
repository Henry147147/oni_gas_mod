using System;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static Clustercraft? clusterFlightCraft;
        private static AxialI clusterFlightOrigin, clusterFlightDestination;

        private static void SpacedOutRocketFlight(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "launch real cluster rocket"))
            {
                if (clusterEngine == null || clusterCapsule == null || clusterRocketWorld == null)
                    throw new InvalidOperationException("Run rocket creation before the flight control.");
                _ = ColonySaveBarrier.FlushInputs();
                var mixture = WorldSpecies(clusterRocketWorld);
                recorded["flight oxygen"] = mixture.Oxygen;
                recorded["flight carbon"] = mixture.Carbon;
                recorded["flight owner frame"] = mixture.Frame;
                var modules = clusterEngine.GetComponent<RocketModuleCluster>().CraftInterface;
                clusterFlightCraft = modules.GetComponent<Clustercraft>();
                clusterFlightOrigin = clusterFlightCraft.Location;
                var targets = AxialI.DIRECTIONS.Select(direction => clusterFlightOrigin + direction)
                    .Where(cell => ClusterGrid.Instance.IsValidCell(cell) && ClusterGrid.Instance.GetAsteroidAtCell(cell) == null).ToArray();
                if (targets.Length == 0)
                    throw new InvalidOperationException("The generated rocket site has no adjacent space destination.");
                clusterFlightDestination = targets[0];
                var fuel = clusterEngine.GetComponent<Storage>();
                fuel.AddGasChunk(SimHashes.CarbonDioxide, 100, 300, byte.MaxValue, 0, false, false);
                AccessTools.Method(typeof(ClusterTraveler), "ForceRevealLocation")
                    .Invoke(clusterFlightCraft.GetComponent<ClusterTraveler>(), new object[] { clusterFlightDestination });
                modules.GetClusterDestinationSelector().SetDestination(clusterFlightDestination);
                // Use the normal launch command with the game's instant-build
                // fixture switch, as for module construction. Actual engine
                // animation, world-parent changes, fuel and flight callbacks
                // run afterwards with the switch restored.
                var previous = DebugHandler.InstantBuildMode;
                DebugHandler.InstantBuildMode = true;
                try { modules.TriggerLaunch(false); }
                finally { DebugHandler.InstantBuildMode = previous; }
            }
            if (!recorded.ContainsKey("observed actual flight"))
            {
                if (clusterFlightCraft!.Status != Clustercraft.CraftStatus.InFlight ||
                    clusterFlightCraft.Location != clusterFlightDestination)
                {
                    if (frame < 240) return;
                    Record("rocket reached actual flight", false,
                        $"status {clusterFlightCraft.Status}, location {clusterFlightCraft.Location}, destination {clusterFlightDestination}");
                    Finish(); return;
                }
                recorded["observed actual flight"] = frame;
            }
            if (frame < recorded["observed actual flight"] + 8) return;
            var proof = WorldSpecies(clusterRocketWorld!);
            Record("rocket travelled to another cluster hex", clusterFlightCraft!.Status == Clustercraft.CraftStatus.InFlight &&
                clusterFlightCraft.Location == clusterFlightDestination && clusterFlightDestination != clusterFlightOrigin,
                $"{clusterFlightOrigin} -> {clusterFlightCraft.Location}, status {clusterFlightCraft.Status}");
            Record("flight retains both cabin constituents", Math.Abs(proof.Oxygen - recorded["flight oxygen"]) < 1e-6 &&
                Math.Abs(proof.Carbon - recorded["flight carbon"]) < 1e-6 && proof.Frame > recorded["flight owner frame"],
                $"oxygen {proof.Oxygen:R}/{recorded["flight oxygen"]:R} kg, carbon dioxide {proof.Carbon:R}/{recorded["flight carbon"]:R} kg");
            Record("flight keeps closed world ledgers", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 &&
                status.MaximumRelativeEnergyError <= 1e-12, $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
            Record("flight keeps exact pipe accounts", ColonyConduits.Audit(out var detail), detail);
            Finish();
        }
    }
}
