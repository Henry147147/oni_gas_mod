using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static int clusterCargoNextX, clusterCargoY, clusterCargoInput, clusterCargoOutput;
        private static GameObject? clusterCargoModule, clusterCargoLoader, clusterCargoUnloader;
        private static double ClusterStoredMass(Storage storage, SimHashes element)
            => storage.items.Where(item => item != null).Select(item => item.GetComponent<PrimaryElement>())
                .Where(item => item != null && item.ElementID == element).Sum(item => (double)item.Mass);

        private static void SpacedOutCargo(int frame, OniNativeColony.Status status, bool gas)
        {
            var first = gas ? SimHashes.Oxygen : SimHashes.Water;
            var second = gas ? SimHashes.CarbonDioxide : SimHashes.DirtyWater;
            var loaderId = gas ? ModularLaunchpadPortGasConfig.ID : ModularLaunchpadPortLiquidConfig.ID;
            var unloaderId = gas ? ModularLaunchpadPortGasUnloaderConfig.ID : ModularLaunchpadPortLiquidUnloaderConfig.ID;
            var loaderDef = Assets.GetBuildingDef(loaderId);
            var unloaderDef = Assets.GetBuildingDef(unloaderId);
            var flow = gas ? Game.Instance.gasConduitFlow : Game.Instance.liquidConduitFlow;
            if (Once(0, "cargo loading site"))
            {
                if (clusterLaunchPad == null || clusterEngine == null || clusterRocketWorld == null)
                    throw new InvalidOperationException("Run rocket construction before cargo loading.");
                var padCell = Grid.PosToCell(clusterLaunchPad);
                Grid.CellToXY(padCell, out var px, out clusterCargoY);
                if (gas || clusterCargoNextX == 0)
                    clusterCargoNextX = px + clusterLaunchPad.GetComponent<OccupyArea>().OccupiedCellsOffsets.Max(o => o.x) + 1;
                // Power helpers use this scenario's actual station site,
                // rather than an unrelated arena beside the printing pod.
                x0 = clusterCargoNextX; y0 = clusterCargoY;
                var width = loaderDef.WidthInCells + unloaderDef.WidthInCells;
                for (var x = clusterCargoNextX; x < clusterCargoNextX + width; ++x)
                    for (var y = clusterCargoY - 1; y < clusterCargoY + 7; ++y)
                    {
                        var cell = Grid.XYToCell(x, y);
                        if (!Grid.IsValidCell(cell) || Grid.WorldIdx[cell] != Grid.WorldIdx[padCell])
                            throw new InvalidOperationException("Cargo fixture leaves its actual launch-pad world.");
                    }
                FloorArenas.Clear(clusterCargoNextX, clusterCargoY, width, 7, out _);
                for (var x = clusterCargoNextX; x < clusterCargoNextX + width; ++x)
                    for (var y = clusterCargoY - 1; y < clusterCargoY + 7; ++y)
                        SimMessages.ReplaceElement(Grid.XYToCell(x, y), y < clusterCargoY ? SimHashes.Unobtanium : SimHashes.Vacuum,
                            CellEventLogger.Instance.SandBoxTool, y < clusterCargoY ? 20000 : 0, y < clusterCargoY ? 300 : 0);
            }
            if (Once(4, "real cargo module and stations"))
            {
                clusterCargoModule = AddRocketModule(clusterEngine!, gas ? GasCargoBaySmallConfig.ID : LiquidCargoBaySmallConfig.ID, false);
                clusterCargoModule.GetComponent<TreeFilterable>().UpdateFilters(new HashSet<Tag> { first.CreateTag(), second.CreateTag() });
                var loaderOffset = loaderDef.BuildingComplete.GetComponent<OccupyArea>().OccupiedCellsOffsets.Min(o => o.x);
                var unloaderOffset = unloaderDef.BuildingComplete.GetComponent<OccupyArea>().OccupiedCellsOffsets.Min(o => o.x);
                clusterCargoLoader = Build(loaderId, Grid.XYToCell(clusterCargoNextX - loaderOffset, clusterCargoY));
                clusterCargoNextX += loaderDef.WidthInCells;
                clusterCargoUnloader = Build(unloaderId, Grid.XYToCell(clusterCargoNextX - unloaderOffset, clusterCargoY));
                clusterCargoNextX += unloaderDef.WidthInCells;
                // The normal unloader filter keeps the cargo aboard until its
                // composition has been observed; then the player filter opens.
                clusterCargoUnloader.GetComponent<TreeFilterable>().UpdateFilters(new HashSet<Tag>());
                clusterCargoInput = Grid.OffsetCell(Grid.PosToCell(clusterCargoLoader), loaderDef.UtilityInputOffset);
                clusterCargoOutput = Grid.OffsetCell(Grid.PosToCell(clusterCargoUnloader), unloaderDef.UtilityOutputOffset);
                Pipe(gas ? Game.Instance.gasConduitSystem : Game.Instance.liquidConduitSystem,
                    gas ? GasConduitConfig.ID : LiquidConduitConfig.ID,
                    new List<int> { clusterCargoInput }, new List<int> { clusterCargoOutput });
            }
            if (frame >= 8 && !recorded.ContainsKey("seed cargo intake"))
            {
                if (!flow.HasConduit(clusterCargoInput) || !flow.HasConduit(clusterCargoOutput))
                {
                    if (frame < 30) return;
                    Record("cargo endpoint pipes became ready", false, "actual input/output pipe missing");
                    Finish(); return;
                }
                if (ColonyConduits.ReadSynced(flow, clusterCargoInput).Mass != 0 ||
                    ColonyConduits.ReadSynced(flow, clusterCargoOutput).Mass != 0 || clusterCargoModule!.GetComponent<Storage>().MassStored() != 0)
                    throw new InvalidOperationException("Cargo route was not empty before its source.");
                var a = ColonyConduits.Seed(flow, clusterCargoInput, first, .75f, 300, byte.MaxValue, 0);
                var b = ColonyConduits.Seed(flow, clusterCargoInput, second, .25f, 300, byte.MaxValue, 0);
                if (a != .75f || b != .25f) throw new InvalidOperationException("Cargo mixture source was refused.");
                recorded["seed cargo intake"] = frame;
            }
            if (recorded.TryGetValue("seed cargo intake", out var seeded) && frame >= seeded + 16 && !recorded.ContainsKey("observed loaded cargo"))
            {
                var cargo = clusterCargoModule!.GetComponent<Storage>();
                var a = ClusterStoredMass(cargo, first); var b = ClusterStoredMass(cargo, second);
                Record("actual cargo bay holds both constituents", Math.Abs(a - .75) < 1e-6 && Math.Abs(b - .25) < 1e-6,
                    $"first {a:R} kg, second {b:R} kg; loader {MachineState(clusterCargoLoader)}");
                Record("cargo intake and loader drained", ColonyConduits.ReadSynced(flow, clusterCargoInput).Mass < 1e-6 &&
                    clusterCargoLoader!.GetComponent<Storage>().MassStored() < 1e-6, "input and actual loader storage empty");
                clusterCargoUnloader!.GetComponent<TreeFilterable>().UpdateFilters(new HashSet<Tag> { first.CreateTag(), second.CreateTag() });
                recorded["observed loaded cargo"] = frame;
            }
            if (recorded.TryGetValue("observed loaded cargo", out var loaded) && frame >= loaded + 16)
            {
                var output = ColonyConduits.ReadSynced(flow, clusterCargoOutput);
                var a = ClusterPipeMass(output, first); var b = ClusterPipeMass(output, second);
                Record("cargo unloads both constituents", Math.Abs(a - .75) < 1e-6 && Math.Abs(b - .25) < 1e-6,
                    $"first {a:R} kg, second {b:R} kg; unloader {MachineState(clusterCargoUnloader)}");
                Record("cargo and unloader leave no duplicate inventory", clusterCargoModule!.GetComponent<Storage>().MassStored() < 1e-6 &&
                    clusterCargoUnloader!.GetComponent<Storage>().MassStored() < 1e-6, "cargo and actual unloader storage empty");
                Record("cargo keeps closed world ledgers", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 &&
                    status.MaximumRelativeEnergyError <= 1e-12, $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
                Record("cargo keeps exact pipe accounts", ColonyConduits.Audit(out var detail), detail);
                if (Current.Checks.All(c => c.Passed))
                    AccessTools.Method(typeof(ReorderableBuilding), "RemoveModule").Invoke(clusterCargoModule.GetComponent<ReorderableBuilding>(), null);
                Finish();
            }
        }
    }
}
