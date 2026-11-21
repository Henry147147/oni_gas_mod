using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Capture actual restored buildings once at the end of a preset load.
        internal static object RestoredContainerCapacities()
        {
            return Components.BuildingCompletes.Items
                .Where(b => b != null && b.GetComponent<ContainerCapacity>() != null)
                .GroupBy(b => b.Def.PrefabID)
                .Select(group =>
                {
                    var capacities = ContainerCapacityPatch.For(group.Key);
                    var values = group.Select(b =>
                    {
                        var storage = b.GetComponent<Storage>();
                        var slider = b.GetComponent<IUserControlledCapacity>();
                        if (slider == null && group.Key == StorageTileConfig.ID) slider = b.GetSMI<StorageTile.Instance>();
                        var inlet = b.GetComponent<ConduitConsumer>();
                        if (storage.capacityKg != capacities.Capacity || (inlet != null && inlet.capacityKG != capacities.InletCapacity) ||
                            (slider != null && (slider.MaxCapacity != storage.capacityKg || slider.UserMaxCapacity > slider.MaxCapacity)))
                            throw new System.InvalidOperationException("Restored capacity differs for " + group.Key);
                        return slider?.UserMaxCapacity;
                    }).ToArray();
                    return new { Building = group.Key, Count = values.Length, CapacityKg = capacities.Capacity,
                        DeliveryLimitsKg = values.Where(v => v.HasValue).Distinct().OrderBy(v => v).ToArray() };
                }).ToArray();
        }

        private static GameObject? testDirt, testOxylite, testBin, testReservoir;
        private static void StorageOptions(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .1f, 300f));
            GameObject Drop(SimHashes element) => ElementLoader.FindElementByHash(element).substance.SpawnResource(
                Grid.CellToPosCCC(Cell(3, 1), Grid.SceneLayer.Ore), 100, 300, byte.MaxValue, 0, prevent_merge: true);
            if (Once(6, "spawn items"))
            {
                testDirt = Drop(SimHashes.ToxicSand);
                testOxylite = Drop(SimHashes.OxyRock);
            }
            if (Once(10, "build containers"))
            {
                testBin = Build(StorageLockerConfig.ID, Cell(2, 1));
                testReservoir = Build(GasReservoirConfig.ID, Cell(5, 1));
            }
            if (!Once(16, "options")) return;
            var settings = ModSettingsRuntime.Current;
            var bin = testBin!.GetComponent<Storage>();
            var gas = testReservoir!;
            Record("bin uses its own capacity", bin.capacityKg == 20000f * settings.Number("storage.capacity.StorageLocker"), bin.capacityKg.ToString());
            Record("gas reservoir inlet and storage agree", gas.GetComponent<ConduitConsumer>().capacityKG == gas.GetComponent<Storage>().capacityKg &&
                gas.GetComponent<Storage>().capacityKg == 1000f * settings.Number("storage.capacity.GasReservoir"), gas.GetComponent<Storage>().capacityKg.ToString());

            void CheckItem(GameObject item, bool shouldEmit, string label)
            {
                var primary = item.GetComponent<PrimaryElement>();
                var before = primary.Mass;
                var sublimates = item.GetComponent<Sublimates>();
                // Put an ordinary 200 ms update just at the emission threshold.
                // A blocked tick must preserve the pending mass as well.
                var pending = Traverse.Create(sublimates).Field<float>("sublimatedMass");
                pending.Value = sublimates.info.minSublimationAmount;
                sublimates.Sim200ms(.2f);
                Record(label, shouldEmit ? primary.Mass < before : primary.Mass == before && pending.Value == sublimates.info.minSublimationAmount, $"{before} → {primary.Mass} kg");
            }
            var dirt = testDirt!;
            var oxylite = testOxylite!;
            try
            {
                var binKey = SettingCatalog.CapacityPrefix + StorageLockerConfig.ID;
                var gasKey = SettingCatalog.CapacityPrefix + GasReservoirConfig.ID;
                var slider = bin.GetComponent<IUserControlledCapacity>();
                slider.UserMaxCapacity = bin.capacityKg;
                ModSettingsRuntime.SetPreference(binKey, 5.0, false);
                Record("an existing full-limit bin follows a capacity increase", bin.capacityKg == 100000 && slider.UserMaxCapacity == 100000,
                    $"physical {bin.capacityKg}, deliveries {slider.UserMaxCapacity}");
                var load = ElementLoader.FindElementByHash(SimHashes.Granite).substance.SpawnResource(
                    Grid.CellToPosCCC(Cell(3, 1), Grid.SceneLayer.Ore), 45000, 300, byte.MaxValue, 0, prevent_merge: true);
                bin.Store(load);
                slider.UserMaxCapacity = 1234;
                ModSettingsRuntime.SetPreference(binKey, 2.0, false);
                Record("a lower custom delivery limit survives resizing", bin.capacityKg == 40000 && slider.UserMaxCapacity == 1234,
                    $"physical {bin.capacityKg}, deliveries {slider.UserMaxCapacity}");
                Record("reducing capacity preserves overfull contents", bin.MassStored() == 45000, $"stored {bin.MassStored()}");
                bin.Drop(load);
                ModSettingsRuntime.SetPreference(gasKey, 5.0, false);
                Record("existing reservoir and inlet resize together", gas.GetComponent<Storage>().capacityKg == 5000 && gas.GetComponent<ConduitConsumer>().capacityKG == 5000,
                    $"physical {gas.GetComponent<Storage>().capacityKg}, inlet {gas.GetComponent<ConduitConsumer>().capacityKG}");
                ModSettingsRuntime.SetPreference(SettingCatalog.KeepDebris, true, false);
                ModSettingsRuntime.SetPreference(SettingCatalog.KeepStored, false, false);
                CheckItem(dirt, false, "loose polluted dirt is protected");
                CheckItem(oxylite, true, "loose oxylite emits");
                bin.Store(dirt);
                CheckItem(dirt, true, "unprotected stored dirt emits");
                bin.Store(oxylite);
                ModSettingsRuntime.SetPreference(SettingCatalog.KeepDebris, false, false);
                ModSettingsRuntime.SetPreference(SettingCatalog.KeepStored, true, false);
                CheckItem(dirt, false, "stored polluted dirt is protected");
                CheckItem(oxylite, false, "stored oxylite is protected");
                bin.Drop(dirt);
                CheckItem(dirt, true, "dropped dirt resumes emission");
            }
            finally
            {
                foreach (var id in new[] { StorageLockerConfig.ID, GasReservoirConfig.ID })
                {
                    var key = SettingCatalog.CapacityPrefix + id;
                    ModSettingsRuntime.SetPreference(key, settings.Number(key), false);
                }
                ModSettingsRuntime.SetPreference(SettingCatalog.KeepDebris, settings.Toggle(SettingCatalog.KeepDebris), false);
                ModSettingsRuntime.SetPreference(SettingCatalog.KeepStored, settings.Toggle(SettingCatalog.KeepStored), false);
            }
            Finish();
        }
    }
}
