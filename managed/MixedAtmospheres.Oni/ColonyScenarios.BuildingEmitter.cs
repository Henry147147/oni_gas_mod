using System;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // A building that emits a gas directly (ONI's BuildingElementEmitter: the
    // Fertilizer Synthesizer's natural gas, the Blastshot Maker's carbon
    // dioxide). Unlike other emitters it reads back what it emitted, every
    // tick, from the emitted-mass table of the game's data by its handle's
    // index (docs/colony-owner.md#element-consumers-and-emitters). The owner
    // runs the emitter, so the owner's publication has to hold that record:
    // without it the read was a null address and the game stopped at every
    // tick (the player's report of 2026-11-25, docs/playtest-1.13.7.md).
    internal static partial class ColonyScenarios
    {
        private static readonly System.Reflection.FieldInfo BuildingEmitterHandle = AccessTools.Field(typeof(BuildingElementEmitter), "simHandle");
        private static unsafe void BuildingEmitter(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, .5f, 300f));
            if (Once(6, "build"))
            {
                recorded["gas"] = ArenaSpecies(SimHashes.Methane);
                machine = Build(FertilizerMakerConfig.ID, Cell(3, 0));
                Store(machine, SimHashes.DirtyWater, 50); Store(machine, SimHashes.Dirt, 50); Store(machine, SimHashes.Phosphorite, 50);
            }
            if (Once(70, "check"))
            {
                var added = ArenaSpecies(SimHashes.Methane) - recorded["gas"];
                var emitter = machine != null ? machine.GetComponent<BuildingElementEmitter>() : null;
                var handle = emitter != null ? (int)BuildingEmitterHandle.GetValue(emitter) : -1;
                var rate = emitter != null && Sim.IsValidHandle(handle) ? emitter.AverageEmitRate : 0;
                Record("the synthesizer's natural gas reached the owner", added > .05, $"+{added:G4} kg natural gas; {MachineState(machine)}");
                Record("the game's data holds an emitted-mass table for the emitter's handle", Sim.IsValidHandle(handle) && Game.Instance.simData.emittedMassEntries != null,
                    $"handle 0x{handle:x8}, table {(Game.Instance.simData.emittedMassEntries != null ? "present" : "a null address")}");
                // The rate is what the building read from its record, averaged by the game.
                Record("the building reads the rate it emits at", rate > .008 && rate < .012, $"average rate {rate * 1000:G4} g/s (the synthesizer emits 10 g/s while running)");
                Finish();
            }
        }
    }
}
