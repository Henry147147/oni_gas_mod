using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static GameObject? testDirt, testOxylite;
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
            if (!Once(10, "options")) return;
            var settings = ModSettingsRuntime.Current;
            var bin = Build(StorageLockerConfig.ID, Cell(2, 1)).GetComponent<Storage>();
            var gas = Build(GasReservoirConfig.ID, Cell(5, 1));
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
                ModSettingsRuntime.SetPreference(SettingCatalog.KeepDebris, settings.Toggle(SettingCatalog.KeepDebris), false);
                ModSettingsRuntime.SetPreference(SettingCatalog.KeepStored, settings.Toggle(SettingCatalog.KeepStored), false);
            }
            Finish();
        }
    }
}
