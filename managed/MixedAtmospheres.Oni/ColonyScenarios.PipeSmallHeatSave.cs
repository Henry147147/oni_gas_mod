using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static void PipeSmallHeatSave(int frame)
        {
            var flow = Game.Instance.gasConduitFlow;
            if (Once(0, "isolate a small radiant gas contact"))
                Arena((x,y) => (SimHashes.Vacuum, 0f, 300f));
            var cell = Cell(3,3);
            if (Once(5, "build the radiant pipe"))
                Pipe(Game.Instance.gasConduitSystem, GasConduitRadiantConfig.ID, new List<int>{cell});
            if (frame >= 12 && step <= 12 && !flow.HasConduit(cell))
            {
                if (frame >= 32) { Record("radiant pipe joined its network", false, "Topology timeout"); Finish(); }
                return;
            }
            if (Once(12, "supply a contact below the native sender cutoff"))
            {
                var body = Grid.Objects[cell,(int)ObjectLayer.GasConduit].GetComponent<PrimaryElement>();
                var oxygen = ColonyConduits.MaterialOf(SimHashes.Oxygen);
                ColonyConduits.StartControl();
                ColonyPipeContactHeat.StartControl();
                recorded["small contacts"] = 0;
                ColonyPipeContactHeat.ValidationObserved = (observedFlow, observedCell, input) =>
                {
                    if (observedFlow != flow || observedCell != cell) return;
                    var after = ColonyConduits.ThermalProbeNative(flow,cell).temperature;
                    var maxWork = Math.Abs(input.FluidTemperature-input.BodyTemperature)*input.FluidCapacity;
                    if (maxWork <= 1e-6f && after != input.FluidTemperature)
                        ++recorded["small contacts"];
                };
                ColonyConduits.SupplyControlInventory(flow, cell, PipeInventory.Single(oxygen, .000125, body.Temperature+.005f, 255, 0));
                recorded["small heat setup"] = frame;
            }
            if (recorded.TryGetValue("small heat setup", out var start) && Once((int)start+20, "settle the ordinary save boundary"))
            {
                ColonyPipeContactHeat.ValidationObserved = null;
                Game.Instance.StartCoroutine(FinishSmallHeatSave());
            }
        }
        private static IEnumerator FinishSmallHeatSave()
        {
            yield return new WaitForEndOfFrame();
            Record("the native carrier changed below the body-message cutoff", recorded["small contacts"] > 0,
                $"{recorded["small contacts"]} validated contacts with a nonzero temperature change");
            try
            {
                var settled = ColonySaveBarrier.Settle();
                var mismatch = ColonyPipeContactHeat.BodyHeatMismatch();
                Record("save settled all genuine body messages", mismatch == null, mismatch ?? $"Settled frame {settled}");
                Record("pipe heat accounts close", ColonyConduits.Audit(out var detail), detail);
            }
            catch (Exception error) { Record("ordinary pipe heat save barrier completed", false, error.ToString()); }
            Finish();
        }
    }
}
