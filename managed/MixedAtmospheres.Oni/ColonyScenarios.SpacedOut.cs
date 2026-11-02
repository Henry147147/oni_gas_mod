using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Spaced Out! systems that touch fluids (docs/colony-owner.md#scenarios,
    // docs/dlc-compatibility.md#spaced-out-fluid-systems): radiation, the
    // research reactor, cluster rocket engines and the rest of the DLC's
    // converters, each through the game's own mechanism with the owner's
    // ledgers closed.
    internal static partial class ColonyScenarios
    {
        private static bool SpacedOutScenario(string name, int frame)
        {
            switch (name)
            {
                case "spaced-out-radiation": SoRadiation(frame); return true;
                case "spaced-out-hep": SoRadbolt(frame); return true;
                case "spaced-out-reactor": SoReactor(frame, false); return true;
                case "spaced-out-reactor-meltdown": SoReactor(frame, true); return true;
                case "spaced-out-engine-exhaust": SoEngines(frame); return true;
                case "spaced-out-sublimation-station": SoSublimation(frame); return true;
                case "spaced-out-centrifuge": SoCentrifuge(frame); return true;
                case "spaced-out-sludge-press": SoSludgePress(frame); return true;
                case "spaced-out-intakes": SoIntakes(frame); return true;
                case "spaced-out-dehydrator": SoDehydrator(frame); return true;
                default: return false;
            }
        }

        private static void RequireSpacedOut()
        {
            if (!DlcManager.IsExpansion1Active()) throw new InvalidOperationException("This control requires actual Spaced Out! content.");
        }
        private static double SiteSpecies(SimHashes element) =>
            Enumerable.Range(0, fillWidth).Sum(x => Enumerable.Range(0, fillHeight).Sum(y => SpeciesMass(Cell(x, y), element)));
        private static double SiteMass() =>
            Enumerable.Range(0, fillWidth).Sum(x => Enumerable.Range(0, fillHeight).Sum(y => CellMass(Cell(x, y))));
        private static void LedgersClosed(string what)
        {
            var s = ColonyOwner.Status;
            Record(what + " keeps closed owner ledgers", s.State == 1 && s.MaximumRelativeMassError <= 1e-12 && s.MaximumRelativeEnergyError <= 1e-12,
                $"mass {s.MaximumRelativeMassError:R}, energy {s.MaximumRelativeEnergyError:R}");
        }

        // ---- radiation

        // A radiation lamp burning uranium ore in owned polluted oxygen: the
        // game's own radiation stage fills Grid.Radiation of owned cells, a
        // radiation sensor reads it, and germs in irradiated cells die faster
        // than in a cell behind the lamp.
        private static GameObject? soLamp, soSensor;
        private static readonly List<string> soSeries = new List<string>();
        private static void SoRadiation(int frame)
        {
            var slime = Db.Get().Diseases.SlimeGerms;
            var beam = new[] { (2, 3), (4, 3), (6, 3) };
            if (Once(0, "arena")) { RequireSpacedOut(); soSeries.Clear(); Arena((x, y) => (SimHashes.ContaminatedOxygen, 1f, 300f)); }
            if (Once(6, "build"))
            {
                Record("the game's radiation is enabled", Sim.IsRadiationEnabled(), "Sim.IsRadiationEnabled()");
                recorded["mass"] = ArenaMass();
                soLamp = Build(RadiationLightConfig.ID, Cell(0, 3));
                Store(soLamp, SimHashes.UraniumOre, 10);
                soSensor = Build(LogicRadiationSensorConfig.ID, Cell(6, 4));
            }
            if (Once(8, "sensor"))
            {
                var sensor = soSensor!.GetComponent<LogicRadiationSensor>();
                sensor.Threshold = 1; sensor.ActivateAboveThreshold = true;
            }
            if (Once(70, "irradiated"))
            {
                var rads = beam.Select(c => Grid.Radiation[Cell(c.Item1, c.Item2)]).ToArray();
                var behind = Grid.Radiation[Cell(0, 0)];
                Record("the lamp radiates owned cells", rads.All(r => r > 0), $"beam cells {string.Join(", ", rads.Select(r => r.ToString("G5")))} rads, behind the lamp {behind:G5}; {MachineState(soLamp)}");
                var on = soSensor!.GetComponent<LogicRadiationSensor>().IsSwitchedOn;
                Record("a radiation sensor reads it", on, $"switched on {on}, sensor cell {Grid.Radiation[Cell(6, 4)]:G5} rads");
                var uranium = Stored(soLamp, SimHashes.UraniumOre); var depleted = Stored(soLamp, SimHashes.DepletedUranium);
                Record("the lamp burns its fuel", uranium < 10 - 1e-4 || depleted > 0, $"uranium ore {uranium:G6} kg, depleted uranium {depleted:G6} kg");
                // Identical germs in every cell: neighbours then exchange none
                // in the first frame, so each cell's change is its own rule.
                var count = InWindow(slime, SimHashes.ContaminatedOxygen, 1);
                for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
                    SimMessages.ModifyDiseaseOnCell(Cell(x, y), DiseaseIndex(slime), count);
                recorded["germs"] = count;
            }
            if (frame >= 71 && frame <= 82 && !Current.Finished && recorded.GetValueOrDefault("series frame", -1) != frame)
            {
                recorded["series frame"] = frame;
                var cells = new[] { (2, 3), (4, 3), (6, 3), (0, 0), (7, 0) };
                soSeries.Add($"f{frame}: " + string.Join(" ", cells.Select(c => $"({c.Item1},{c.Item2}) n={Grid.DiseaseCount[Cell(c.Item1, c.Item2)]} i={Grid.DiseaseIdx[Cell(c.Item1, c.Item2)]} r={Grid.Radiation[Cell(c.Item1, c.Item2)]:G6}")));
            }
            if (Once(84, "check"))
            {
                Debug.Log("[Mixed Atmospheres] so-radiation germ series (killRate " + slime.radiationKillRate + ", seeded " + recorded["germs"] + "):\n" + string.Join("\n", soSeries));
                Record("germ series recorded", soSeries.Count > 4, string.Join(" | ", soSeries.Take(6)));
                Record("no fluid created or lost", Math.Abs(ArenaMass() - recorded["mass"]) < 1e-6 * recorded["mass"], $"{recorded["mass"]:G9} -> {ArenaMass():G9} kg");
                LedgersClosed("radiation");
                Finish();
            }
        }

        // A radbolt flies through owned oxygen into a granite wall: the owned
        // cells on its path get radiation sickness, its remaining payload
        // arrives as hot fallout, and the oxygen is intact.
        private static void SoRadbolt(int frame)
        {
            if (Once(0, "arena")) { RequireSpacedOut(); Arena((x, y) => x == 7 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1.2f, 300f)); }
            if (Once(6, "bolt"))
            {
                recorded["oxygen"] = ArenaSpecies(SimHashes.Oxygen); recorded["fallout"] = ArenaSpecies(SimHashes.Fallout);
                var bolt = GameUtil.KInstantiate(Assets.GetPrefab(HighEnergyParticleConfig.ID), Grid.CellToPosCCC(Cell(0, 3), Grid.SceneLayer.FXFront2), Grid.SceneLayer.FXFront2);
                bolt.SetActive(true);
                var particle = bolt.GetComponent<HighEnergyParticle>();
                particle.payload = 100; particle.SetDirection(EightDirection.Right);
                recorded["payload"] = 100;
            }
            if (Once(60, "check"))
            {
                var oxygen = ArenaSpecies(SimHashes.Oxygen) - recorded["oxygen"]; var fallout = ArenaSpecies(SimHashes.Fallout) - recorded["fallout"];
                var sick = Db.Get().Diseases.GetIndex(Db.Get().Diseases.RadiationPoisoning.Id);
                var germ = Enumerable.Range(0, 7).Count(x => Grid.DiseaseIdx[Cell(x, 3)] == sick && Grid.DiseaseCount[Cell(x, 3)] > 0);
                Record("the bolt's payload arrived as fallout", fallout > .01 && fallout <= .1001 + 1e-6, $"+{fallout:G6} kg fallout of at most 0.1");
                Record("the owned oxygen is intact", Math.Abs(oxygen) < 1e-6, $"{oxygen:+0.0e+0;-0.0e+0} kg");
                Record("radiation sickness marked the bolt's path", germ > 0, $"{germ} of 7 cells in its row hold radiation sickness");
                LedgersClosed("radbolt");
                Finish();
            }
        }

        // ---- research reactor

        private static GameObject? soReactor;
        private static int soReactorInput;
        private static void SoReactor(int frame, bool meltdown)
        {
            const int w = 16, h = 14;
            var flow = Game.Instance.liquidConduitFlow;
            if (Once(0, "arena"))
            {
                RequireSpacedOut(); pendingShift = w + 2 - 11;
                Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1f, 300f), w, h);
            }
            if (Once(6, "build"))
            {
                recorded["water"] = SiteSpecies(SimHashes.Water) + SiteSpecies(SimHashes.Steam); recorded["mass"] = SiteMass();
                soReactor = Build(NuclearReactorConfig.ID, Cell(8, 1));
                soReactor.GetComponents<Storage>()[0].AddOre(SimHashes.EnrichedUranium, 60, 300, byte.MaxValue, 0, false, true);
                soReactorInput = Grid.OffsetCell(Grid.PosToCell(soReactor), soReactor.GetComponent<Building>().Def.UtilityInputOffset);
                Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, new List<int> { soReactorInput });
            }
            // Coolant arrives through the reactor's conduit, ten kilograms a time.
            if (frame >= 8 && frame <= 60 && frame % 4 == 0 && soReactor != null && flow.HasConduit(soReactorInput) && Once(frame, "coolant"))
            {
                var taken = ColonyConduits.Seed(flow, soReactorInput, SimHashes.Water, 10, 300, byte.MaxValue, 0);
                recorded["seeded"] = (recorded.TryGetValue("seeded", out var s) ? s : 0) + taken;
            }
            var storages = soReactor == null ? null : soReactor.GetComponents<Storage>();
            var smi = soReactor == null ? null : soReactor.GetSMI<Reactor.StatesInstance>();
            string State() => smi == null ? "-" : smi.GetCurrentState()?.name ?? "?";
            if (Once(100, "reacting"))
            {
                var coolant = storages!.Sum(st => (double)st.GetMassAvailable(SimHashes.Water));
                Record("coolant reached the reactor through its conduit", coolant >= 30 && recorded.GetValueOrDefault("seeded") >= 30,
                    $"{recorded.GetValueOrDefault("seeded"):G5} kg piped in, reactor holds {coolant:G5} kg water; state {State()}");
                var rad = Grid.Radiation[Cell(8, 9)];
                Record("the reactor reacts and radiates", State().StartsWith("reacting") || smi!.IsInsideState(smi.sm.on), $"state {State()}, radiation above it {rad:G5}");
                Record("its radiation reaches owned cells", rad > 0 || Grid.Radiation[Cell(8, 8)] > 0, $"{rad:G5} rads");
                // The coolant's heat triggers the vent: put the reaction's coolant over its limit.
                foreach (var item in storages![1].items.Where(i => i != null && i.GetComponent<PrimaryElement>().ElementID == SimHashes.Water))
                    item.GetComponent<PrimaryElement>().Temperature = 700;
            }
            if (!meltdown)
            {
                if (Once(104, "waste"))
                    AccessTools.Field(typeof(Reactor), "spentFuel").SetValue(soReactor!.GetComponent<Reactor>(), 1.2f);
                if (Once(260, "check"))
                {
                    var added = SiteSpecies(SimHashes.Water) + SiteSpecies(SimHashes.Steam) - recorded["water"];
                    Record("the vented coolant reached the owner", added > 10, $"+{added:G5} kg water and steam; state {State()}");
                    var waste = SiteSpecies(SimHashes.NuclearWaste);
                    Record("the dumped nuclear waste reached the owner", waste > 50, $"{waste:G5} kg nuclear waste; reactor still holds {storages!.Sum(st => (double)st.GetMassAvailable(SimHashes.NuclearWaste)):G5} kg");
                    LedgersClosed("reactor");
                    Finish();
                }
                return;
            }
            // Meltdown: the fuel passes its limit; waste comets and nuclear waste fall over the owner.
            if (Once(106, "meltdown"))
            {
                foreach (var item in storages![1].items.Where(i => i != null && i.GetComponent<PrimaryElement>().ElementID == SimHashes.EnrichedUranium))
                    item.GetComponent<PrimaryElement>().Temperature = 3100;
                recorded["stored"] = storages.Sum(st => (double)st.MassStored());
            }
            if (Once(420, "check"))
            {
                var waste = SiteSpecies(SimHashes.NuclearWaste);
                Record("the meltdown began", State().StartsWith("meltdown") || State() == "dead", $"state {State()}");
                Record("meltdown nuclear waste reached the owner", waste > 1, $"{waste:G5} kg nuclear waste in the site; {recorded["stored"]:G5} kg was stored at the melt");
                LedgersClosed("meltdown");
                Finish();
            }
        }

        // ---- cluster rocket engines

        // Every cluster engine on its own launch pad in a sealed vacuum
        // compartment, put into the burning state as at launch: each one's
        // exhaust reaches the owner in its own compartment.
        private static readonly string[] soEngineIds = { CO2EngineConfig.ID, SugarEngineConfig.ID, HydrogenEngineClusterConfig.ID,
            KeroseneEngineClusterConfig.ID, KeroseneEngineClusterSmallConfig.ID, SteamEngineClusterConfig.ID, BiodieselEngineClusterConfig.ID };
        private static readonly GameObject?[] soEngines = new GameObject?[soEngineIds.Length];
        private static readonly GameObject?[] soPads = new GameObject?[soEngineIds.Length];
        private static double CompartmentSpecies(int k, SimHashes element) =>
            Enumerable.Range(11 * k, 10).Sum(x => Enumerable.Range(0, 24).Sum(y => SpeciesMass(Cell(x, y), element)));
        private static void SoEngines(int frame)
        {
            const int h = 24; var n = soEngineIds.Length;
            if (Once(0, "rocket sites"))
            {
                RequireSpacedOut(); pendingShift = 11 * n + 1 - 11; Array.Clear(soEngines, 0, n); Array.Clear(soPads, 0, n);
                Arena((x, y) => x % 11 == 10 ? (SimHashes.Unobtanium, 20000f, 300f) : (SimHashes.Vacuum, 0f, 0f), 11 * n - 1, h);
            }
            if (Once(8, "launch pads")) for (var k = 0; k < n; ++k) soPads[k] = Build(LaunchPadConfig.ID, Cell(11 * k + 4, 0));
            if (Once(12, "engines"))
                for (var k = 0; k < n; ++k) soEngines[k] = AddRocketModule(soPads[k]!, soEngineIds[k], true);
            if (Once(16, "ignite"))
                for (var k = 0; k < n; ++k)
                {
                    var burner = soEngines[k]!.GetComponent<RocketEngineCluster>();
                    var smi = burner.GetSMI<RocketEngineCluster.StatesInstance>();
                    recorded["exhaust" + k] = CompartmentSpecies(k, burner.exhaustElement) + (burner.exhaustElement == SimHashes.Steam ? CompartmentSpecies(k, SimHashes.Water) : 0);
                    smi.GoTo(smi.sm.burning);
                }
            if (Once(40, "check"))
            {
                for (var k = 0; k < n; ++k)
                {
                    var burner = soEngines[k]!.GetComponent<RocketEngineCluster>(); var element = burner.exhaustElement;
                    var added = CompartmentSpecies(k, element) + (element == SimHashes.Steam ? CompartmentSpecies(k, SimHashes.Water) : 0) - recorded["exhaust" + k];
                    var expected = burner.exhaustEmitRate * .2 * 24;
                    Record($"{soEngineIds[k]} exhaust reached the owner", added > expected * .5,
                        $"+{added:G5} kg {element} (about {expected:G4} kg emitted at {burner.exhaustEmitRate:G4} kg/s, {burner.exhaustTemperature:F0} K)");
                }
                LedgersClosed("engine exhaust");
                Finish();
            }
        }

        // ---- converters and recipes

        // A sublimation station turns toxic sand into polluted oxygen, and a
        // nuclear waste chunk sublimates liquid waste into the owner.
        private static void SoSublimation(int frame)
        {
            if (Once(0, "arena")) { RequireSpacedOut(); Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .3f, 300f)); }
            if (Once(6, "build"))
            {
                recorded["polluted"] = ArenaSpecies(SimHashes.ContaminatedOxygen); recorded["waste"] = ArenaSpecies(SimHashes.NuclearWaste);
                recorded["mass"] = ArenaMass();
                var station = Build(SublimationStationConfig.ID, Cell(2, 1)); Store(station, SimHashes.ToxicSand, 20);
                var chunk = ElementLoader.FindElementByHash(SimHashes.NuclearWaste).substance
                    .SpawnResource(Grid.CellToPosCCC(Cell(6, 1), Grid.SceneLayer.Ore), 50, 300, byte.MaxValue, 0);
                // The chunk accumulates 0.066 kg/s before it emits: start it past its 6.6 kg threshold.
                AccessTools.Field(typeof(Sublimates), "sublimatedMass").SetValue(chunk.GetComponent<Sublimates>(), 7f);
                soSeries.Clear(); soSeries.Add(MachineState(station));
            }
            if (Once(60, "check"))
            {
                var polluted = ArenaSpecies(SimHashes.ContaminatedOxygen) - recorded["polluted"]; var waste = ArenaSpecies(SimHashes.NuclearWaste) - recorded["waste"];
                Record("the station's polluted oxygen reached the owner", polluted > .05, $"+{polluted:G4} kg; {soSeries[0]}");
                Record("sublimated nuclear waste reached the owner", waste > .5, $"+{waste:G4} kg nuclear waste");
                LedgersClosed("sublimation");
                Finish();
            }
        }

        // A uranium centrifuge separates ore into enriched uranium and molten
        // uranium, which leaves through its liquid pipe and vent.
        private static void SoCentrifuge(int frame)
        {
            if (Once(0, "arena")) { RequireSpacedOut(); Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .3f, 300f)); }
            if (Once(6, "build"))
            {
                recorded["molten"] = ArenaSpecies(SimHashes.MoltenUranium); recorded["mass"] = ArenaMass();
                var fab = Build(UraniumCentrifugeConfig.ID, Cell(2, 1)); machine = fab;
                var def = fab.GetComponent<Building>().Def;
                var vent = Build(LiquidVentConfig.ID, Cell(6, 1));
                Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, Route(Grid.OffsetCell(Grid.PosToCell(fab), def.UtilityOutputOffset),
                    Grid.OffsetCell(Grid.PosToCell(vent), vent.GetComponent<Building>().Def.UtilityInputOffset)));
                fab.GetComponent<ComplexFabricator>().inStorage.AddOre(SimHashes.UraniumOre, 10, 300, byte.MaxValue, 0, false, true);
            }
            if (Once(8, "order"))
            {
                var fab = machine!.GetComponent<ComplexFabricator>();
                fab.SetRecipeQueueCount(fab.GetRecipes()[0], 1);
            }
            if (Once(320, "check"))
            {
                var molten = ArenaSpecies(SimHashes.MoltenUranium) - recorded["molten"];
                var fab = machine!.GetComponent<ComplexFabricator>();
                Record("molten uranium reached the owner", molten > 1, $"+{molten:G4} kg molten uranium (8 kg per order); out storage {fab.outStorage.MassStored():G4} kg, in storage {fab.inStorage.MassStored():G4} kg; {MachineState(machine)}");
                LedgersClosed("centrifuge");
                Finish();
            }
        }

        // A sludge press (worked by a duplicant, whose work the scenario
        // completes) splits mud into dirt and water, and toxic mud into toxic
        // sand and polluted water; the liquid leaves through its pipe.
        private static void SoSludgePress(int frame)
        {
            if (Once(0, "arena")) { RequireSpacedOut(); Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .3f, 300f), 12, 6); }
            if (Once(6, "build"))
            {
                recorded["water"] = SiteSpecies(SimHashes.Water); recorded["dirty"] = SiteSpecies(SimHashes.DirtyWater); recorded["mass"] = SiteMass();
                var press = Build(SludgePressConfig.ID, Cell(2, 1)); machine = press;
                var def = press.GetComponent<Building>().Def;
                var vent = Build(LiquidVentConfig.ID, Cell(9, 1));
                Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, Route(Grid.OffsetCell(Grid.PosToCell(press), def.UtilityOutputOffset),
                    Grid.OffsetCell(Grid.PosToCell(vent), vent.GetComponent<Building>().Def.UtilityInputOffset)));
                var fab = press.GetComponent<ComplexFabricator>();
                fab.inStorage.AddOre(SimHashes.Mud, 150, 300, byte.MaxValue, 0, false, true);
                fab.inStorage.AddOre(SimHashes.ToxicMud, 150, 300, byte.MaxValue, 0, false, true);
            }
            if (Once(8, "orders"))
            {
                var fab = machine!.GetComponent<ComplexFabricator>();
                foreach (var tag in new[] { SimHashes.Mud.CreateTag(), SimHashes.ToxicMud.CreateTag() })
                    fab.SetRecipeQueueCount(fab.GetRecipes().First(r => r.ingredients.Length == 1 && r.ingredients[0].material == tag), 1);
            }
            // A duplicant's work would fill the order's progress; the order then completes on its own.
            foreach (var at in new[] { 14, 40 })
                if (Once(at, "complete order " + at))
                {
                    var fab = machine!.GetComponent<ComplexFabricator>();
                    Debug.Log("[Mixed Atmospheres] sludge press order at " + at + ": working " + HasOrder(fab) + ", in " + fab.inStorage.MassStored() + " kg, build " + fab.buildStorage.MassStored() + " kg, out " + fab.outStorage.MassStored() + " kg");
                    if (HasOrder(fab)) { AccessTools.Field(typeof(ComplexFabricator), "orderProgress").SetValue(fab, 1f); fab.CompleteWorkingOrder(); }
                    else Record("order " + at + " started", false, "no working order");
                }
            if (Once(200, "check"))
            {
                var water = SiteSpecies(SimHashes.Water) - recorded["water"]; var dirty = SiteSpecies(SimHashes.DirtyWater) - recorded["dirty"];
                var fab = machine!.GetComponent<ComplexFabricator>();
                Record("the mud's water reached the owner", water > 5, $"+{water:G4} kg water; out storage {fab.outStorage.MassStored():G4} kg");
                Record("the toxic mud's polluted water reached the owner", dirty > 5, $"+{dirty:G4} kg polluted water");
                LedgersClosed("sludge press");
                Finish();
            }
        }

        // Buildings whose conduit intake dumps or stores the wrong element:
        // a mixed gas pipe feeds an enclosed telescope and a lead suit locker
        // (both dump what they cannot hold into their cell), and a mixed
        // liquid pipe feeds a decontamination shower (which keeps it).
        private static void SoIntakes(int frame)
        {
            const int w = 30, h = 10;
            if (Once(0, "arena"))
            {
                RequireSpacedOut(); pendingShift = w + 2 - 11;
                Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .3f, 300f), w, h);
            }
            if (Once(6, "build"))
            {
                recorded["co2"] = SiteSpecies(SimHashes.CarbonDioxide); recorded["dirty"] = SiteSpecies(SimHashes.DirtyWater); recorded["mass"] = SiteMass();
                var telescope = Build(ClusterTelescopeEnclosedConfig.ID, Cell(4, 1));
                var locker = Build(LeadSuitLockerConfig.ID, Cell(13, 1));
                var shower = Build(DecontaminationShowerConfig.ID, Cell(22, 1));
                soIntakes = new[] { telescope, locker, shower };
                foreach (var b in soIntakes)
                {
                    var def = b.GetComponent<Building>().Def;
                    var cell = b.GetComponent<Building>().GetUtilityInputCell();
                    Pipe(def.InputConduitType == ConduitType.Gas ? Game.Instance.gasConduitSystem : Game.Instance.liquidConduitSystem,
                        def.InputConduitType == ConduitType.Gas ? GasConduitConfig.ID : LiquidConduitConfig.ID, new List<int> { cell });
                    soIntakeCells.Add(cell);
                }
            }
            if (Once(12, "seed"))
            {
                recorded["seeded"] = 0;
                for (var i = 0; i < 2; ++i)
                {
                    var flow = Game.Instance.gasConduitFlow;
                    ColonyConduits.Seed(flow, soIntakeCells[i], SimHashes.Oxygen, .75f, 300, byte.MaxValue, 0);
                    ColonyConduits.Seed(flow, soIntakeCells[i], SimHashes.CarbonDioxide, .25f, 300, byte.MaxValue, 0);
                }
                ColonyConduits.Seed(Game.Instance.liquidConduitFlow, soIntakeCells[2], SimHashes.Water, .75f, 300, byte.MaxValue, 0);
                ColonyConduits.Seed(Game.Instance.liquidConduitFlow, soIntakeCells[2], SimHashes.DirtyWater, .25f, 300, byte.MaxValue, 0);
            }
            if (Once(100, "check"))
            {
                var co2 = SiteSpecies(SimHashes.CarbonDioxide) - recorded["co2"];
                var held = soIntakes.Select(b => b.GetComponents<Storage>().Sum(st => st.MassStored())).ToArray();
                Record("the telescope and locker took oxygen and dumped the carbon dioxide", co2 > .2 && co2 < .5 + 1e-6 && held[0] > .5 && held[1] > .5,
                    $"+{co2:G4} kg carbon dioxide dumped (of 0.5 piped); telescope holds {held[0]:G4} kg, locker {held[1]:G4} kg");
                var shower = soIntakes[2].GetComponents<Storage>();
                Record("the shower keeps both liquids", held[2] > .9 && shower.Sum(st => st.GetMassAvailable(SimHashes.DirtyWater)) > .2,
                    $"holds {held[2]:G4} kg, polluted water {shower.Sum(st => st.GetMassAvailable(SimHashes.DirtyWater)):G4} kg");
                Record("intake pipes drained", soIntakeCells.All(c => ColonyConduits.ReadSynced(c == soIntakeCells[2] ? Game.Instance.liquidConduitFlow : Game.Instance.gasConduitFlow, c).Mass < .01),
                    string.Join(", ", soIntakeCells.Select(c => ColonyConduits.ReadSynced(c == soIntakeCells[2] ? Game.Instance.liquidConduitFlow : Game.Instance.gasConduitFlow, c).Mass.ToString("G3"))));
                Record("intake pipe accounts exact", ColonyConduits.Audit(out var detail), detail);
                LedgersClosed("intakes");
                Finish();
            }
        }
        private static bool HasOrder(ComplexFabricator fab) => (int)AccessTools.Field(typeof(ComplexFabricator), "workingOrderIdx").GetValue(fab) >= 0;
        private static GameObject[] soIntakes = Array.Empty<GameObject>();
        private static readonly List<int> soIntakeCells = new List<int>();

        // A food dehydrator's byproduct water, dropped as the game's duplicants
        // empty it, falls into owned cells.
        private static void SoDehydrator(int frame)
        {
            if (Once(0, "arena")) { RequireSpacedOut(); Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .3f, 300f)); }
            if (Once(6, "build"))
            {
                recorded["water"] = ArenaSpecies(SimHashes.Water) + ArenaSpecies(SimHashes.Steam); recorded["mass"] = ArenaMass();
                machine = Build(FoodDehydratorConfig.ID, Cell(3, 1));
            }
            if (Once(8, "dump"))
            {
                var fab = machine!.GetComponent<ComplexFabricator>();
                fab.outStorage.AddLiquid(SimHashes.Water, 6, 348, byte.MaxValue, 0, false, true);
                fab.outStorage.DropAll(Grid.CellToPos(Grid.PosToCell(machine)), vent_gas: false, dump_liquid: true);
            }
            if (Once(60, "check"))
            {
                var water = ArenaSpecies(SimHashes.Water) + ArenaSpecies(SimHashes.Steam) - recorded["water"];
                Record("the dehydrator's water reached the owner", water > 5, $"+{water:G4} kg of 6 dumped");
                LedgersClosed("dehydrator");
                Finish();
            }
        }
    }
}
