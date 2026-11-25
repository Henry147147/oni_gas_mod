using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Every critter, plant and geyser of the active content, one at a time in
    // the room of every-building: spawned as the game spawns it, left for 22
    // ticks, removed. Critters and plants breathe and exhale through element
    // consumers and emitters, geysers emit, a Sanishell emits germs: each can
    // read simulation data the owner must publish, as a building can
    // (docs/playtest-1.13.7.md). What the game logs meanwhile is compared
    // with what it logs under ONI's own simulation (EveryBuildingBaseline,
    // printed by --mode colony-vanilla).
    internal static partial class ColonyScenarios
    {
        private static List<GameObject>? livingPrefabs;
        private static readonly Type[] LivingComponents =
        {
            typeof(ElementConsumer), typeof(ElementEmitter), typeof(DiseaseEmitter), typeof(Sublimates), typeof(Geyser), typeof(PressureVulnerable),
            typeof(TemperatureVulnerable), typeof(Storage), typeof(RadiationEmitter), typeof(Light2D),
        };
        private static List<GameObject> LivingPrefabs(int part, int parts)
        {
            var found = new Dictionary<string, GameObject>(StringComparer.Ordinal);
            foreach (var prefab in Assets.GetPrefabsWithTag(GameTags.Creature).Concat(Assets.GetPrefabsWithTag(GameTags.Plant)).Concat(Assets.GetPrefabsWithComponent<Geyser>()))
                if (prefab != null && prefab.GetComponent<KPrefabID>() is KPrefabID id) found[id.PrefabTag.Name] = prefab;
            var all = found.OrderBy(e => e.Key, StringComparer.Ordinal).Select(e => e.Value).ToList();
            if (parts <= 1) return all;
            int from = all.Count * (part - 1) / parts, to = all.Count * part / parts;
            return all.GetRange(from, to - from);
        }

        private static void EveryLiving(int frame, int part, int parts)
        {
            if (Once(0, "arena"))
            {
                census.Clear(); censusNext = 0; censusBuilding = null; censusEntry = null; censusPhase = "restore"; censusPhaseFrame = 0;
                livingPrefabs = LivingPrefabs(part, parts);
                Arena(CensusCell, CensusWidth, CensusHeight);
                if (!censusListening) { Application.logMessageReceived += CensusLog; censusListening = true; }
            }
            if (frame < 8 || livingPrefabs == null || Current.Finished) return;
            var now = ColonyFrame();
            if (censusPhase == "restore")
            {
                if (censusPhaseFrame == 0) { CensusRestore(); censusPhaseFrame = now; return; }
                if (now - censusPhaseFrame < CensusSettle) return;
                CensusClose();
                if (censusNext >= livingPrefabs.Count) { LivingFinish(part, parts); return; }
                var prefab = livingPrefabs[censusNext++]; var id = prefab.GetComponent<KPrefabID>().PrefabTag.Name;
                var kind = prefab.GetComponent<Geyser>() != null ? "geyser" : prefab.HasTag(GameTags.Plant) ? "plant" : "critter";
                var entry = new CensusEntry { Id = id, Rule = kind, BuiltFrame = now };
                census.Add(entry); censusEntry = entry; Current.Stage = $"{census.Count} of {livingPrefabs.Count}: {kind} {id}";
                censusPhase = "live"; censusPhaseFrame = now;
                try
                {
                    var layer = kind == "critter" ? Grid.SceneLayer.Creatures : Grid.SceneLayer.BuildingFront;
                    censusBuilding = GameUtil.KInstantiate(prefab, Grid.CellToPosCBC(Cell(CensusOriginX, CensusOriginY), layer), layer);
                    censusBuilding.SetActive(true); entry.Outcome = "built";
                }
                catch (Exception error)
                {
                    censusBuilding = null; entry.Outcome = "build threw";
                    entry.Logs.Add("Exception: spawn threw " + error.GetType().Name + ": " + error.Message.Split('\n')[0]);
                }
                return;
            }
            if (censusPhase == "live" && (now - censusPhaseFrame >= CensusLife || censusEntry!.LogCount > 200))
            {
                var entry = censusEntry!;
                entry.Survived = censusBuilding != null;
                if (censusBuilding != null)
                {
                    foreach (var type in LivingComponents) if (censusBuilding.GetComponent(type) != null) entry.Components.Add(type.Name);
                    try { Util.KDestroyGameObject(censusBuilding); }
                    catch (Exception error) { entry.Logs.Add("Exception: removal threw " + error.GetType().Name + ": " + error.Message); }
                }
                else if (entry.Outcome == "built") entry.Logs.Add("Gone: it removed itself within its " + CensusLife + " ticks");
                censusBuilding = null; censusPhase = "restore"; censusPhaseFrame = 0;
            }
        }

        private static void LivingFinish(int part, int parts)
        {
            if (censusListening) { Application.logMessageReceived -= CensusLog; censusListening = false; }
            var built = census.Count(e => e.Outcome == "built");
            var noisy = census.Where(e => e.Logs.Count != 0).ToList(); var beyond = census.Where(e => e.Beyond.Count != 0).ToList();
            try { ReportJson.WriteAtomic(Path.Combine(ColonyRuntimeValidation.OutputDirectory, $"every-living-{part}of{parts}.json"), new { Vanilla, Part = part, Parts = parts, Buildings = census }); }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] every-living report not written: " + error.Message); }
            Record("the active content's critters, plants and geysers were spawned", census.Count > 20 && built > census.Count / 2,
                $"{built} of {census.Count} spawned ({census.Count(e => e.Rule == "critter")} critters, {census.Count(e => e.Rule == "plant")} plants, {census.Count(e => e.Rule == "geyser")} geysers)");
            if (Vanilla) Record("ONI's own simulation: baseline recorded", true, $"{noisy.Count} log something: " + string.Join(", ", noisy.Select(e => e.Id)));
            else
            {
                Record("none logged an exception or an error beyond what it logs under ONI's own simulation", beyond.Count == 0,
                    beyond.Count == 0 ? $"{noisy.Count} log only their baseline" :
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
