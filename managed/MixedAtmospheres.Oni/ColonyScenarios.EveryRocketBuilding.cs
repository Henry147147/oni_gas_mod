using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // The buildings every-building leaves out, where they belong (Spaced
    // Out!): each rocket engine alone on a launch pad, each other rocket
    // module on top of an engine, and each rocket interior building inside a
    // habitat module's interior. Built through the player's own construction
    // commands, left for 22 ticks, removed. What the game logs meanwhile is
    // compared with what it logs under ONI's own simulation
    // (EveryBuildingBaseline, printed by --mode colony-vanilla).
    internal static partial class ColonyScenarios
    {
        private static readonly List<(BuildingDef def, string kind)> rocketCensus = new List<(BuildingDef, string)>();
        private static GameObject? rocketPad, rocketEngine, rocketHabitat, rocketSubject;
        private static WorldContainer? rocketInterior;
        private static string rocketStage = "";
        private static int rocketStageFrame, rocketNext;

        private static string RocketKind(BuildingDef def)
        {
            var prefab = def.BuildingComplete;
            if (prefab.GetComponent<RocketEngineCluster>() != null) return "engine";
            if (prefab.GetComponent<RocketModuleCluster>() != null) return "module";
            if (RocketInteriorBuilding(def)) return "interior";
            return "";
        }

        // A free spot of the interior for a building of the given size: its cells open and empty, a floor under it.
        private static int RocketInteriorCell(WorldContainer world, BuildingDef def)
        {
            int left = -(def.WidthInCells - 1) / 2, right = def.WidthInCells / 2;
            for (var y = world.WorldOffset.y + 1; y < world.WorldOffset.y + world.WorldSize.y - def.HeightInCells; ++y)
                for (var x = world.WorldOffset.x + 1 - left; x < world.WorldOffset.x + world.WorldSize.x - 1 - right; ++x)
                {
                    var free = true;
                    for (var dy = 0; dy < def.HeightInCells && free; ++dy)
                        for (var dx = left; dx <= right && free; ++dx)
                        {
                            var cell = Grid.XYToCell(x + dx, y + dy);
                            free = Grid.IsValidCell(cell) && Grid.WorldIdx[cell] == world.id && !Grid.Solid[cell] &&
                                Grid.Objects[cell, (int)ObjectLayer.Building] == null && Grid.Objects[cell, (int)def.ObjectLayer] == null;
                        }
                    for (var dx = left; dx <= right && free; ++dx) free = Grid.Solid[Grid.XYToCell(x + dx, y - 1)];
                    if (free) return Grid.XYToCell(x, y);
                }
            return -1;
        }

        private static void EveryRocketBuilding(int frame)
        {
            if (Once(0, "rocket site"))
            {
                if (!DlcManager.IsExpansion1Active()) throw new InvalidOperationException("The rocket buildings require Spaced Out! content.");
                census.Clear(); censusEntry = null; rocketCensus.Clear(); rocketNext = 0; rocketStage = "engines"; rocketStageFrame = 0;
                rocketPad = rocketEngine = rocketHabitat = rocketSubject = null; rocketInterior = null;
                // Engines first, then the other modules, then the interior buildings; by prefab ID within each.
                foreach (var wanted in new[] { "engine", "module", "interior" })
                    foreach (var candidate in Assets.BuildingDefs.Where(d => d != null && d.BuildingComplete != null && Game.IsCorrectDlcActiveForCurrentSave(d)).OrderBy(d => d.PrefabID, StringComparer.Ordinal))
                        if (RocketKind(candidate) == wanted) rocketCensus.Add((candidate, wanted));
                FloorArenas.Clear(x0 - 1, y0 - 1, 14, 34, out _);
                for (var y = -1; y < 33; ++y) for (var x = -1; x < 13; ++x)
                {
                    var wall = y == -1 || y == 32 || x == -1 || x == 12;
                    SimMessages.ReplaceElement(Cell(x, y), wall ? SimHashes.Unobtanium : SimHashes.Vacuum, CellEventLogger.Instance.SandBoxTool, wall ? 20000 : 0, wall ? 300 : 0);
                }
                if (!censusListening) { Application.logMessageReceived += CensusLog; censusListening = true; }
            }
            if (Once(4, "rocket platform")) rocketPad = Build(LaunchPadConfig.ID, Cell(5, 0));
            if (frame < 10 || Current.Finished) return;
            var now = ColonyFrame();
            // The subject's window has ended: remove it as the game removes it, then let the removal settle.
            if (rocketStage.EndsWith(" live", StringComparison.Ordinal))
            {
                if (now - rocketStageFrame < CensusLife && censusEntry!.LogCount <= 200) return;
                var entry = censusEntry!; entry.Survived = rocketSubject != null;
                if (rocketSubject != null)
                {
                    foreach (var type in CensusComponents) if (rocketSubject.GetComponent(type) != null) entry.Components.Add(type.Name);
                    try
                    {
                        if (rocketStage == "interior live") Util.KDestroyGameObject(rocketSubject);
                        else AccessTools.Method(typeof(ReorderableBuilding), "RemoveModule").Invoke(rocketSubject.GetComponent<ReorderableBuilding>(), null);
                    }
                    catch (Exception error) { var cause = error.InnerException ?? error; entry.Logs.Add("Exception: removal threw " + cause.GetType().Name + ": " + cause.Message.Split('\n')[0]); }
                }
                else if (entry.Outcome == "built") entry.Logs.Add("Gone: the building removed itself within its " + CensusLife + " ticks");
                rocketSubject = null; rocketStage = rocketStage.Replace(" live", " settle"); rocketStageFrame = now; return;
            }
            if (rocketStage.EndsWith(" settle", StringComparison.Ordinal))
            {
                if (now - rocketStageFrame < 6) return;
                CensusClose(); rocketStage = rocketStage.Replace(" settle", "");
            }
            if (rocketNext >= rocketCensus.Count) { RocketCensusFinish(); return; }
            var (def, kind) = rocketCensus[rocketNext];
            // The rocket each kind needs: none for an engine, an engine for a module, an engine and a habitat for the interior.
            if (kind != "engine" && rocketEngine == null) { rocketEngine = AddRocketModule(rocketPad!, CO2EngineConfig.ID, true); rocketStageFrame = now; return; }
            if (kind == "interior" && rocketHabitat == null)
            {
                if (now - rocketStageFrame < 4) return;
                rocketHabitat = AddRocketModule(rocketEngine!, HabitatModuleMediumConfig.ID, false); rocketStageFrame = now; return;
            }
            if (kind == "interior" && rocketInterior == null)
            {
                var door = rocketHabitat!.GetComponent<ClustercraftExteriorDoor>();
                var paired = door == null ? null : AccessTools.Field(typeof(ClustercraftExteriorDoor), "targetDoor").GetValue(door);
                if (paired != null) rocketInterior = door!.GetTargetWorld();
                if (rocketInterior == null) { if (now - rocketStageFrame > 60) throw new InvalidOperationException("The habitat module's interior did not appear."); return; }
                rocketStageFrame = now; return;
            }
            if (now - rocketStageFrame < 4) return;
            ++rocketNext;
            var subject = new CensusEntry { Id = def.PrefabID, Width = def.WidthInCells, Height = def.HeightInCells, Rule = def.BuildLocationRule.ToString(), Deprecated = def.Deprecated, DebugOnly = def.DebugOnly, BuiltFrame = now };
            census.Add(subject); censusEntry = subject; Current.Stage = $"{census.Count} of {rocketCensus.Count}: {kind} {def.PrefabID}";
            rocketStage = kind + " live"; rocketStageFrame = now;
            try
            {
                if (kind == "engine") rocketSubject = AddRocketModule(rocketPad!, def.PrefabID, true);
                else if (kind == "module") rocketSubject = AddRocketModule(rocketEngine!, def.PrefabID, false);
                else
                {
                    var cell = RocketInteriorCell(rocketInterior!, def);
                    if (cell < 0) { subject.Outcome = "no room in the interior"; subject.Logs.Add("Skipped: no room in the interior"); rocketSubject = null; rocketStageFrame = now - CensusLife; return; }
                    rocketSubject = def.Build(cell, Orientation.Neutral, null, CensusMaterials(def), 300, playsound: false);
                }
                subject.Outcome = "built";
            }
            catch (Exception error)
            {
                var cause = error.InnerException ?? error;
                rocketSubject = null; subject.Outcome = "build threw"; subject.Logs.Add("Exception: build threw " + cause.GetType().Name + ": " + cause.Message.Split('\n')[0]);
                rocketStageFrame = now - CensusLife;
            }
        }

        private static void RocketCensusFinish()
        {
            if (censusListening) { Application.logMessageReceived -= CensusLog; censusListening = false; }
            var built = census.Count(e => e.Outcome == "built");
            var noisy = census.Where(e => e.Logs.Count != 0).ToList(); var beyond = census.Where(e => e.Beyond.Count != 0).ToList();
            try { ReportJson.WriteAtomic(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "every-rocket-building.json"), new { Vanilla, Buildings = census }); }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] every-rocket-building report not written: " + error.Message); }
            Record("the rocket modules and rocket interior buildings were built", census.Count > 20 && built > census.Count / 2,
                $"{built} of {census.Count} built ({rocketCensus.Count(e => e.kind == "engine")} engines, {rocketCensus.Count(e => e.kind == "module")} other modules, {rocketCensus.Count(e => e.kind == "interior")} interior buildings)");
            if (Vanilla) Record("ONI's own simulation: baseline recorded", true, $"{noisy.Count} buildings log something: " + string.Join(", ", noisy.Select(e => e.Id)));
            else
            {
                Record("no building logged an exception or an error beyond what it logs under ONI's own simulation", beyond.Count == 0,
                    beyond.Count == 0 ? $"{noisy.Count} buildings log only their baseline" :
                    string.Join("; ", beyond.Take(12).Select(e => e.Id + ": " + string.Join(" | ", e.Beyond.Take(3)))) + (beyond.Count > 12 ? $"; and {beyond.Count - 12} more" : ""));
                var status = ColonyOwner.Status;
                Record("the owner ran every tick without a fault", status.State == 1 && !ColonyFault.Faulted, $"state {status.State}, faulted {ColonyFault.Faulted}, frames {status.CompletedFrames}");
                Record("the ledgers stayed closed", status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-12,
                    $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
            }
            Finish();
        }
    }
}
