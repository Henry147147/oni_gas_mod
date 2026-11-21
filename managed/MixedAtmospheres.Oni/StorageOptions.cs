using System.Collections.Generic;
using HarmonyLib;
using KSerialization;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    [HarmonyPatch(typeof(Sublimates), nameof(Sublimates.Sim200ms))]
    internal static class DebrisSublimationPatch
    {
        private static bool loose, stored;
        internal static void Configure(SettingValues settings)
        {
            loose = settings.Toggle(SettingCatalog.KeepDebris);
            stored = settings.Toggle(SettingCatalog.KeepStored);
        }
        private static bool Prefix(Pickupable ___pickupable, PrimaryElement ___primaryElement)
        {
            if ((!loose && !stored) || ___pickupable == null) return true;
            return ___pickupable.storage != null ? !stored : !loose || ___primaryElement.ElementID == SimHashes.OxyRock;
        }
    }

    // Save the previous physical capacity separately from ONI's delivery slider.
    // On older saves the original building capacity is the migration baseline.
    [SerializationConfig(MemberSerialization.OptIn)]
    internal sealed class ContainerCapacity : KMonoBehaviour
    {
        [Serialize] private float lastCapacity;

        protected override void OnSpawn()
        {
            base.OnSpawn();
            Apply();
        }

        internal void Apply()
        {
            var id = GetComponent<Building>().Def.PrefabID;
            var capacities = ContainerCapacityPatch.For(id);
            var storage = GetComponent<Storage>();
            var previous = lastCapacity > 0 ? lastCapacity : capacities.BaseCapacity;
            var maximum = capacities.Capacity;
            capacities.ApplyPhysical(gameObject);
            if (previous != maximum || (lastCapacity == 0 && GetComponent<CargoBayCluster>() != null))
            {
                var slider = GetComponent<IUserControlledCapacity>();
                if (slider == null && id == StorageTileConfig.ID)
                    slider = this.GetSMI<StorageTile.Instance>();
                if (slider != null)
                {
                    var limit = slider.UserMaxCapacity;
                    var desired = limit == previous ? maximum : Mathf.Min(limit, maximum);
                    // Most getters clamp to the physical capacity. Store that clamp
                    // on a decrease so an older hidden limit cannot return later.
                    if (limit != desired || (previous > maximum && limit == maximum)) slider.UserMaxCapacity = desired;
                }
                storage.Trigger((int)GameHashes.OnStorageChange, storage.gameObject);
            }
            lastCapacity = maximum;
        }
    }

    [HarmonyPatch(typeof(Assets), nameof(Assets.AddBuildingDef))]
    internal static class ContainerCapacityPatch
    {
        internal sealed class Capacities
        {
            internal readonly float BaseCapacity;
            private readonly float inletCapacity;
            private readonly StorageTile.Def? tile;
            private readonly float tileCapacity;
            internal readonly GameObject Prefab;
            internal float Factor;
            internal float Capacity => BaseCapacity * Factor;
            internal float InletCapacity => inletCapacity * Factor;

            internal Capacities(BuildingDef def)
            {
                Prefab = def.BuildingComplete;
                BaseCapacity = Prefab.GetComponent<Storage>().capacityKg;
                inletCapacity = Prefab.GetComponent<ConduitConsumer>()?.capacityKG ?? 0;
                if (def.PrefabID == StorageTileConfig.ID)
                {
                    tile = Prefab.AddOrGetDef<StorageTile.Def>();
                    tileCapacity = tile.MaxCapacity;
                }
            }

            internal void SetFactor(float factor)
            {
                Factor = factor;
                ApplyPhysical(Prefab);
                if (tile != null) tile.MaxCapacity = tileCapacity * Factor;
            }

            internal void ApplyPhysical(GameObject building)
            {
                building.GetComponent<Storage>().capacityKg = Capacity;
                var inlet = building.GetComponent<ConduitConsumer>();
                if (inlet != null) inlet.capacityKG = InletCapacity;
            }
        }

        private static readonly Dictionary<string, Capacities> registered = new Dictionary<string, Capacities>();
        internal static Capacities For(string id) => registered[id];

        private static void Prefix(BuildingDef def)
        {
            var key = SettingCatalog.CapacityPrefix + def.PrefabID;
            if (!SettingCatalog.TryGet(key, out _)) return;
            if (!registered.TryGetValue(def.PrefabID, out var capacities) || capacities.Prefab != def.BuildingComplete)
                registered[def.PrefabID] = capacities = new Capacities(def);
            capacities.SetFactor((float)ModSettingsRuntime.Current.Number(key));
            capacities.Prefab.AddOrGet<ContainerCapacity>();
        }

        internal static void Configure(SettingValues settings)
        {
            foreach (var pair in registered)
            {
                var factor = (float)settings.Number(SettingCatalog.CapacityPrefix + pair.Key);
                if (factor == pair.Value.Factor || pair.Value.Prefab == null) continue;
                pair.Value.SetFactor(factor);
                if (Game.Instance == null) continue;
                foreach (var building in Components.BuildingCompletes.Items)
                    if (building != null && building.Def.PrefabID == pair.Key)
                        building.GetComponent<ContainerCapacity>()?.Apply();
            }
        }
    }
}
