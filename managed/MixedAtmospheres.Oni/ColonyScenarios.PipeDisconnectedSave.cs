using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static void MixedPipeDisconnectedSave(int frame)
        {
            var flow = Game.Instance.liquidConduitFlow;
            var cell = Cell(3, 3);
            var removedCell = Cell(5, 3);
            if (ColonyRuntimeValidation.Settings?.Mode == "colony-owner-load")
            {
                if (Once(2, "check the loaded broken pipe and repair it"))
                {
                    var building = Grid.Objects[cell, (int)ObjectLayer.LiquidConduit];
                    Record("pipe reloads disconnected", building.GetComponent<Conduit>().IsDisconnected() && !flow.HasConduit(cell),
                        $"Live {flow.HasConduit(cell)}");
                    Record("disconnected mixture reloads", HasDisconnectedFixtureMixture(flow, cell), "3 kg water and 7 kg polluted water");
                    Record("removed pipe was rehomed outside the conduit", ColonyConduits.TryRead(flow, removedCell, out var removed) && removed.Count == 0,
                        $"Removed pipe contents {removed.Mass:R} kg");
                    var hp = building.GetComponent<BuildingHP>();
                    hp.Repair(hp.MaxHitPoints);
                }
                if (Once(8, "check the repaired pipe"))
                {
                    Record("repaired pipe rejoins with its mixture", flow.HasConduit(cell) && HasDisconnectedFixtureMixture(flow, cell),
                        $"Live {flow.HasConduit(cell)}");
                    Record("pipe accounts close after reload and repair", ColonyConduits.Audit(out var detail), detail);
                    Finish();
                }
                return;
            }
            if (Once(0, "prepare a pipe save arena"))
                Arena((x, y) => (SimHashes.Oxygen, 1f, 300f));
            if (Once(5, "build two isolated liquid pipes"))
                Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, new List<int> { cell }, new List<int> { removedCell });
            if (Once(12, "break one full pipe and remove the other without its native dump"))
            {
                var inventory = PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Water), 3, 300, 0, .25)
                    .Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.DirtyWater), 7, 300, 1, 1e-240));
                ColonyConduits.SupplyControlInventory(flow, cell, inventory);
                var hp = Grid.Objects[cell, (int)ObjectLayer.LiquidConduit].GetComponent<BuildingHP>();
                hp.DoDamage(hp.MaxHitPoints);

                ColonyConduits.SupplyControlInventory(flow, removedCell, inventory);
                flow.MarkForReplacement(removedCell);
                Grid.Objects[removedCell, (int)ObjectLayer.LiquidConduit].GetComponent<Deconstructable>().ForceDestroyAndGetMaterials();
            }
            if (Once(18, "save after the broken pipe leaves its network"))
            {
                var conduit = Grid.Objects[cell, (int)ObjectLayer.LiquidConduit].GetComponent<Conduit>();
                Record("broken pipe has no live native conduit", conduit.IsDisconnected() && !flow.HasConduit(cell),
                    $"Disconnected {conduit.IsDisconnected()}, live {flow.HasConduit(cell)}");
                Record("disconnected pipe keeps the complete mixture", HasDisconnectedFixtureMixture(flow, cell), "3 kg water and 7 kg polluted water");
                Record("removal bypassed the scalar dump", !flow.HasConduit(removedCell) && HasDisconnectedFixtureMixture(flow, removedCell),
                    $"Live {flow.HasConduit(removedCell)}");
                Record("pipe accounts close before saving", ColonyConduits.Audit(out var detail), detail);
                Finish();
            }
        }

        private static bool HasDisconnectedFixtureMixture(ConduitFlow flow, int cell)
        {
            return ColonyConduits.TryRead(flow, cell, out var inventory) && inventory.Count == 2 &&
                ScenarioPart(inventory.Parts, ColonyConduits.MaterialOf(SimHashes.Water).Id).Minus(new PipeAmount(3)).Sign == 0 &&
                ScenarioPart(inventory.Parts, ColonyConduits.MaterialOf(SimHashes.DirtyWater).Id).Minus(new PipeAmount(7)).Sign == 0;
        }
    }
}
