using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static Dictionary<int, double> airborneExpected = new Dictionary<int, double>();
        private static Dictionary<int, double> airborneDebrisBefore = new Dictionary<int, double>();

        private static void ClusterAddStoredFluids(Dictionary<int, double> result, Storage storage)
        {
            foreach (var item in storage.items.Where(item => item != null))
            {
                var element = item.GetComponent<PrimaryElement>();
                if (element != null && element.Mass > 0 && (element.Element.IsGas || element.Element.IsLiquid))
                    ClusterAdd(result, element.Element.idx, element.Mass);
            }
        }
        private static Dictionary<int, double> ClusterDebrisFluids()
        {
            var result = new Dictionary<int, double>();
            foreach (var storage in Resources.FindObjectsOfTypeAll<Storage>().Where(s => s.isSpawned))
                if (storage.GetComponent<KPrefabID>()?.PrefabTag.Name == "DebrisPayload")
                    ClusterAddStoredFluids(result, storage);
            return result;
        }
        private static void SpacedOutRocketAbandon(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "request actual airborne self destruct"))
            {
                if (clusterFlightCraft == null || clusterFlightCraft.Status != Clustercraft.CraftStatus.InFlight || clusterRocketWorld == null)
                    throw new InvalidOperationException("Run the actual rocket flight control before airborne recovery.");
                _ = ColonySaveBarrier.FlushInputs();
                retiredRocket = new ColonyWorldRegion(clusterRocketWorld.id, clusterRocketWorld.WorldOffset.x, clusterRocketWorld.WorldOffset.y,
                    clusterRocketWorld.WorldSize.x, clusterRocketWorld.WorldSize.y, true);
                airborneExpected = ClusterWorldFluid(clusterRocketWorld.id, true);
                foreach (var storage in clusterFlightCraft.ModuleInterface.GetParts().SelectMany(module => module.GetComponents<Storage>()).Distinct())
                    ClusterAddStoredFluids(airborneExpected, storage);
                airborneDebrisBefore = ClusterDebrisFluids();
                Record("airborne recovery includes both phases and native fuel", airborneExpected.Count >= 4 && airborneExpected.Values.All(m => m > 0),
                    string.Join("; ", airborneExpected.Select(p => $"{ElementLoader.elements[p.Key].id}: {p.Value:R} kg")));
                // The confirmed player button emits this event. Let the
                // native self-destruct monitor, animation, recovery and grid
                // cleanup run on their ordinary callbacks and clocks.
                clusterFlightCraft.ModuleInterface.gameObject.Trigger((int)GameHashes.RocketSelfDestructRequested);
            }
            if (frame < 4) return;
            if (ColonyClusterRetirement.Pending || ClusterManager.Instance.GetWorld(retiredRocket.Id) != null)
            {
                if (frame < 100) return;
                Record("airborne retirement completed", false, $"pending {ColonyClusterRetirement.Pending}, world {retiredRocket.Id}");
                Finish(); return;
            }
            var after = ClusterDebrisFluids();
            foreach (var expected in airborneExpected)
            {
                after.TryGetValue(expected.Key, out var now);
                airborneDebrisBefore.TryGetValue(expected.Key, out var before);
                Record("airborne debris recovers " + ElementLoader.elements[expected.Key].id, Math.Abs(now - before - expected.Value) < 1e-6,
                    $"recovered {now - before:R} kg, expected {expected.Value:R} kg");
            }
            Record("airborne interior cells are no longer owned", Enumerable.Range(0, retiredRocket.Width * retiredRocket.Height)
                .All(i => !ColonyOwner.OwnsFluidCell(Grid.XYToCell(retiredRocket.X + i % retiredRocket.Width, retiredRocket.Y + i / retiredRocket.Width))),
                $"world {retiredRocket.Id}, completed retirements {ColonyClusterRetirement.Completed}");
            Record("airborne retirement keeps closed ledgers", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 &&
                status.MaximumRelativeEnergyError <= 1e-12, $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
            Record("airborne retirement keeps exact pipe accounts", ColonyConduits.Audit(out var detail), detail);
            Finish();
        }
    }
}
