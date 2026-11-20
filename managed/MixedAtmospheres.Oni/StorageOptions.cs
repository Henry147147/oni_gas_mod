using HarmonyLib;

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

    // A rocket saves its delivery limit independently of its physical capacity.
    // Clamp a saved raised limit when starting with a lower multiplier.
    [HarmonyPatch(typeof(CargoBayCluster), "OnSpawn")]
    internal static class CargoBayLimitPatch
    {
        private static void Prefix(CargoBayCluster __instance, ref float ___userMaxCapacity)
        {
            var maximum = __instance.storage.capacityKg;
            if (___userMaxCapacity > maximum && SettingCatalog.TryGet(SettingCatalog.CapacityPrefix + __instance.PrefabID().Name, out _))
                ___userMaxCapacity = maximum;
        }
    }

    // Building registration runs after ONI finishes the prefab, before a new
    // building or a restored instance copies it. Contents and player-set
    // delivery limits are serialized on instances, never on this template.
    [HarmonyPatch(typeof(Assets), nameof(Assets.AddBuildingDef))]
    internal static class ContainerCapacityPatch
    {
        private static void Prefix(BuildingDef def)
        {
            var key = SettingCatalog.CapacityPrefix + def.PrefabID;
            if (!SettingCatalog.TryGet(key, out _)) return;
            var factor = (float)ModSettingsRuntime.Current.Number(key);
            if (factor == 1f) return;
            var prefab = def.BuildingComplete;
            prefab.GetComponent<Storage>().capacityKg *= factor;
            var inlet = prefab.GetComponent<ConduitConsumer>();
            if (inlet != null) inlet.capacityKG *= factor;
            if (def.PrefabID == StorageTileConfig.ID) prefab.AddOrGetDef<StorageTile.Def>().MaxCapacity *= factor;
        }
    }
}
