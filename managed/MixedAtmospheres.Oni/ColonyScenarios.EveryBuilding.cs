using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Every building of the active content, one at a time, in a sealed room
    // of oxygen over a granite floor: built complete as the game builds it,
    // left for 22 ticks (every 200 ms, 1 s and 4 s update of its components
    // runs at least once), then removed. What the game logs meanwhile is the
    // building's: an exception, an error or an assertion.
    //
    // The scenario exists because of the Fertilizer Synthesizer
    // (docs/playtest-1.13.7.md): its component read a table the owner did not
    // publish and threw at every tick, in a building no scenario had ever
    // placed. A building needs no inputs to fail that way; it only has to
    // stand in an owned colony.
    //
    // Some buildings log an error wherever they are built like this (a
    // rocket module off a rocket, a bridge with nothing to join). Those are
    // ONI's and are listed in EveryBuildingBaseline, which the same scenario
    // prints when it runs under ONI's own simulation (--mode colony-vanilla):
    // the mod's run fails on whatever a building logs beyond its baseline.
    internal static partial class ColonyScenarios
    {
        internal sealed class CensusEntry
        {
            public string Id = "", Rule = "", Outcome = "pending";
            public int Width, Height, BuiltFrame = -1;
            public bool Deprecated, DebugOnly, Survived;
            public int LogCount;
            public List<string> Components = new List<string>();
            public List<string> Logs = new List<string>();      // every signature seen in the building's window
            public List<string> Beyond = new List<string>();    // those outside its baseline
        }
        private const int CensusWidth = 16, CensusHeight = 12, CensusLife = 22, CensusSettle = 3;
        private const int CensusOriginX = 7, CensusOriginY = 1;
        private static readonly List<CensusEntry> census = new List<CensusEntry>();
        private static List<BuildingDef>? censusDefs;
        private static int censusNext, censusPhaseFrame;
        private static string censusPhase = "";
        private static GameObject? censusBuilding, censusSupport;
        private static CensusEntry? censusEntry;
        private static bool censusListening;
        // The sim-facing components worth knowing about when a building misbehaves.
        private static readonly Type[] CensusComponents =
        {
            typeof(ElementConsumer), typeof(ElementEmitter), typeof(BuildingElementEmitter), typeof(ElementConverter), typeof(ConduitConsumer),
            typeof(ConduitDispenser), typeof(Storage), typeof(SimCellOccupier), typeof(Sublimates), typeof(EnergyConsumer), typeof(Generator),
            typeof(StructureTemperatureComponents), typeof(Vent), typeof(Door), typeof(LogicPorts), typeof(RadiationEmitter), typeof(HighEnergyParticleStorage),
        };

        private static void CensusLog(string condition, string stackTrace, LogType type)
        {
            if (censusEntry == null) return;
            var mod = condition.IndexOf("[Mixed Atmospheres]", StringComparison.Ordinal) >= 0;
            if (type == LogType.Log) return;
            // The mod's own warnings count when they report an exception or a fault; the game's warnings are routine.
            if (type == LogType.Warning && !(mod && (condition.IndexOf("xception", StringComparison.Ordinal) >= 0 || condition.IndexOf("fault", StringComparison.OrdinalIgnoreCase) >= 0))) return;
            var signature = CensusSignature(condition, stackTrace, type);
            if (!censusEntry.Logs.Contains(signature) && censusEntry.Logs.Count < 12) censusEntry.Logs.Add(signature);
            ++censusEntry.LogCount;
        }

        // A log entry without what changes from run to run: the kind, the
        // first line with digits removed, and the first game frame of the trace.
        internal static string CensusSignature(string condition, string stackTrace, LogType type)
        {
            var first = (condition ?? "").Split('\n')[0];
            var text = new System.Text.StringBuilder();
            foreach (var c in first) if (!char.IsDigit(c)) text.Append(c);
            // Without a closing full stop: CoreCLR (Moono) ends the null reference message with one, Mono does not.
            var line = text.ToString().Trim().TrimEnd('.');
            if (line.Length > 140) line = line.Substring(0, 140);
            var frame = "";
            foreach (var raw in (stackTrace ?? "").Split('\n'))
            {
                var at = raw.Trim();
                if (at.Length == 0 || at.StartsWith("UnityEngine.", StringComparison.Ordinal) || at.StartsWith("Debug", StringComparison.Ordinal) ||
                    at.StartsWith("LogCatcher", StringComparison.Ordinal) || at.StartsWith("KCrashReporter", StringComparison.Ordinal)) continue;
                var end = at.IndexOf('(');
                frame = (end > 0 ? at.Substring(0, end) : at).Replace("at ", "").Trim();
                break;
            }
            return type + ": " + line + (frame.Length > 0 ? " @ " + frame : "");
        }

        private static (SimHashes element, float mass, float temperature) CensusCell(int x, int y) =>
            y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1f, 300f);

        // The buildings of this part: those the active content allows, by prefab ID, cut into `parts` runs.
        private static List<BuildingDef> CensusDefs(int part, int parts)
        {
            var all = Assets.BuildingDefs.Where(d => d != null && d.BuildingComplete != null && Game.IsCorrectDlcActiveForCurrentSave(d))
                .OrderBy(d => d.PrefabID, StringComparer.Ordinal).ToList();
            if (parts <= 1) return all;
            int from = all.Count * (part - 1) / parts, to = all.Count * part / parts;
            return all.GetRange(from, to - from);
        }

        // The room as it was filled: what a building left behind (its tile, its gas, its liquid, its heat) is replaced.
        private static void CensusRestore()
        {
            FloorArenas.Clear(x0, y0, CensusWidth, CensusHeight, out _);
            var tool = CellEventLogger.Instance.SandBoxTool;
            for (var y = 0; y < CensusHeight; ++y)
                for (var x = 0; x < CensusWidth; ++x)
                {
                    var cell = Cell(x, y); var (element, mass, temperature) = CensusCell(x, y);
                    var solid = IsSolid(element);
                    var same = Grid.Solid[cell] == solid && Grid.Element[cell].id == element && Math.Abs(Grid.Temperature[cell] - temperature) < 25 &&
                        Grid.Mass[cell] > mass * .5f && Grid.Mass[cell] < mass * 2f;
                    if (!same) SimMessages.ReplaceElement(cell, element, tool, mass, temperature);
                }
        }

        private static void EveryBuilding(int frame, int part, int parts)
        {
            if (Once(0, "arena"))
            {
                census.Clear(); censusNext = 0; censusBuilding = censusSupport = null; censusEntry = null; censusPhase = "restore"; censusPhaseFrame = 0;
                censusDefs = CensusDefs(part, parts);
                Arena(CensusCell, CensusWidth, CensusHeight);
                if (!censusListening) { Application.logMessageReceived += CensusLog; censusListening = true; }
            }
            if (frame < 8 || censusDefs == null || Current.Finished) return;
            var now = ColonyFrame();
            if (censusPhase == "restore")
            {
                // The entry of the building just removed stays open while the room settles: what its removal logs is its own.
                if (censusPhaseFrame == 0) { CensusRestore(); censusPhaseFrame = now; return; }
                if (now - censusPhaseFrame < CensusSettle) return;
                CensusClose();
                if (censusNext >= censusDefs.Count) { CensusFinish(part, parts); return; }
                CensusBuild(censusDefs[censusNext++], now);
                return;
            }
            if (censusPhase == "live" && (now - censusPhaseFrame >= CensusLife || censusEntry!.LogCount > 200))
            {
                var entry = censusEntry!;
                entry.Survived = censusBuilding != null;
                // A building that removes itself (an ice sculpture at 300 K, a trap without its floor) does so under either simulation.
                if (entry.Outcome == "built" && !entry.Survived) entry.Logs.Add("Gone: the building removed itself within its " + CensusLife + " ticks");
                if (censusBuilding != null)
                {
                    foreach (var type in CensusComponents) if (censusBuilding.GetComponent(type) != null) entry.Components.Add(type.Name);
                    try { Util.KDestroyGameObject(censusBuilding); }
                    catch (Exception error) { entry.Logs.Add("Exception: removal threw " + error.GetType().Name + ": " + error.Message); }
                }
                // The engine a base-game rocket module stood on goes after it.
                if (censusSupport != null)
                    try { Util.KDestroyGameObject(censusSupport); }
                    catch (Exception error) { entry.Logs.Add("Exception: removal of the engine below threw " + error.GetType().Name + ": " + error.Message); }
                censusBuilding = censusSupport = null; censusPhase = "restore"; censusPhaseFrame = 0;
            }
        }

        // A building's materials: for each ingredient the first material of the active content that stays solid
        // well above the room's 300 K. The game's own default (BuildingDef.DefaultElements) is the first element
        // with the ingredient's tag, enabled or not, and for plastic that is one that melts at room temperature:
        // the building would be gone before its first update.
        private static List<Tag> CensusMaterials(BuildingDef def)
        {
            var chosen = new List<Tag>();
            foreach (var category in def.MaterialCategory)
            {
                var valid = MaterialSelector.GetValidMaterials(category, true);
                if (valid.Count == 0) valid = MaterialSelector.GetValidMaterials(category);
                if (valid.Count == 0) continue;
                var index = valid.FindIndex(tag => ElementLoader.GetElement(tag) is Element element && !element.disabled && element.highTemp >= 400f);
                chosen.Add(valid[index < 0 ? 0 : index]);
            }
            return chosen;
        }
        // The game tags most of a rocket interior's buildings; its four gas and liquid ports go by their name.
        private static bool RocketInteriorBuilding(BuildingDef def) =>
            def.BuildingComplete.GetComponent<KPrefabID>() is KPrefabID kind && kind.HasTag(GameTags.RocketInteriorBuilding) ||
            def.PrefabID.StartsWith("RocketInterior", StringComparison.Ordinal);
        private static void CensusBuild(BuildingDef def, int now)
        {
            var entry = new CensusEntry { Id = def.PrefabID, Width = def.WidthInCells, Height = def.HeightInCells, Rule = def.BuildLocationRule.ToString(),
                Deprecated = def.Deprecated, DebugOnly = def.DebugOnly };
            census.Add(entry); censusEntry = entry; Current.Stage = $"{census.Count} of {censusDefs!.Count}: {def.PrefabID}";
            censusPhase = "live"; censusPhaseFrame = now; entry.BuiltFrame = now;
            // A Spaced Out! rocket module exists only as part of a rocket: built alone it has no craft, and ONI's own
            // circuit manager then throws at every frame (seen under ONI's own simulation). every-rocket-building
            // builds those. A base-game module stands on the ground or on the module below it: an engine is built
            // alone here, any other module on top of a steam engine, as a player stacks them.
            if (def.BuildingComplete.GetComponent<RocketModuleCluster>() != null || def.BuildingComplete.GetComponent<ReorderableBuilding>() != null)
            { entry.Outcome = "rocket module"; censusBuilding = null; censusPhaseFrame = now - CensusLife; return; }
            var support = def.BuildingComplete.GetComponent<RocketModule>() != null && def.BuildingComplete.GetComponent<RocketEngine>() == null
                ? Assets.GetBuildingDef(SteamEngineConfig.ID) : null;
            // The same holds for a building of a rocket's interior (its ports, power plug and control station): outside
            // a rocket its power plug has no circuit and every wire built afterwards throws, also under ONI's own simulation.
            if (RocketInteriorBuilding(def))
            { entry.Outcome = "rocket interior building"; censusBuilding = null; censusPhaseFrame = now - CensusLife; return; }
            if (def.WidthInCells > CensusWidth - 2 || def.HeightInCells + (support?.HeightInCells ?? 0) > CensusHeight - 1) { entry.Outcome = "does not fit the room"; entry.Logs.Add("Skipped: does not fit the room"); censusBuilding = null; censusPhaseFrame = now - CensusLife; return; }
            try
            {
                if (support != null) censusSupport = support.Build(Cell(CensusOriginX, CensusOriginY), Orientation.Neutral, null, CensusMaterials(support), 300, playsound: false);
                censusBuilding = def.Build(Cell(CensusOriginX, CensusOriginY + (support?.HeightInCells ?? 0)), Orientation.Neutral, null, CensusMaterials(def), 300, playsound: false);
                entry.Outcome = "built";
            }
            catch (Exception error)
            {
                censusBuilding = null; entry.Outcome = "build threw";
                entry.Logs.Add("Exception: build threw " + error.GetType().Name + ": " + error.Message.Split('\n')[0]);
            }
        }

        private static void CensusClose()
        {
            var entry = censusEntry; censusEntry = null;
            if (entry == null) return;
            EveryBuildingBaseline.TryGetValue(entry.Id, out var known);
            entry.Beyond = entry.Logs.Where(log => known == null || !known.Contains(log)).ToList();
            // The lines themselves go into the scenario's report file: repeated here, an error ONI logs for a
            // building under either simulation would read as a failure of the mod to the harness.
            if (entry.Logs.Count != 0)
                Debug.Log($"[Mixed Atmospheres] census {(Vanilla ? "baseline" : "run")}: {entry.Id} has {entry.Logs.Count} recorded line(s), {entry.Beyond.Count} beyond its baseline");
        }

        private static void CensusFinish(int part, int parts)
        {
            if (censusListening) { Application.logMessageReceived -= CensusLog; censusListening = false; }
            var built = census.Count(e => e.Outcome == "built");
            var unfit = census.Where(e => e.Outcome == "does not fit the room").Select(e => e.Id).ToList();
            var rockets = census.Count(e => e.Outcome == "rocket module" || e.Outcome == "rocket interior building");
            var threw = census.Where(e => e.Outcome == "build threw").ToList();
            var noisy = census.Where(e => e.Logs.Count != 0).ToList();
            var beyond = census.Where(e => e.Beyond.Count != 0).ToList();
            try
            {
                ReportJson.WriteAtomic(Path.Combine(ColonyRuntimeValidation.OutputDirectory, parts <= 1 ? "every-building.json" : $"every-building-{part}of{parts}.json"),
                    new { Vanilla, Part = part, Parts = parts, Buildings = census });
            }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] every-building report not written: " + error.Message); }
            Record("the active content's buildings were built", census.Count > 20 && built + unfit.Count + threw.Count + rockets == census.Count && built > census.Count / 2,
                $"{built} of {census.Count} built, {rockets} rocket modules and rocket interior buildings left to the rocket scenarios" + (unfit.Count != 0 ? "; too large for the room: " + string.Join(", ", unfit) : ""));
            if (Vanilla)
                // The reference run: what each building logs under ONI's own simulation is the baseline, printed above.
                Record("ONI's own simulation: baseline recorded", true, $"{noisy.Count} buildings log something: " + string.Join(", ", noisy.Select(e => e.Id)));
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

        // What a building logs when it is built like this under ONI's own
        // simulation, by prefab ID (signatures as CensusSignature writes
        // them). Regenerate with --mode colony-vanilla --scenario every-building-N:
        // the run prints one "every-building baseline:" line per building.
        // Recorded 2026-11-25 on game build 764025 (Spaced Out! and base game): two legacy geyser prefabs without a
        // configuration and a third log one error each, an ice sculpture melts at 300 K, a few plants and the
        // sweepy leave without what they grow or dock on, and the base game's command capsule is taller than the room.
        internal static readonly Dictionary<string, HashSet<string>> EveryBuildingBaseline = new Dictionary<string, HashSet<string>>(StringComparer.Ordinal)
        {
            { "ChlorineGeyser", new HashSet<string> { "Error: ~~~!System.NullReferenceException: Object reference not set to an instance of an object" } },
            { "CrewCapsule", new HashSet<string> { "Skipped: does not fit the room" } },
            { "ForestTreeBranch", new HashSet<string> { "Gone: it removed itself within its 22 ticks" } },
            { "Geyser", new HashSet<string> { "Error: ~~~!System.NullReferenceException: Object reference not set to an instance of an object" } },
            { "IceSculpture", new HashSet<string> { "Gone: the building removed itself within its 22 ticks" } },
            // The game's own cleanup of the story mailbox, in a colony whose worlds were generated without the Lonely
            // Minion story (the story traits are drawn per colony; seen 2026-11-27, release tier on 48fa61e).
            { "LonelyMailBox", new HashSet<string> { "Exception: NullReferenceException: Object reference not set to an instance of an object @ LonelyMinionMailbox.OnCleanUp" } },
            { "MethaneGeyser", new HashSet<string> { "Error: ~~~!System.NullReferenceException: Object reference not set to an instance of an object" } },
            { "SaltPlant", new HashSet<string> { "Gone: it removed itself within its 22 ticks" } },
            { "SpiceVine", new HashSet<string> { "Gone: it removed itself within its 22 ticks" } },
            { "StaterpillarGenerator", new HashSet<string> { "Gone: the building removed itself within its 22 ticks" } },
            { "SweepBot", new HashSet<string> { "Gone: it removed itself within its 22 ticks" } },
        };
    }
}
