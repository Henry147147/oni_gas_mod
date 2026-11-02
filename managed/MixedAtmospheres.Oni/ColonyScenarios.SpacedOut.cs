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
                case "spaced-out-landers": SoLanders(frame); return true;
                case "spaced-out-poi-harvest": SoPoiHarvest(frame); return true;
                case "spaced-out-rail-gun": SoRailGun(frame); return true;
                case "spaced-out-comets-geysers": SoCometsGeysers(frame); return true;
                case "spaced-out-vacuum-pocket": SoVacuumPocket(frame); return true;
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
        // Debris items of one element resting in the current site.
        private static double SiteDebris(SimHashes element) => Components.Pickupables.Items
            .Where(item => item != null && !item.wasAbsorbed && !item.GetComponent<KPrefabID>().pendingDestruction &&
                           item.GetComponent<PrimaryElement>() is PrimaryElement e && e.ElementID == element)
            .Where(item => { Grid.CellToXY(Grid.PosToCell(item), out var x, out var y); return x >= x0 && x < x0 + fillWidth && y >= y0 && y < y0 + fillHeight; })
            .Sum(item => (double)item.GetComponent<PrimaryElement>().Mass);
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
        private static readonly List<(int count, float rads)> soEdge = new List<(int, float)>();
        private static readonly List<int> soControl = new List<int>();
        private static void SoRadiation(int frame)
        {
            var slime = Db.Get().Diseases.SlimeGerms;
            var beam = new[] { (2, 3), (4, 3), (6, 3) };
            if (Once(0, "arena")) { RequireSpacedOut(); soSeries.Clear(); soEdge.Clear(); soControl.Clear(); Arena((x, y) => (SimHashes.ContaminatedOxygen, 1f, 300f)); }
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
                soEdge.Add((Grid.DiseaseCount[Cell(7, 0)], Grid.Radiation[Cell(7, 0)])); soControl.Add(Grid.DiseaseCount[Cell(0, 0)]);
                soSeries.Add($"f{frame}: " + string.Join(" ", cells.Select(c => $"({c.Item1},{c.Item2}) n={Grid.DiseaseCount[Cell(c.Item1, c.Item2)]} i={Grid.DiseaseIdx[Cell(c.Item1, c.Item2)]} r={Grid.Radiation[Cell(c.Item1, c.Item2)]:G6}")));
            }
            if (Once(84, "check"))
            {
                var rate = slime.radiationKillRate;
                // The game's own tick: germs fall by radiation x kill rate (germs grow a little, and a cell holding fewer than the kill empties).
                var worst = 0.0; var steps = 0;
                for (var k = 1; k < soEdge.Count; ++k)
                {
                    if (soEdge[k - 1].count <= 0) continue;
                    var predicted = Math.Max(0, soEdge[k - 1].count - soEdge[k - 1].rads * rate);
                    worst = Math.Max(worst, Math.Abs(soEdge[k].count - predicted)); ++steps;
                }
                Record("germs in an irradiated cell fall by radiation x kill rate each tick", steps >= 3 && worst <= 6 && soEdge.Last().count == 0,
                    $"kill rate {rate:G3}; edge cell counts {string.Join(", ", soEdge.Select(e => $"{e.count} at {e.rads:F1} rads"))}; largest step deviation {worst:G3} germs over {steps} steps");
                var seeded = soControl.SkipWhile(c => c == 0).ToList();
                Record("germs without radiation keep to their ordinary rule", seeded.Count > 4 && seeded.All(c => Math.Abs(c - recorded["germs"]) <= 6), $"control cell {string.Join(", ", soControl)}");
                Record("heavily irradiated cells emptied of germs", beam.All(c => Grid.DiseaseCount[Cell(c.Item1, c.Item2)] == 0), string.Join(", ", beam.Select(c => $"({c.Item1},{c.Item2}) {Grid.DiseaseCount[Cell(c.Item1, c.Item2)]}")));
                Debug.Log("[Mixed Atmospheres] so-radiation germ series (killRate " + slime.radiationKillRate + ", seeded " + recorded["germs"] + "):\n" + string.Join("\n", soSeries));
                if (ColonyWorldGerms.TryRead(out var germs, out _) && germs.Mode == 1)
                    Record("germ accounts close under the radiation kill", germs.AuditError < 1e-9, $"audit {germs.AuditError:G3}, edited out {germs.EditedOut:G6}, radiation fields handed over {ColonyRadiationGerms.Fields} ({ColonyRadiationGerms.Cells} cells)");
                Record("no fluid created or lost", Math.Abs(ArenaMass() - recorded["mass"]) < 1e-6 * recorded["mass"], $"{recorded["mass"]:G9} -> {ArenaMass():G9} kg");
                LedgersClosed("radiation");
                Finish();
            }
        }

        // Two radbolts fly towards each other through owned oxygen and meet:
        // their summed payload arrives as hot fallout in an owned cell and the
        // oxygen is intact. A third bolt strikes a granite wall: the cells on
        // its path get radiation sickness, and where its payload goes is
        // recorded (ONI's own simulation handles a gas added to a solid).
        private static void SoRadbolt(int frame)
        {
            if (Once(0, "arena")) { RequireSpacedOut(); Arena((x, y) => y == 5 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1.2f, 300f)); }
            if (Once(6, "bolts"))
            {
                recorded["oxygen"] = ArenaSpecies(SimHashes.Oxygen); recorded["fallout"] = ArenaSpecies(SimHashes.Fallout) + ArenaSpecies(SimHashes.NuclearWaste);
                foreach (var (x, y, direction) in new[] { (0, 2, EightDirection.Right), (7, 2, EightDirection.Left), (0, 4, EightDirection.Up) })
                {
                    var bolt = GameUtil.KInstantiate(Assets.GetPrefab(HighEnergyParticleConfig.ID), Grid.CellToPosCCC(Cell(x, y), Grid.SceneLayer.FXFront2), Grid.SceneLayer.FXFront2);
                    bolt.SetActive(true);
                    var particle = bolt.GetComponent<HighEnergyParticle>();
                    particle.payload = 100; particle.SetDirection(direction);
                }
            }
            if (Once(100, "check"))
            {
                var oxygen = ArenaSpecies(SimHashes.Oxygen) - recorded["oxygen"]; var fallout = ArenaSpecies(SimHashes.Fallout) + ArenaSpecies(SimHashes.NuclearWaste) - recorded["fallout"];
                var sick = Db.Get().Diseases.GetIndex(Db.Get().Diseases.RadiationPoisoning.Id);
                var germ = Enumerable.Range(0, 8).Count(x => Grid.DiseaseIdx[Cell(x, 2)] == sick && Grid.DiseaseCount[Cell(x, 2)] > 0);
                var colliding = (200 - 0.1 * 7) * .001;
                Record("the colliding bolts' payload arrived as fallout", fallout > .8 * colliding && fallout < .1001 + colliding,
                    $"+{fallout:G6} kg fallout and nuclear waste (hot fallout condenses to waste) of about {colliding:G4} from the collision, 0.1 more at most if the wall bolt's reached the owner; fallout {ArenaSpecies(SimHashes.Fallout):G4}, waste {ArenaSpecies(SimHashes.NuclearWaste):G4}");
                Record("the owned oxygen is intact", Math.Abs(oxygen) < 1e-6, $"{oxygen:+0.0e+0;-0.0e+0} kg");
                Record("radiation sickness marked the bolts' path", germ > 0, $"{germ} of 8 cells in the colliding row hold radiation sickness");
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
            if (Once(70, "radiating"))
            {
                var rad = Grid.Radiation[Cell(8, 9)];
                var emitter = soReactor!.GetComponent<RadiationEmitter>();
                var field = string.Join(" ", new[] { (8, 7), (8, 9), (8, 12), (3, 9), (13, 9), (8, 3), (1, 5) }.Select(c => $"({c.Item1},{c.Item2}) {Grid.Radiation[Cell(c.Item1, c.Item2)]:G4}"));
                Record("the reactor reacts and radiates", smi!.IsInsideState(smi.sm.on) && (rad > 0 || Grid.Radiation[Cell(8, 8)] > 0),
                    $"state {State()}, radiation above it {rad:G5} rads; emitter {emitter.emitRads:G5} rads over {emitter.emitRadiusX}x{emitter.emitRadiusY} at cell {emitter.GetEmissionCell()}, sim handle {AccessTools.Field(typeof(SimComponent), "simHandle").GetValue(emitter)}; field {field}");
            }
            if (Once(100, "reacting"))
            {
                var coolant = storages!.Sum(st => (double)st.GetMassAvailable(SimHashes.Water));
                Record("coolant reached the reactor through its conduit", coolant >= 30 && recorded.GetValueOrDefault("seeded") >= 30,
                    $"{recorded.GetValueOrDefault("seeded"):G5} kg piped in, reactor holds {coolant:G5} kg water; state {State()}");
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
                var melted = 10 + recorded["stored"]; var fallout = SiteSpecies(SimHashes.Fallout); var debris = SiteDebris(SimHashes.NuclearWaste) + SiteDebris(SimHashes.Corium);
                // The comets of corium it throws land as solid corium tiles.
                for (var y = 0; y < fillHeight; ++y) for (var x = 0; x < fillWidth; ++x)
                    if (Grid.Element[Cell(x, y)].id == SimHashes.Corium) debris += Grid.Mass[Cell(x, y)];
                Record("the meltdown ran its course", State().EndsWith("dead") || State().Contains("meltdown"), $"state {State()}");
                var comet = Assets.GetPrefab(NuclearWasteCometConfig.ID).GetComponent<Comet>();
                // Part of the melted mass leaves as comets of corium (3 kg a half second), which land as tiles of the game's own tile mass; the rest falls as liquid waste.
                Record("the meltdown's nuclear waste reached the owner", waste + fallout > .25 * melted && waste + fallout + debris > .5 * melted && waste + fallout + debris < 1.05 * melted,
                    $"{waste:G5} kg nuclear waste + {fallout:G5} kg fallout in the site + {debris:G5} kg corium and waste debris of {melted:G5} kg melted (10 kg plus {recorded["stored"]:G5} kg stored, water and fuel included); comets of {comet.massRange.x:G3} kg land as tiles of {comet.AddTileMass:G3} kg");
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
                    .SpawnResource(Grid.CellToPosCCC(Cell(6, 1), Grid.SceneLayer.Ore), 50, 400, byte.MaxValue, 0);
                // The chunk accumulates 0.066 kg/s before it emits: start it past its 6.6 kg threshold.
                AccessTools.Field(typeof(Sublimates), "sublimatedMass").SetValue(chunk.GetComponent<Sublimates>(), 7f);
                soSeries.Clear(); soSeries.Add(MachineState(station)); machine = station;
            }
            if (Once(30, "state")) soSeries.Add(MachineState(machine));
            if (Once(60, "check"))
            {
                var polluted = ArenaSpecies(SimHashes.ContaminatedOxygen) - recorded["polluted"]; var waste = ArenaSpecies(SimHashes.NuclearWaste) - recorded["waste"];
                Record("the station's polluted oxygen reached the owner", polluted > .05, $"+{polluted:G4} kg; at build [{soSeries[0]}]; at 30 [{soSeries[1]}]; now [{MachineState(machine)}]");
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
                var path = Route(Grid.OffsetCell(Grid.PosToCell(fab), def.UtilityOutputOffset), Grid.OffsetCell(Grid.PosToCell(vent), vent.GetComponent<Building>().Def.UtilityInputOffset));
                Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, path); pipeCells.AddRange(path);
            }
            // The ordered recipe must exist before its ingredients: a fabricator drops stored material no order needs.
            if (Once(8, "order"))
            {
                var fab = machine!.GetComponent<ComplexFabricator>();
                fab.SetRecipeQueueCount(fab.GetRecipes()[0], 1);
            }
            if (Once(9, "ore")) machine!.GetComponent<ComplexFabricator>().inStorage.AddOre(SimHashes.UraniumOre, 10, 300, byte.MaxValue, 0, false, true);
            if (Once(320, "check"))
            {
                var molten = ArenaSpecies(SimHashes.MoltenUranium) - recorded["molten"];
                var frozen = ElementLoader.FindElementByHash(SimHashes.MoltenUranium).lowTempTransitionTarget;
                var solid = SiteDebris(frozen); var inPipe = pipeCells.Sum(c => (double)Game.Instance.liquidConduitFlow.GetContents(c).mass);
                var fab = machine!.GetComponent<ComplexFabricator>();
                Record("the molten uranium reached the owner", molten + solid + inPipe > 7.5 && molten + solid + inPipe < 8.5,
                    $"{molten:G4} kg liquid + {solid:G4} kg frozen {frozen} debris + {inPipe:G4} kg in pipe of 8 kg per order; out storage {fab.outStorage.MassStored():G4} kg, in storage {fab.inStorage.MassStored():G4} kg; {MachineState(machine)}");
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
            }
            // The ordered recipes must exist before their ingredients: a fabricator drops stored material no order needs.
            if (Once(8, "orders"))
            {
                var fab = machine!.GetComponent<ComplexFabricator>();
                var recipes = fab.GetRecipes();
                Debug.Log("[Mixed Atmospheres] sludge press recipes: " + recipes.Length + " e.g. " + string.Join(", ", recipes.Take(6).Select(r => r.ingredients[0].material + "->" + string.Join("+", r.results.Select(x => x.material + " " + x.amount)))));
                foreach (var tag in new[] { SimHashes.Mud.CreateTag(), SimHashes.ToxicMud.CreateTag() })
                    fab.SetRecipeQueueCount(recipes.First(r => r.ingredients.Length == 1 && r.ingredients[0].material == tag), 1);
            }
            if (Once(9, "mud"))
            {
                var fab = machine!.GetComponent<ComplexFabricator>();
                fab.inStorage.AddOre(SimHashes.Mud, 150, 300, byte.MaxValue, 0, false, true);
                fab.inStorage.AddOre(SimHashes.ToxicMud, 150, 300, byte.MaxValue, 0, false, true);
            }
            // A duplicant's work would fill the order's progress; the order then completes on its own.
            foreach (var at in new[] { 30, 80 })
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
                var where = string.Join("; ", soIntakes.Select(b =>
                {
                    var c = Grid.PosToCell(b); Grid.CellToXY(c, out var cx, out var cy);
                    return $"{b.name}@{cx - x0},{cy - y0} solid {Grid.Solid[c]} owned {ColonyOwner.OwnsFluidCell(c)} co2 {SpeciesMass(c, SimHashes.CarbonDioxide):G3} kg";
                }));
                Record("the telescope and locker took oxygen and dumped the carbon dioxide", co2 > .2 && co2 < .5 + 1e-6 && held[0] > .5 && held[1] > .5,
                    $"+{co2:G4} kg carbon dioxide dumped (of 0.5 piped); telescope holds {held[0]:G4} kg, locker {held[1]:G4} kg; {where}");
                var shower = soIntakes[2].GetComponents<Storage>();
                var dirty = SiteSpecies(SimHashes.DirtyWater) - recorded["dirty"] + shower.Sum(st => st.GetMassAvailable(SimHashes.DirtyWater));
                Record("the shower took the water and passed on the polluted water", shower.Sum(st => st.GetMassAvailable(SimHashes.Water)) > .7 && Math.Abs(dirty - .25) < 1e-3,
                    $"holds {shower.Sum(st => st.GetMassAvailable(SimHashes.Water)):G4} kg water; polluted water {dirty:R} kg between storage ({shower.Sum(st => st.GetMassAvailable(SimHashes.DirtyWater)):G4}) and the site");
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

        // ---- landers, space harvesting and the rail gun

        // A scout lander and a landing pod descend through owned vacuum, each in
        // its own compartment, emitting exhaust on the way.
        private static GameObject? soScout, soPod;
        private static void SoLanders(int frame)
        {
            const int h = 24;
            if (Once(0, "arena"))
            {
                RequireSpacedOut();
                Arena((x, y) => x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Vacuum, 0f, 0f), 9, h);
            }
            double Left(SimHashes e) => Enumerable.Range(0, 4).Sum(x => Enumerable.Range(0, h).Sum(y => SpeciesMass(Cell(x, y), e)));
            double Right(SimHashes e) => Enumerable.Range(5, 4).Sum(x => Enumerable.Range(0, h).Sum(y => SpeciesMass(Cell(x, y), e)));
            if (Once(6, "land"))
            {
                recorded["scout co2"] = Left(SimHashes.CarbonDioxide); recorded["pod co2"] = Right(SimHashes.CarbonDioxide);
                soScout = GameUtil.KInstantiate(Assets.GetPrefab(ScoutLanderConfig.ID), Grid.CellToPosCBC(Cell(2, 1), Grid.SceneLayer.Front), Grid.SceneLayer.Front);
                soScout.SetActive(true);
                soPod = GameUtil.KInstantiate(Assets.GetPrefab(LandingPodConfig.ID), Grid.CellToPosCBC(Cell(7, 1), Grid.SceneLayer.Front), Grid.SceneLayer.Front);
                soPod.SetActive(true);
            }
            if (Once(8, "descent height"))
            {
                soScout!.GetSMI<CargoLander.StatesInstance>().flightAnimOffset = h - 4;
                AccessTools.Field(typeof(PodLander), "flightAnimOffset").SetValue(soPod!.GetComponent<PodLander>(), (float)h - 4);
            }
            if (Once(120, "check"))
            {
                var scout = soScout!.GetSMI<CargoLander.StatesInstance>();
                var landed = scout.GetCurrentState()?.name ?? "?";
                Record("the lander landed", landed.Contains("grounded") || landed.EndsWith("land"), $"lander state {landed}, offset {scout.flightAnimOffset:G3}");
                Record("the lander's exhaust reached the owner", Left(SimHashes.CarbonDioxide) - recorded["scout co2"] > 1, $"+{Left(SimHashes.CarbonDioxide) - recorded["scout co2"]:G4} kg carbon dioxide");
                Record("the pod's exhaust reached the owner", Right(SimHashes.CarbonDioxide) - recorded["pod co2"] > 1, $"+{Right(SimHashes.CarbonDioxide) - recorded["pod co2"]:G4} kg carbon dioxide");
                LedgersClosed("landers");
                Finish();
            }
        }

        // Gas and liquid mined from a space point of interest land in real
        // cargo bays through the game's hex-cell inventory, as the bays'
        // collectors extract them in flight.
        private static void SoPoiHarvest(int frame)
        {
            if (Once(0, "rocket site"))
            {
                RequireSpacedOut(); pendingShift = 12 - 11; clusterLaunchPad = clusterEngine = clusterCapsule = null;
                Arena((x, y) => (SimHashes.Vacuum, 0f, 0f), 10, 24);
            }
            if (Once(8, "launch pad")) clusterLaunchPad = Build(LaunchPadConfig.ID, Cell(4, 0));
            if (Once(12, "engine")) clusterEngine = AddRocketModule(clusterLaunchPad!, CO2EngineConfig.ID, true);
            if (Once(16, "bays")) { soGasBay = AddRocketModule(clusterEngine!, GasCargoBaySmallConfig.ID, false); soLiquidBay = AddRocketModule(soGasBay!, LiquidCargoBaySmallConfig.ID, false); }
            if (Once(22, "harvest"))
            {
                var craft = clusterEngine!.GetComponent<RocketModuleCluster>().CraftInterface.GetComponent<Clustercraft>();
                var inventory = ClusterGrid.Instance.AddOrGetHexCellInventory(craft.Location);
                soGasBay!.GetComponent<TreeFilterable>().UpdateFilters(new HashSet<Tag> { SimHashes.Oxygen.CreateTag(), SimHashes.CarbonDioxide.CreateTag() });
                soLiquidBay!.GetComponent<TreeFilterable>().UpdateFilters(new HashSet<Tag> { SimHashes.Water.CreateTag(), SimHashes.DirtyWater.CreateTag() });
                var gasStorage = soGasBay!.GetComponent<Storage>(); var liquidStorage = soLiquidBay.GetComponent<Storage>();
                foreach (var (element, mass, storage) in new[] { (SimHashes.Oxygen, .75f, gasStorage), (SimHashes.CarbonDioxide, .25f, gasStorage),
                    (SimHashes.Water, .75f, liquidStorage), (SimHashes.DirtyWater, .25f, liquidStorage) })
                {
                    var tag = element.CreateTag();
                    inventory.AddItem(ElementLoader.FindElementByHash(element), mass);
                    var taken = inventory.ExtractAndStoreItemMass(tag, mass, storage);
                    Record("bay stored mined " + element, Math.Abs(taken - mass) < 1e-6 && Math.Abs(ClusterStoredMass(storage, element) - mass) < 1e-4,
                        $"extracted {taken:R} of {mass:R} kg; bay holds {ClusterStoredMass(storage, element):R} kg");
                }
            }
            if (Once(60, "check"))
            {
                Record("the bays keep their harvest", ClusterStoredMass(soGasBay!.GetComponent<Storage>(), SimHashes.Oxygen) > .74 && ClusterStoredMass(soLiquidBay!.GetComponent<Storage>(), SimHashes.Water) > .74,
                    $"gas bay {soGasBay!.GetComponent<Storage>().MassStored():R} kg, liquid bay {soLiquidBay!.GetComponent<Storage>().MassStored():R} kg");
                Record("pipe accounts exact", ColonyConduits.Audit(out var detail), detail);
                LedgersClosed("harvest");
                Finish();
            }
        }
        private static GameObject? soGasBay, soLiquidBay;

        // A rail gun takes mixed gas and liquid through its secondary input
        // ports into its storage, and a payload opener releases a payload's
        // gas and liquid through its secondary output ports.
        private static GameObject? soRailGun, soOpener;
        private static void SoRailGun(int frame)
        {
            const int w = 30, h = 10;
            if (Once(0, "arena"))
            {
                RequireSpacedOut(); pendingShift = w + 2 - 11;
                Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Vacuum, 0f, 0f), w, h);
            }
            if (Once(6, "build"))
            {
                recorded["mass"] = SiteMass();
                soRailGun = Build(RailGunConfig.ID, Cell(5, 1));
                soOpener = Build(RailGunPayloadOpenerConfig.ID, Cell(20, 1));
            }
            if (Once(8, "ports"))
            {
                var gun = soRailGun!.GetComponent<RailGun>(); var opener = soOpener!.GetComponent<RailGunPayloadOpener>();
                soGunGas = Grid.OffsetCell(Grid.PosToCell(soRailGun), gun.gasPortInfo.offset); soGunLiquid = Grid.OffsetCell(Grid.PosToCell(soRailGun), gun.liquidPortInfo.offset);
                soOpenGas = Grid.OffsetCell(Grid.PosToCell(soOpener), opener.gasPortInfo.offset); soOpenLiquid = Grid.OffsetCell(Grid.PosToCell(soOpener), opener.liquidPortInfo.offset);
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, new List<int> { soGunGas }, new List<int> { soOpenGas });
                Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, new List<int> { soGunLiquid }, new List<int> { soOpenLiquid });
                Pipe(Game.Instance.solidConduitSystem, SolidConduitConfig.ID, new List<int> { Grid.OffsetCell(Grid.PosToCell(soOpener), opener.solidPortInfo.offset) });
                // The gun launches only with a clear sky and a destination, neither of
                // which a sealed fixture has: its ports then take material as the
                // game's always-consuming conduit consumers do.
                foreach (var consumer in soRailGun.GetComponents<ConduitConsumer>()) consumer.alwaysConsume = true;
                Debug.Log($"[Mixed Atmospheres] rail gun ports gas {soGunGas} liquid {soGunLiquid}; opener gas {soOpenGas} liquid {soOpenLiquid}");
            }
            if (Once(14, "feed"))
            {
                var gas = Game.Instance.gasConduitFlow; var liquid = Game.Instance.liquidConduitFlow;
                ColonyConduits.Seed(gas, soGunGas, SimHashes.Oxygen, .375f, 300, byte.MaxValue, 0);
                ColonyConduits.Seed(gas, soGunGas, SimHashes.CarbonDioxide, .125f, 300, byte.MaxValue, 0);
                ColonyConduits.Seed(liquid, soGunLiquid, SimHashes.Water, .75f, 300, byte.MaxValue, 0);
                ColonyConduits.Seed(liquid, soGunLiquid, SimHashes.DirtyWater, .25f, 300, byte.MaxValue, 0);
            }
            if (Once(80, "gun check"))
            {
                var storage = soRailGun!.GetComponent<RailGun>().resourceStorage;
                var o2 = ClusterStoredMass(storage, SimHashes.Oxygen); var co2 = ClusterStoredMass(storage, SimHashes.CarbonDioxide);
                var water = ClusterStoredMass(storage, SimHashes.Water); var dirty = ClusterStoredMass(storage, SimHashes.DirtyWater);
                Record("the rail gun stored both gases and both liquids", Math.Abs(o2 - .375) < 1e-4 && Math.Abs(co2 - .125) < 1e-4 && Math.Abs(water - .75) < 1e-4 && Math.Abs(dirty - .25) < 1e-4,
                    $"oxygen {o2:R}, carbon dioxide {co2:R}, water {water:R}, polluted water {dirty:R} kg; {MachineState(soRailGun)}");
                // A payload as the gun would have launched it, holding the same mixtures.
                var payload = Util.KInstantiate(Assets.GetPrefab("RailGunPayload"), Grid.CellToPosCBC(Cell(18, 4), Grid.SceneLayer.Front));
                payload.SetActive(true);
                var held = payload.GetComponent<Storage>();
                held.AddGasChunk(SimHashes.Oxygen, .375f, 300, byte.MaxValue, 0, false, true); held.AddGasChunk(SimHashes.CarbonDioxide, .125f, 300, byte.MaxValue, 0, false, true);
                held.AddLiquid(SimHashes.Water, .75f, 300, byte.MaxValue, 0, false, true); held.AddLiquid(SimHashes.DirtyWater, .25f, 300, byte.MaxValue, 0, false, true);
                soOpener!.GetComponent<RailGunPayloadOpener>().payloadStorage.Store(payload, true, false);
            }
            if (Once(260, "opener check"))
            {
                var opener = soOpener!.GetComponent<RailGunPayloadOpener>();
                var gas = ColonyConduits.ReadSynced(Game.Instance.gasConduitFlow, soOpenGas); var liquid = ColonyConduits.ReadSynced(Game.Instance.liquidConduitFlow, soOpenLiquid);
                var held = opener.resourceStorage.MassStored();
                Record("the opener released the payload's gas and liquid to its ports", opener.payloadStorage.items.Count == 0 && gas.Mass > .45 && liquid.Mass > .95 && held < .01,
                    $"output pipes hold gas {gas.Mass:G4} kg, liquid {liquid.Mass:G4} kg; opener storage {held:G4} kg, payloads waiting {opener.payloadStorage.items.Count}; {MachineState(soOpener)}");
                Record("rail gun pipe accounts exact", ColonyConduits.Audit(out var detail), detail);
                LedgersClosed("rail gun");
                Finish();
            }
        }
        private static int soGunGas, soGunLiquid, soOpenGas, soOpenLiquid;

        // Spaced Out! comets (oxylite, snowball, nuclear waste) strike the floor
        // of owned oxygen in their own compartments and its liquid-metal
        // geysers erupt into owned oxygen: matter lands in owned cells or as
        // debris and tiles, with the owner's ledgers closed.
        private static readonly string[] soComets = { OxyliteCometConfig.ID, SnowballCometConfig.ID, NuclearWasteCometConfig.ID };
        private static readonly string[] soGeysers = { GeyserGenericConfig.MoltenAluminum, GeyserGenericConfig.MoltenTungsten, GeyserGenericConfig.MoltenNiobium };
        private static readonly List<GameObject> soGeyserObjects = new List<GameObject>();
        private static void SoCometsGeysers(int frame)
        {
            const int h = 20, pitch = 9; var n = soComets.Length + soGeysers.Length;
            if (Once(0, "compartments"))
            {
                RequireSpacedOut(); pendingShift = pitch * n + 1 - 11; soGeyserObjects.Clear();
                Arena((x, y) => x % pitch == pitch - 1 ? (SimHashes.Unobtanium, 20000f, 300f) : y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .3f, 300f), pitch * n - 1, h);
            }
            double Mass(int k) => Enumerable.Range(pitch * k, pitch - 1).Sum(x => Enumerable.Range(1, h - 1).Sum(y => CellMass(Cell(x, y))));
            if (Once(6, "strike"))
            {
                for (var k = 0; k < n; ++k) recorded["mass" + k] = Mass(k);
                for (var k = 0; k < soComets.Length; ++k)
                {
                    var comet = GameUtil.KInstantiate(Assets.GetPrefab(soComets[k]), Grid.CellToPosCCC(Cell(pitch * k + 4, h - 2), Grid.SceneLayer.FXFront), Grid.SceneLayer.FXFront);
                    comet.SetActive(true);
                    var body = comet.GetComponent<Comet>(); body.Velocity = new Vector2(0, -12);
                }
                for (var j = 0; j < soGeysers.Length; ++j)
                {
                    var prefab = Assets.GetPrefab(GeyserGenericConfig.ID + "_" + soGeysers[j]);
                    if (prefab == null) { soGeyserObjects.Add(null!); continue; }
                    var g = GameUtil.KInstantiate(prefab, Grid.CellToPosCBC(Cell(pitch * (soComets.Length + j) + 4, 1), Grid.SceneLayer.BuildingBack), Grid.SceneLayer.BuildingBack);
                    g.SetActive(true); soGeyserObjects.Add(g);
                }
            }
            if (Once(7, "erupt")) foreach (var g in soGeyserObjects) if (g != null) g.GetComponent<global::Geyser>().ShiftTimeTo(global::Geyser.TimeShiftStep.ActiveState);
            if (Once(100, "check"))
            {
                var live = UnityEngine.Object.FindObjectsByType<Comet>(UnityEngine.FindObjectsSortMode.None).Count(c => { var cell = Grid.PosToCell(c.transform.GetPosition()); Grid.CellToXY(cell, out var cx, out var cy); return cx >= x0 && cx < x0 + pitch * soComets.Length && cy >= y0 && cy < y0 + h; });
                Record("the comets landed", live == 0, $"{live} comets still falling");
                for (var k = 0; k < soComets.Length; ++k)
                    Record($"{soComets[k]} landed in its compartment", true, $"owned fluid {recorded["mass" + k]:G6} -> {Mass(k):G6} kg; debris oxylite {SiteDebris(SimHashes.OxyRock):G3}, ice {SiteDebris(SimHashes.Ice):G3}, corium {SiteDebris(SimHashes.Corium):G3}");
                for (var j = 0; j < soGeysers.Length; ++j)
                {
                    var k = soComets.Length + j; var element = soGeyserObjects[j] == null ? "-" : soGeyserObjects[j].GetComponent<global::Geyser>().configuration.GetElement().ToString();
                    Record($"geyser {soGeysers[j]} exists and erupts into the owner", soGeyserObjects[j] == null || Mass(k) > recorded["mass" + k] + .5,
                        soGeyserObjects[j] == null ? "no generic prefab in this game (placed by world generation only)" : $"{element}: owned fluid {recorded["mass" + k]:G6} -> {Mass(k):G6} kg");
                }
                LedgersClosed("comets and geysers");
                Finish();
            }
        }

        // The trigger of the owner fault found by the first rail-gun run (a
        // natural hydrogen pocket whose cells are displaced into vacuum when an
        // arena's border closes; docs/dlc-compatibility.md): five cells of
        // hydrogen, 0.1128 kg at 319 K each, beside a large vacuum. The owner
        // must keep running and keep its ledgers closed. Kept out of batches
        // until the flow solver's vacuum expansion of light gas is fixed.
        private static void SoVacuumPocket(int frame)
        {
            if (Once(0, "vacuum arena")) { RequireSpacedOut(); Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Vacuum, 0f, 0f), 30, 10); }
            if (Once(6, "hydrogen pocket"))
            {
                recorded["mass"] = ArenaSpecies(SimHashes.Hydrogen);
                for (var y = 1; y <= 5; ++y)
                    SimMessages.AddRemoveSubstance(Cell(29, y), SimHashes.Hydrogen, CellEventLogger.Instance.OxygenModifierSimUpdate, .1128f, 319, byte.MaxValue, 0);
            }
            if (Once(80, "check"))
            {
                var kept = Enumerable.Range(0, 30).Sum(x => Enumerable.Range(0, 10).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Hydrogen)));
                Record("the owner keeps running as the hydrogen expands into vacuum", ColonyOwner.Status.State == 1, $"owner state {ColonyOwner.Status.State}; {ColonyOwner.Status.Error}");
                Record("the expanding hydrogen is conserved", Math.Abs(kept - 5 * .1128) < 1e-4, $"{kept:R} of {5 * .1128:R} kg");
                LedgersClosed("vacuum pocket");
                Finish();
            }
        }
    }
}
