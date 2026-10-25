using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // In-game checks for the colony owner, one short scenario per system
    // (docs/colony-owner.md#scenarios, docs/playtest-plan.md#evidence). Each
    // carves a sealed arena of Unobtanium beside the printing pod through the
    // game's own cell messages, sets it up, runs some owner frames and checks
    // what the system should do. The owner's ledgers must stay closed
    // throughout. Harness only: scenario buildings are built instantly and a
    // building that needs power is wired to a dev generator in the top corner
    // on its side of the arena's middle column.
    internal static partial class ColonyScenarios
    {

        internal sealed class Check { public string Name = "", Detail = ""; public bool Passed; }
        internal sealed class Result
        {
            public string Name = "", Stage = "waiting";
            public bool Finished, Passed;
            public List<Check> Checks = new List<Check>();
        }
        internal static Result Current { get; private set; } = new Result();
        // A comma-separated list runs in one game, each in its own arena
        // further right of the pod.
        internal static List<Result> Results { get; } = new List<Result>();
        internal static bool AllPassed => Results.Count > 0 && Results.All(r => r.Finished && r.Passed);
        private static int index;

        private static ulong started;
        private static int step;
        private static int x0, y0;
        private const int Width = 8, Height = 6;
        private static readonly Dictionary<string, double> recorded = new Dictionary<string, double>();
        private static readonly List<GameObject> powered = new List<GameObject>();

        internal static void Reset() { Results.Clear(); index = 0; Next(); }
        private static void Next() { Current = new Result(); started = 0; step = 0; recorded.Clear(); powered.Clear(); filling = null; pipeCells.Clear(); generators.Clear(); mixedFilters.Clear();mixedSensors.Clear();mixedDiseaseSensor=null;mixedBoundaryJobs.Clear();mixedPumps.Clear();mixedPumpHeldOutputs.Clear();mixedPumpDischargeSources.Clear();mixedOutputJobs.Clear();mixedTeardownPipes.Clear();mixedValves.Clear();mixedThermalBody=null; }

        // Called every Unity frame while the owner runs. Returns true when
        // every scenario of the list is done.
        internal static bool Update(string list, OniNativeColony.Status status)
        {
            var names = list.Split(',');
            if (index >= names.Length) return true;
            if (!RunOne(names[index], status)) return false;
            Results.Add(Current); ++index;
            if (index >= names.Length) return true;
            Next(); return false;
        }
        private static bool RunOne(string name, OniNativeColony.Status status)
        {
            Current.Name = name;
            if (Current.Finished) return true;
            if (status.CompletedFrames == 0) return false;
            if (started == 0) { started = status.CompletedFrames; Anchor(); }
            var frame = (int)(status.CompletedFrames - started);
            try
            {
                Fill();
                switch (name)
                {
                    case "terrain": Terrain(frame); break;
                    case "heat": Heat(frame, status); break;
                    case "phase": Phase(frame, status); break;
                    case "freeze-melt": FreezeMelt(frame, status); break;
                    case "deoxidizer": Deoxidizer(frame, status); break;
                    case "pipes": Pipes(frame); break;
                    case "door": DoorCycle(frame, status); break;
                    case "sublimation": Sublimation(frame, status); break;
                    case "falling": Falling(frame); break;
                    case "breathing": Breathing(frame, status); break;
                    case "airflow": Airflow(frame); break;
                    case "electrolysis": Electrolysis(frame); break;
                    case "vent-overpressure": VentOverpressure(frame); break;
                    case "space-heater": SpaceHeater(frame); break;
                    case "bottle-emptier": BottleEmptier(frame); break;
                    case "gas-generator": GasGenerator(frame); break;
                    case "germs": Germs(frame); break;
                    case "world-germs": WorldGerms(frame); break;
                    case "germ-oracle": GermOracle(frame); break;
                    case "skimmer": Skimmer(frame); break;
                    case "terrarium": Terrarium(frame); break;
                    case "deodorizer": Deodorizer(frame); break;
                    case "rust-deoxidizer": RustDeoxidizer(frame); break;
                    case "coal-generator": CoalGenerator(frame); break;
                    case "oil-refinery": OilRefinery(frame); break;
                    case "electrolyzer-blocked": ElectrolyzerBlocked(frame); break;
                    case "hazards": Hazards(frame); break;
                    case "germ-exposure": GermExposure(frame); break;
                    case "suit": Suit(frame); break;
                    case "pitcher-pump": PitcherPump(frame); break;
                    case "fault-containment": FaultContainment(frame, status); break;
                    case "puft": Puft(frame); break;
                    case "plant": Plant(frame); break;
                    case "wheezewort": Wheezewort(frame); break;
                    case "life-support": LifeSupport(frame); break;
                    case "mask-station": MaskStation(frame); break;
                    case "polymer-press": PolymerPress(frame); break;
                    case "oxyfern": Oxyfern(frame); break;
                    case "debris-heat": DebrisHeat(frame, status); break;
                    case "building-melt": BuildingMelt(frame, status); break;
                    case "geyser": Geyser(frame); break;
                    case "rocket-exhaust": RocketExhaust(frame); break;
                    case "meteor": Meteor(frame, status); break;
                    case "sensor": Sensor(frame); break;
                    case "filter": Filter(frame); break;
                    case "mixed-pipes": MixedPipes(frame); break;
                    case "mixed-pipe-filter": MixedPipeFilter(frame); break;
                    case "mixed-pipe-boundary": MixedPipeBoundary(frame); break;
                    case "mixed-pipe-output": MixedPipeOutput(frame); break;
                    case "mixed-pipe-teardown": MixedPipeTeardown(frame); break;
                    case "mixed-pipe-valves": MixedPipeValves(frame); break;
                    case "mixed-pipe-gas-vents": MixedPipeGasVents(frame); break;
                    case "mixed-pipe-biology": MixedPipeGermEvolution(frame); break;
                    case "mixed-pipe-all-germs": MixedPipeAllGerms(frame); break;
                    case "mixed-pipe-save-guard": MixedPipeSaveGuard(frame); break;
                    case "mixed-pipe-pumps": MixedPipePumps(frame); break;
                    case "mixed-pipe-thermal": MixedPipeThermal(frame); break;
                    case "mixed-pipe-cold": MixedPipeCold(frame); break;
                    case "windows-contact": WindowsContact(frame); break;
                    case "mixed-pipe-machines": MixedPipeMachines(frame); break;
                    case "mixed-pipe-colony": MixedPipeColony(frame, status); break;
                    case "mixed-pipe-heat-machines": MixedPipeHeatMachines(frame); break;
                    case "mixed-pipe-retire": MixedPipeRetire(frame); break;
                    case "mixed-pipe-readers": MixedPipeReaders(frame); break;
                    case "mixed-pipe-owned-storage": MixedPipeOwnedStorage(frame); break;
                    case "mixed-storage-heat": MixedStorageHeat(frame); break;
                    case "owned-storage-readers": MixedOwnedStorageReaders(frame); break;
                    case "owned-storage-biology": MixedOwnedStorageBiology(frame); break;
                    case "pipe-schedule": RunPipeSchedule((ulong)frame); break;
                    case "pipe-body-rounding": MixedPipeBodyRounding(frame); break;
                    case "mixed-pipe-catalogue": MixedPipeCatalogue(frame); break;
                    case "pacu": Pacu(frame); break;
                    case "liquid-pump": LiquidPump(frame); break;
                    case "base": Base(frame, status); break;
                    default: throw new InvalidOperationException("Unknown colony scenario " + name);
                }
            }
            catch (Exception error)
            {
                var cause = error is System.Reflection.TargetInvocationException && error.InnerException != null ? error.InnerException : error;
                Debug.LogWarning("[Mixed Atmospheres] Colony scenario " + name + " threw: " + cause);
                Record("scenario ran", false, cause.GetType().Name + ": " + cause.Message); Finish();
            }
            return Current.Finished;
        }

        // ---- scenarios

        // Dig a granite column between gas and vacuum; then build a tile in the gas.
        private static void Terrain(int frame)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x < 4 ? (SimHashes.Oxygen, 1.2f, 300f) : x == 4 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Vacuum, 0f, 0f));
            if (Once(6, "dig"))
            {
                recorded["gas"] = ArenaMass(); recorded["opened"] = ColonyOwner.Status.OpenedCells; recorded["outside"] = OutsideMass();
                for (var y = 0; y < Height; ++y) SimMessages.Dig(Cell(4, y));
            }
            if (Once(14, "build"))
            {
                var right = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => CellMass(Cell(x, y))));
                Record("dug column opened", ColonyOwner.Status.OpenedCells >= recorded["opened"] + Height, $"opened {ColonyOwner.Status.OpenedCells - recorded["opened"]}");
                Record("gas reached the far chamber", right > .5, $"{right:G4} kg");
                Record("dig conserved the gas", Math.Abs(ArenaMass() - recorded["gas"]) < 1e-6 * recorded["gas"],
                    $"{recorded["gas"]:G9} -> {ArenaMass():G9} kg; columns {string.Join(" ", Enumerable.Range(0, Width).Select(x => Enumerable.Range(0, Height).Sum(y => CellMass(Cell(x, y))).ToString("G4")))}; " +
                    $"just outside {recorded["outside"]:G6} -> {OutsideMass():G6} kg");
                recorded["gas"] = ArenaMass(); recorded["closed"] = ColonyOwner.Status.ClosedCells;
                SimMessages.ReplaceElement(Cell(1, 2), SimHashes.Granite, CellEventLogger.Instance.SandBoxTool, 1000, 300);
            }
            if (Once(18, "check"))
            {
                Record("built tile closed its cell", ColonyOwner.Status.ClosedCells >= recorded["closed"] + 1 && Grid.Solid[Cell(1, 2)], $"closed {ColonyOwner.Status.ClosedCells - recorded["closed"]}");
                Record("build displaced, not deleted, the gas", Math.Abs(ArenaMass() - recorded["gas"]) < 1e-6 * recorded["gas"], $"{recorded["gas"]:G9} -> {ArenaMass():G9} kg");
                Finish();
            }
        }

        // A hot granite floor heats the air above it through ONI's own conduction.
        private static void Heat(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 2000f, 450f) : (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "start")) recorded["t"] = GasTemperature(1);
            if (Once(27, "check"))
            {
                // The colony-wide native heat counter also holds every other contact
                // (Terra's magma cooling into its rock), so the ledger is the check.
                var t = GasTemperature(1);
                Record("air above the floor warmed", t > recorded["t"] + .2, $"{recorded["t"]:F3} -> {t:F3} K");
                Record("the owner's energy ledger stayed closed", status.MaximumRelativeEnergyError < 1e-9, $"{status.MaximumRelativeEnergyError:G3}");
                Finish();
            }
        }

        // Warm water evaporates into the dry air above it at its temperature.
        private static void Phase(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena"))
                Arena((x, y) => y == 0 ? (SimHashes.Granite, 4000f, 330f) : y <= 2 ? (SimHashes.Water, 990f, 330f) : (SimHashes.Oxygen, 1.2f, 330f));
            if (Once(6, "start")) recorded["evaporated"] = status.EvaporatedKg;
            if (Once(32, "check"))
            {
                Record("water evaporated", status.EvaporatedKg > recorded["evaporated"] + .01, $"{status.EvaporatedKg - recorded["evaporated"]:G4} kg");
                Record("vapour is in the air", Enumerable.Range(0, Width).Any(x => SpeciesMass(Cell(x, 4), SimHashes.Steam) > 0), "steam above the pool");
                Finish();
            }
        }

        // Ice in warm air melts into water; water on a cold block freezes.
        // Optional existing native admission capture: observes both native
        // buffers before UpdateData without inserting a tick or a barrier.
        private static string WarmIceCapturePath=>Environment.GetEnvironmentVariable("MA_COLONY_FLOOR_ADMISSION_DUMP")??"";
        private static void RequestWarmIceCapture()
        {
            var path=WarmIceCapturePath;if(path.Length==0)return;
            if(System.IO.File.Exists(path)||System.IO.File.Exists(path+".request"))throw new System.IO.IOException("Ice source capture needs a fresh private path.");
            using(var writer=new System.IO.BinaryWriter(System.IO.File.Open(path+".request.tmp",System.IO.FileMode.CreateNew)))
            {
                writer.Write(System.Text.Encoding.ASCII.GetBytes("MAFLRQ01"));
                foreach(var dimension in new[]{Grid.WidthInCells,Grid.HeightInCells,x0,y0,Width,Height})writer.Write((uint)dimension);
                writer.Write(1u);writer.Write((uint)Cell(6,2));writer.Write((uint)ElementLoader.FindElementByHash(SimHashes.Ice).idx);
                writer.Write(400f);writer.Write(290f);
            }
            System.IO.File.Move(path+".request.tmp",path+".request");
        }
        private static void RecordWarmIceCapture()
        {
            var path=WarmIceCapturePath;if(path.Length==0)return;
            if(!System.IO.File.Exists(path)){Record("the full warm ice reached native physics before dynamics",false,"no native source capture");return;}
            using(var reader=new System.IO.BinaryReader(System.IO.File.OpenRead(path)))
            {
                if(reader.BaseStream.Length!=84||System.Text.Encoding.ASCII.GetString(reader.ReadBytes(8))!="MAFLPR01")throw new System.IO.InvalidDataException("Ice source capture framing differs.");
                foreach(var dimension in new[]{Grid.WidthInCells,Grid.HeightInCells,x0,y0,Width,Height})
                    if(reader.ReadUInt32()!=(uint)dimension)throw new System.IO.InvalidDataException("Ice source capture geometry differs.");
                var ticket=reader.ReadUInt64();var seconds=reader.ReadDouble();var mask=reader.ReadUInt32();var count=reader.ReadUInt32();var cell=reader.ReadUInt32();
                var okay=ticket>0&&seconds>0&&mask==3&&count==1&&cell==(uint)Cell(6,2);var detail=$"native ticket {ticket}, dt {seconds:R}, mask {mask}";
                for(var buffer=0;buffer<2;++buffer)
                {
                    var element=reader.ReadUInt32();var mass=reader.ReadSingle();var temperature=reader.ReadSingle();
                    okay&=element==ElementLoader.FindElementByHash(SimHashes.Ice).idx&&mass==400&&temperature==290;
                    detail+=$"; buffer {buffer} element {element}, {mass:R} kg, {temperature:R} K";
                }
                Record("the full warm ice reached native physics before dynamics",okay,detail);
            }
        }
        private static void FreezeMelt(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x < 4 ? (y == 0 ? (SimHashes.Granite, 4000f, 40f) : y == 1 ? (SimHashes.Water, 20f, 274f) : (SimHashes.Oxygen, 1.2f, 280f))
                    : (y == 2 && x == 6 ? (SimHashes.Vacuum, 0f, 0f) : (SimHashes.Oxygen, 1.2f, 300f)));
            // Before the fill (frame 2): the warm ice melts within frames of being placed.
            if (Once(1, "start")) { recorded["opened"] = status.OpenedCells; recorded["closed"] = status.ClosedCells; recorded["injected"] = status.InjectedKg; }
            // Restore the original dynamic source stage after the bulk fill.
            // It must not melt into cells the initializer will overwrite.
            if (Once(2, "warm ice source"))
            {
                RequestWarmIceCapture();
                SimMessages.ReplaceElement(Cell(6, 2), SimHashes.Ice, CellEventLogger.Instance.SandBoxTool, 400, 290);
            }
            if (Once(42, "check"))
            {
                RecordWarmIceCapture();
                string Describe(int cell) => $"{Grid.Element[cell].id} {Grid.Mass[cell]:G4} kg {Grid.Temperature[cell]:F1} K{(Grid.Solid[cell] ? " solid" : "")}{(ColonyOwner.OwnsFluidCell(cell) ? " owned" : "")}";
                // The ice's 400 kg melts into the owner (it may melt before the
                // owner ever closes its cell, so no opening need be counted).
                var arenaWater = Enumerable.Range(0, Width).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Water)));
                Record("ice melted into an owned cell", !Grid.Solid[Cell(6, 2)] && ColonyOwner.OwnsFluidCell(Cell(6, 2)) && arenaWater > 380,
                    $"opened {status.OpenedCells - recorded["opened"]}, closed {status.ClosedCells - recorded["closed"]}; ice cell {Describe(Cell(6, 2))}; owned water in the arena {arenaWater:G4} kg");
                Record("melt water booked", status.InjectedKg > recorded["injected"] + 300, $"{status.InjectedKg - recorded["injected"]:G4} kg");
                // A small cell of water freezes into ice debris, a larger one into a tile.
                var frozen = Enumerable.Range(0, 4).Count(x => Grid.Solid[Cell(x, 1)]);
                var ice = Components.Pickupables.Items.Where(item => item != null && item.GetComponent<PrimaryElement>()?.ElementID == SimHashes.Ice)
                    .Where(item => { Grid.CellToXY(Grid.PosToCell(item), out var ix, out var iy); return ix >= x0 && ix < x0 + 4 && iy >= y0 && iy < y0 + Height; })
                    .Sum(item => (double)item.GetComponent<PrimaryElement>().Mass);
                var water = Enumerable.Range(0, 4).Sum(x => SpeciesMass(Cell(x, 1), SimHashes.Water));
                Record("water froze on the cold block", frozen > 0 || ice > 1,
                    $"{frozen} frozen cells, {ice:G4} kg ice debris, {water:G4} kg water left; pool {Describe(Cell(1, 1))}, floor {Describe(Cell(1, 0))}");
                Finish();
            }
        }

        // An oxygen diffuser (mineral deoxidizer) turns stored algae into oxygen
        // that ONI emits into its cell; the owner takes it.
        private static void Deoxidizer(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, .6f, 300f));
            if (Once(6, "build"))
            {
                recorded["gas"] = ArenaMass(); recorded["injected"] = status.InjectedKg;
                var building = Build(MineralDeoxidizerConfig.ID, Cell(3, 0));
                building.GetComponent<Storage>().AddOre(SimHashes.Algae, 100, 300, byte.MaxValue, 0);
            }
            if (Once(32, "check"))
            {
                var added = ArenaMass() - recorded["gas"];
                Record("the machine's oxygen reached the owner", status.InjectedKg > recorded["injected"] + .01 && added > .01, $"{added:G4} kg; {MachineState(powered.FirstOrDefault())}");
                Finish();
            }
        }

        // A gas pump in a chamber of oxygen and carbon dioxide feeds a pipe
        // through the wall to a vent in a vacuum chamber: both gases arrive as
        // packets, and owner, pipe and pump together keep the gas.
        private static void Pipes(int frame)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : x > 4 ? (SimHashes.Vacuum, 0f, 0f) :
                    ((x + y) % 2 == 0 ? (SimHashes.Oxygen, 1.2f, 300f) : (SimHashes.CarbonDioxide, 1.6f, 300f)));
            if (Once(6, "build"))
            {
                recorded["gas"] = ArenaMass(); recorded["consumed"] = ColonyActors.DeliveredKg; recorded["injected"] = ColonyOwner.Status.InjectedKg;
                recorded["extracted"] = ColonyOwner.Status.ExtractedKg;
                var pump = Build(GasPumpConfig.ID, Cell(1, 0));
                var vent = Build(GasVentConfig.ID, Cell(6, 0));
                var from = Grid.OffsetCell(Cell(1, 0), pump.GetComponent<Building>().Def.UtilityOutputOffset);
                var to = Grid.OffsetCell(Cell(6, 0), vent.GetComponent<Building>().Def.UtilityInputOffset);
                Grid.CellToXY(from, out var fx, out var fy); Grid.CellToXY(to, out var tx, out var ty);
                var path = new List<int>();
                for (var x = fx; x != tx; x += Math.Sign(tx - fx)) path.Add(Grid.XYToCell(x, fy));
                for (var y = fy; y != ty; y += Math.Sign(ty - fy)) path.Add(Grid.XYToCell(tx, y));
                path.Add(to);
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, path);
                pipeCells.AddRange(path);
            }
            if (frame >= 8 && frame <= 99 && frame % 5 == 0 && Once(frame, "trace" + frame))
            {
                double Side(int from, int to) => Enumerable.Range(from, to - from).Sum(x => Enumerable.Range(0, Height).Sum(y => CellMass(Cell(x, y))));
                var pipe = pipeCells.Sum(c => (double)Game.Instance.gasConduitFlow.GetContents(c).mass);
                var held = powered.Where(b => b != null).Sum(b => b.GetComponents<Storage>().Sum(st => st.items.Where(i => i != null).Sum(i => (double)(i.GetComponent<PrimaryElement>()?.Mass ?? 0))));
                Debug.Log($"[Mixed Atmospheres] pipes frame {frame}: left {Side(0, 4):G6} right {Side(5, 8):G6} pipe {pipe:G4} stored {held:G4} receipts {ColonyActors.DeliveredKg - recorded["consumed"]:G4} " +
                    $"injected {ColonyOwner.Status.InjectedKg - recorded["injected"]:G4} extracted {ColonyOwner.Status.ExtractedKg - recorded["extracted"]:G4}; wall " +
                    string.Join(" ", Enumerable.Range(-1, Height + 2).Select(y => $"{Grid.Element[Cell(4, y)].id}:{Grid.Mass[Cell(4, y)]:G4}{(Grid.Solid[Cell(4, y)] ? "S" : "")}{(ColonyOwner.OwnsFluidCell(Cell(4, y)) ? "o" : "")}")) +
                    "; far row 2 " + string.Join(" ", Enumerable.Range(4, 5).Select(x => $"{Grid.Element[Cell(x, 2)].id}:{Grid.Mass[Cell(x, 2)]:G4}{(ColonyOwner.OwnsFluidCell(Cell(x, 2)) ? "o" : "")}")));
            }
            // A vent's emission waits for the owner's next step; switching the
            // vent off before the check lets the last of it land.
            if (Once(96, "vent off"))
                powered.FirstOrDefault(b => b != null && b.GetComponent<Vent>() != null)?.GetComponent<BuildingEnabledButton>().IsEnabled = false;
            // ONI's pipes move a packet one segment per second: the first
            // reaches the vent after about eight seconds.
            if (Once(100, "check"))
            {
                var right = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => CellMass(Cell(x, y))));
                var oxygen = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Oxygen)));
                var carbon = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.CarbonDioxide)));
                var inPipe = pipeCells.Sum(c => (double)Game.Instance.gasConduitFlow.GetContents(c).mass);
                var stored = powered.Where(b => b != null).Sum(b => HeldMass(b!));
                var vent = powered.FirstOrDefault(b => b != null && b.GetComponent<Vent>() != null);
                var ventState = vent == null ? "no vent" : $"blocked {vent.GetComponent<Vent>().IsBlocked}, cell solid {Grid.Solid[Grid.PosToCell(vent)]}, stored {vent.GetComponents<Storage>().Sum(st => st.MassStored()):G4} kg; {MachineState(vent)}";
                string Column(int x) => string.Join(" ", Enumerable.Range(-1, Height + 2).Select(y => $"{Grid.Element[Cell(x, y)].id}:{Grid.Mass[Cell(x, y)]:G3}{(Grid.Solid[Cell(x, y)] ? "S" : "")}{(ColonyOwner.OwnsFluidCell(Cell(x, y)) ? "o" : "")}"));
                Record("gas crossed through the pipe", right > .05, $"{right:G4} kg in the far chamber; pump {MachineState(powered.FirstOrDefault())}; vent {ventState}; left border {Column(-1)}; right border {Column(Width)}");
                Record("the pipe carried both gases", oxygen > 0 && carbon > 0, $"oxygen {oxygen:G4} kg, carbon dioxide {carbon:G4} kg");
                var total = ArenaMass() + inPipe + stored;
                Record("owner, pipe and pump keep the gas", Math.Abs(total - recorded["gas"]) < .01 * recorded["gas"],
                    $"{recorded["gas"]:G6} -> {total:G6} kg (arena {ArenaMass():G5}, pipe {inPipe:G4}, stored {stored:G4}; consumer receipts {ColonyActors.DeliveredKg - recorded["consumed"]:G4} kg, " +
                    $"owner extracted {ColonyOwner.Status.ExtractedKg - recorded["extracted"]:G4} kg, injected {ColonyOwner.Status.InjectedKg - recorded["injected"]:G4} kg)");
                Finish();
            }
        }
        // An airlock between oxygen and vacuum: closed it seals, opened the gas
        // passes, and closing again displaces the gas in its cells.
        private static Door? door;
        private static int traced = -1;
        private static void DoorCycle(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : x < 4 ? (SimHashes.Oxygen, 1.2f, 300f) : (SimHashes.Vacuum, 0f, 0f));
            // A manual airlock (a pneumatic door does not block gas in ONI), built
            // into the wall so that no gas passes before it is shut. The door
            // takes control-state changes once it has spawned (a frame later).
            if (Once(6, "build")) door = Build(ManualPressureDoorConfig.ID, Cell(4, 0)).GetComponent<Door>();
            if (Once(7, "lock")) SetDoor(Door.ControlState.Locked);
            if (door != null && frame >= 8 && frame % 2 == 0 && traced != frame && (traced = frame) >= 0)
                Debug.Log($"[Mixed Atmospheres] door frame {frame}: open {door.IsOpen()}, control {door.CurrentState}, state {door.GetSMI<Door.Controller.Instance>()?.GetCurrentState()?.name}; cells " +
                    string.Join(" ", new[] { Cell(4, 0), Cell(4, 1) }.Select(c => $"{Grid.Element[c].id}:{Grid.Mass[c]:G4}{(Grid.Solid[c] ? "S" : "")}{(ColonyOwner.OwnsFluidCell(c) ? "o" : "")}")) +
                    $"; right {RightMass():G4} kg, opened {status.OpenedCells}, closed {status.ClosedCells}");
            if (Once(10, "closed")) { recorded["gas"] = ArenaMass(); recorded["right"] = RightMass(); }
            if (Once(16, "open"))
            {
                Record("the closed door sealed", RightMass() <= recorded["right"] + 1e-9, $"{RightMass():G4} kg beyond the door");
                recorded["opened"] = status.OpenedCells; SetDoor(Door.ControlState.Opened);
            }
            // The airlock's opening and closing animations take a couple of seconds.
            if (Once(44, "close"))
            {
                Record("the open door let gas through", status.OpenedCells >= recorded["opened"] + 2 && RightMass() > .05, $"{RightMass():G4} kg beyond the door");
                Record("opening conserved the gas", Math.Abs(ArenaMass() - recorded["gas"]) < 1e-6 * recorded["gas"], $"{recorded["gas"]:G9} -> {ArenaMass():G9} kg");
                recorded["closed"] = status.ClosedCells; SetDoor(Door.ControlState.Locked);
            }
            if (Once(64, "check"))
            {
                Record("closing the door closed its cells", status.ClosedCells >= recorded["closed"] + 2 && Grid.Solid[Cell(4, 0)] && Grid.Solid[Cell(4, 1)],
                    $"closed {status.ClosedCells - recorded["closed"]}");
                Record("closing displaced, not deleted, the gas", Math.Abs(ArenaMass() - recorded["gas"]) < 1e-6 * recorded["gas"], $"{recorded["gas"]:G9} -> {ArenaMass():G9} kg");
                Finish();
            }
        }
        // The game changes a door's state through an errand unless instant
        // building is on; the scenario applies the request at once.
        private static void SetDoor(Door.ControlState state)
        {
            door!.QueueStateChange(state);
            AccessTools.Method(typeof(Door), "ApplyRequestedControlState").Invoke(door, Array.Empty<object>());
        }
        private static double RightMass() => Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => CellMass(Cell(x, y))));

        // Oxylite on the floor of thin oxygen sublimates oxygen into its cell.
        private static void Sublimation(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, .1f, 300f));
            if (Once(6, "drop"))
            {
                recorded["gas"] = ArenaMass(); recorded["injected"] = status.InjectedKg;
                ElementLoader.FindElementByHash(SimHashes.OxyRock).substance.SpawnResource(Grid.CellToPosCCC(Cell(3, 0), Grid.SceneLayer.Ore), 100, 300, byte.MaxValue, 0);
            }
            if (Once(32, "check"))
            {
                var added = ArenaMass() - recorded["gas"];
                Record("oxylite released oxygen into the owner", status.InjectedKg > recorded["injected"] && added > 1e-3, $"{added:G4} kg");
                Finish();
            }
        }

        // A sand tile placed over gas falls: its cell opens, it lands and closes
        // a floor cell, and the gas it passes through is kept.
        private static void Falling(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "place"))
            {
                recorded["gas"] = ArenaMass(); recorded["opened"] = ColonyOwner.Status.OpenedCells; recorded["closed"] = ColonyOwner.Status.ClosedCells;
                recorded["nativeIn"] = ColonyOwner.Status.NativeMassInKg; recorded["nativeOut"] = ColonyOwner.Status.NativeMassOutKg;
                recorded["injected"] = ColonyOwner.Status.InjectedKg; recorded["extracted"] = ColonyOwner.Status.ExtractedKg;
                SimMessages.ReplaceElement(Cell(3, 4), SimHashes.Sand, CellEventLogger.Instance.SandBoxTool, 500, 300);
            }
            if (frame >= 6 && frame <= 16 && Once(frame, "trace" + frame))
                Debug.Log($"[Mixed Atmospheres] falling frame {frame}: gas {ArenaMass():G9} kg, opened {ColonyOwner.Status.OpenedCells}, closed {ColonyOwner.Status.ClosedCells}, column " +
                    string.Join(" ", Enumerable.Range(0, Height).Select(y => $"{Grid.Element[Cell(3, y)].id}:{Grid.Mass[Cell(3, y)]:G4}{(Grid.Solid[Cell(3, y)] ? "S" : "")}{(ColonyOwner.OwnsFluidCell(Cell(3, y)) ? "o" : "")}")) +
                    $"; falling objects {Components.Pickupables.Items.Count(p => p != null && p.GetComponent<PrimaryElement>()?.ElementID == SimHashes.Sand)}");
            if (Once(30, "check"))
            {
                Record("the sand fell", !Grid.Solid[Cell(3, 4)] && Grid.Solid[Cell(3, 0)], $"top solid {Grid.Solid[Cell(3, 4)]}, floor solid {Grid.Solid[Cell(3, 0)]}");
                Record("the falling cell reopened and the landing cell closed", ColonyOwner.Status.OpenedCells > recorded["opened"] &&
                    ColonyOwner.Status.ClosedCells > recorded["closed"] + 1, $"opened {ColonyOwner.Status.OpenedCells - recorded["opened"]}, closed {ColonyOwner.Status.ClosedCells - recorded["closed"]}");
                var st = ColonyOwner.Status;
                Record("the gas was kept", Math.Abs(ArenaMass() - recorded["gas"]) < 1e-6 * recorded["gas"],
                    $"{recorded["gas"]:G9} -> {ArenaMass():G9} kg; native in {st.NativeMassInKg - recorded["nativeIn"]:G4} out {st.NativeMassOutKg - recorded["nativeOut"]:G4}, " +
                    $"injected {st.InjectedKg - recorded["injected"]:G4}, extracted {st.ExtractedKg - recorded["extracted"]:G4} kg; sand at {string.Join(" ", Enumerable.Range(0, Height).Select(y => Grid.Element[Cell(3, y)].id.ToString()))}");
                Finish();
            }
        }

        // The colony's duplicants breathe the owner's air at the printing pod:
        // their breath stays full, oxygen is taken and carbon dioxide returned.
        private static void Breathing(int frame, OniNativeColony.Status status)
        {
            var breath = Db.Get().Amounts.Breath;
            if (Once(0, "start"))
            {
                recorded["extracted"] = status.ExtractedKg; recorded["co2"] = OwnedMass(SimHashes.CarbonDioxide); recorded["oxygen"] = OwnedMass(SimHashes.Oxygen);
                foreach (var minion in Components.LiveMinionIdentities.Items) breath.Lookup(minion.gameObject).SetValue(100);
            }
            if (Once(40, "check"))
            {
                var minions = Components.LiveMinionIdentities.Items;
                var lowest = minions.Count == 0 ? 0 : minions.Min(m => breath.Lookup(m.gameObject).value);
                Record("every duplicant kept its breath", minions.Count > 0 && lowest > 90, $"{minions.Count} duplicants, lowest breath {lowest:F1}");
                Record("oxygen was taken from the owner", status.ExtractedKg > recorded["extracted"] + .1, $"{status.ExtractedKg - recorded["extracted"]:G4} kg extracted");
                Record("carbon dioxide was exhaled into the owner", OwnedMass(SimHashes.CarbonDioxide) > recorded["co2"], $"{OwnedMass(SimHashes.CarbonDioxide) - recorded["co2"]:G4} kg");
                Finish();
            }
        }
        // A column of airflow tiles between water under air and thinner air:
        // the air passes, the water does not. The column starts as rock; the
        // tiles are built on it and it is dug, so no water crosses first.
        private static void Airflow(int frame)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : x < 4 ? (y <= 2 ? (SimHashes.Water, 990f, 300f) : (SimHashes.Oxygen, 1.2f, 300f)) :
                    (SimHashes.Oxygen, .3f, 300f));
            if (Once(6, "build"))
            {
                for (var y = 0; y < Height; ++y) Build(GasPermeableMembraneConfig.ID, Cell(4, y));
                for (var y = 0; y < Height; ++y) SimMessages.Dig(Cell(4, y));
            }
            if (Once(8, "start")) recorded["right"] = RightGas();
            if (Once(32, "check"))
            {
                var liquid = Enumerable.Range(4, 4).Sum(x => Enumerable.Range(0, Height).Sum(y => LiquidMass(Cell(x, y))));
                Record("no water entered or crossed the airflow tiles", liquid == 0, $"{liquid:G4} kg");
                Record("air passed the airflow tiles", RightGas() > recorded["right"] + .05, $"{recorded["right"]:G4} -> {RightGas():G4} kg");
                Finish();
            }
        }
        private static double RightGas() => Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => CellMass(Cell(x, y)) - LiquidMass(Cell(x, y))));
        private static double LiquidMass(int cell)
        {
            var count = ColonyOwner.Composition(cell, out var indices, out var masses);
            double total = 0; for (var k = 0; k < count; ++k) if (ElementLoader.elements[(int)indices[k]].IsLiquid) total += masses[k];
            return total;
        }

        private static double ArenaSpecies(SimHashes element) =>
            Enumerable.Range(0, Width).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), element)));

        // An electrolyzer splits stored water into oxygen and hydrogen for the owner.
        private static void Electrolysis(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, .3f, 300f));
            if (Once(6, "build"))
            {
                recorded["o2"] = ArenaSpecies(SimHashes.Oxygen); recorded["h2"] = ArenaSpecies(SimHashes.Hydrogen);
                Build(ElectrolyzerConfig.ID, Cell(3, 0)).GetComponent<Storage>().AddLiquid(SimHashes.Water, 100, 300, byte.MaxValue, 0, false, true);
            }
            if (Once(32, "check"))
            {
                var o2 = ArenaSpecies(SimHashes.Oxygen) - recorded["o2"]; var h2 = ArenaSpecies(SimHashes.Hydrogen) - recorded["h2"];
                Record("oxygen and hydrogen reached the owner", o2 > .05 && h2 > .005, $"oxygen +{o2:G4} kg, hydrogen +{h2:G4} kg; {MachineState(powered.FirstOrDefault())}");
                Finish();
            }
        }

        // A pump feeds a vent into air above the vent's overpressure limit: the vent stays shut.
        private static void VentOverpressure(int frame)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : x < 4 ? (SimHashes.Oxygen, 1.2f, 300f) : (SimHashes.Oxygen, 3f, 300f));
            if (Once(6, "build"))
            {
                var pump = Build(GasPumpConfig.ID, Cell(1, 0)); var vent = Build(GasVentConfig.ID, Cell(6, 0));
                var from = Grid.OffsetCell(Cell(1, 0), pump.GetComponent<Building>().Def.UtilityOutputOffset);
                var to = Grid.OffsetCell(Cell(6, 0), vent.GetComponent<Building>().Def.UtilityInputOffset);
                Grid.CellToXY(from, out var fx, out var fy); Grid.CellToXY(to, out var tx, out var ty);
                var path = new List<int>();
                for (var x = fx; x != tx; x += Math.Sign(tx - fx)) path.Add(Grid.XYToCell(x, fy));
                for (var y = fy; y != ty; y += Math.Sign(ty - fy)) path.Add(Grid.XYToCell(tx, y));
                path.Add(to);
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, path); pipeCells.AddRange(path);
                recorded["right"] = RightMass();
            }
            if (Once(32, "check"))
            {
                var inPipe = pipeCells.Sum(c => (double)Game.Instance.gasConduitFlow.GetContents(c).mass);
                Record("the vent stayed shut above its overpressure", RightMass() - recorded["right"] < .05 && inPipe > .05,
                    $"right chamber +{RightMass() - recorded["right"]:G4} kg, pipe holds {inPipe:G4} kg");
                Finish();
            }
        }

        // A powered space heater warms the arena's air.
        private static void SpaceHeater(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, 1.2f, 290f));
            if (Once(6, "build")) { recorded["t"] = GasTemperature(3); Build(SpaceHeaterConfig.ID, Cell(3, 0)); }
            if (Once(36, "check"))
            {
                var t = GasTemperature(3);
                Record("the heater warmed the air", t > recorded["t"] + .1, $"{recorded["t"]:F3} -> {t:F3} K");
                Finish();
            }
        }

        // A gas canister emptier releases its stored carbon dioxide into the owner.
        private static void BottleEmptier(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, .5f, 300f));
            if (Once(6, "build")) { Build(BottleEmptierGasConfig.ID, Cell(2, 0)); Build(BottleEmptierConfig.ID, Cell(5, 0)); }
            // Once spawned, an emptier drops whatever its element filter does not
            // accept, so the filter is set before its contents are stored.
            if (Once(7, "fill"))
            {
                recorded["co2"] = ArenaSpecies(SimHashes.CarbonDioxide); recorded["water"] = ArenaSpecies(SimHashes.Water);
                Storage Accepting(GameObject b, SimHashes element)
                {
                    b.GetComponent<TreeFilterable>().UpdateFilters(new HashSet<Tag> { element.CreateTag() });
                    return b.GetComponent<Storage>();
                }
                Accepting(powered[0], SimHashes.CarbonDioxide).AddGasChunk(SimHashes.CarbonDioxide, 5, 300, byte.MaxValue, 0, false, true);
                // The liquid emptier pours as falling-water particles that land in owned cells.
                Accepting(powered[1], SimHashes.Water).AddLiquid(SimHashes.Water, 50, 300, byte.MaxValue, 0, false, true);
            }
            if (Once(8, "filled"))
                Debug.Log("[Mixed Atmospheres] bottle emptiers hold " + string.Join(", ", powered.Where(b => b != null).Select(b => $"{b.name} {b.GetComponents<Storage>().Sum(st => st.MassStored()):G4} kg at {Grid.PosToCell(b)}")));
            if (Once(32, "check"))
            {
                var added = ArenaSpecies(SimHashes.CarbonDioxide) - recorded["co2"]; var poured = ArenaSpecies(SimHashes.Water) - recorded["water"];
                string Emptier(GameObject? b) => b == null ? "missing" : $"cell solid {Grid.Solid[Grid.PosToCell(b)]}, stored {b.GetComponents<Storage>().Sum(st => st.MassStored()):G4} kg; {MachineState(b)}";
                Record("the gas emptier released its gas into the owner", added > .05, $"+{added:G4} kg carbon dioxide; {Emptier(powered.ElementAtOrDefault(0))}");
                Record("the liquid emptier's falling water landed in the owner", poured > .5, $"+{poured:G4} kg water; {Emptier(powered.ElementAtOrDefault(1))}");
                Finish();
            }
        }

        // A natural gas generator burns stored methane and exhausts carbon dioxide.
        private static void GasGenerator(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, .5f, 300f));
            if (Once(6, "build"))
            {
                recorded["co2"] = ArenaSpecies(SimHashes.CarbonDioxide);
                var generator = Build(MethaneGeneratorConfig.ID, Cell(3, 0));
                generator.GetComponent<Storage>().AddGasChunk(SimHashes.Methane, 5, 300, byte.MaxValue, 0, false, true);
                // A generator runs only on a circuit (a wire on its output) and
                // with its output port piped, here to a vent of that port's kind.
                var def = generator.GetComponent<Building>().Def;
                Wire(new List<int> { Grid.OffsetCell(Grid.PosToCell(generator), def.PowerOutputOffset) });
                var gas = def.OutputConduitType == ConduitType.Gas;
                var vent = Build(gas ? GasVentConfig.ID : LiquidVentConfig.ID, Cell(6, 0));
                Pipe(gas ? Game.Instance.gasConduitSystem : Game.Instance.liquidConduitSystem, gas ? GasConduitConfig.ID : LiquidConduitConfig.ID, Route(
                    Grid.OffsetCell(Grid.PosToCell(generator), def.UtilityOutputOffset),
                    Grid.OffsetCell(Grid.PosToCell(vent), vent.GetComponent<Building>().Def.UtilityInputOffset)));
            }
            // The exhaust reaches the vent after ONI's pipe transit.
            if (Once(90, "check"))
            {
                var added = ArenaSpecies(SimHashes.CarbonDioxide) - recorded["co2"];
                var gen = powered.FirstOrDefault(); var def = gen?.GetComponent<Building>().Def;
                var outCell = gen == null ? -1 : gen.GetComponent<Building>().GetUtilityOutputCell();
                Record("the generator's exhaust reached the owner", added > .005, $"+{added:G4} kg carbon dioxide; {MachineState(gen)}; output {def?.OutputConduitType} at {outCell}, " +
                    $"liquid pipe there {outCell >= 0 && Grid.Objects[outCell, (int)ObjectLayer.LiquidConduit] != null}, gas pipe there {outCell >= 0 && Grid.Objects[outCell, (int)ObjectLayer.GasConduit] != null}");
                Finish();
            }
        }

        // Slimelung germs released with polluted oxygen spread with the gas.
        private static void Germs(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.ContaminatedOxygen, 1f, 300f));
            if (Once(6, "release"))
            {
                var slime = Db.Get().Diseases.GetIndex(Db.Get().Diseases.SlimeGerms.id);
                SimMessages.AddRemoveSubstance(Cell(1, 1), SimHashes.ContaminatedOxygen, CellEventLogger.Instance.OxygenModifierSimUpdate, .5f, 300, slime, 1000000);
                recorded["slime"] = slime;
            }
            if (Once(30, "check"))
            {
                var slime = (byte)recorded["slime"]; var cells = 0; double total = 0;
                for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
                    if (Grid.DiseaseIdx[Cell(x, y)] == slime && Grid.DiseaseCount[Cell(x, y)] > 0) { ++cells; total += Grid.DiseaseCount[Cell(x, y)]; }
                Record("the germs travelled with the gas", cells > 1 && total > 1000, $"{total:G4} germs in {cells} cells");
                Finish();
            }
        }

        // A puft breathes the arena's polluted oxygen.
        private static void Puft(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.ContaminatedOxygen, 1f, 300f));
            if (Once(6, "spawn"))
            {
                recorded["po2"] = ArenaSpecies(SimHashes.ContaminatedOxygen);
                var puft = Util.KInstantiate(Assets.GetPrefab(PuftConfig.ID), Grid.CellToPosCBC(Cell(3, 2), Grid.SceneLayer.Creatures));
                puft.SetActive(true);
            }
            if (Once(36, "check"))
            {
                var taken = recorded["po2"] - ArenaSpecies(SimHashes.ContaminatedOxygen);
                Record("the puft took polluted oxygen from the owner", taken > .001, $"{taken:G4} kg");
                Finish();
            }
        }

        // A wild mealwood in owned oxygen reads normal pressure and a safe atmosphere.
        private static GameObject? plant, thinPlant;
        private static void Plant(int frame)
        {
            // Left of the wall normal air; right of it air thinner than the plant's low-pressure warning.
            if (Once(0, "arena"))
                Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : (SimHashes.Oxygen, x < 4 ? 1.2f : .03f, 300f));
            if (Once(6, "plant"))
            {
                GameObject Sow(int x)
                {
                    var sown = GameUtil.KInstantiate(Assets.GetPrefab(BasicSingleHarvestPlantConfig.ID), Grid.CellToPosCBC(Cell(x, 1), Grid.SceneLayer.BuildingFront), Grid.SceneLayer.BuildingFront);
                    sown.SetActive(true);
                    return sown;
                }
                plant = Sow(2); thinPlant = Sow(6);
            }
            // ONI smooths a plant's pressure reading (70% of the last each second), so
            // the thin side needs about ten seconds to read low.
            if (Once(60, "check"))
            {
                var vulnerable = plant != null ? plant.GetComponent<PressureVulnerable>() : null;
                Record("the plant reads the owner's air as safe", vulnerable != null && vulnerable.IsNormal && vulnerable.testAreaElementSafe,
                    vulnerable == null ? "no plant" : $"pressure normal {vulnerable.IsNormal}, atmosphere safe {vulnerable.testAreaElementSafe}");
                var thin = thinPlant != null ? thinPlant.GetComponent<PressureVulnerable>() : null;
                Record("the plant in thin air reads low pressure", thin != null && !thin.IsNormal,
                    thin == null ? "no plant" : $"pressure normal {thin.IsNormal}, cell {CellMass(Grid.PosToCell(thinPlant)):G3} kg, warning below {thin.pressureWarning_Low:G3} kg");
                Finish();
            }
        }

        // A Wheezewort draws the owner's gas through its consumer and returns it
        // colder: what it took is back in the arena or held by the plant.
        private static GameObject? wheezewort;
        private static void Wheezewort(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "plant"))
            {
                recorded["gas"] = ArenaMass(); recorded["events"] = ColonyActors.Delivered;
                wheezewort = GameUtil.KInstantiate(Assets.GetPrefab(ColdBreatherConfig.ID), Grid.CellToPosCBC(Cell(3, 1), Grid.SceneLayer.BuildingFront), Grid.SceneLayer.BuildingFront);
                wheezewort.SetActive(true);
            }
            if (Once(40, "check"))
            {
                var held = wheezewort == null ? 0 : wheezewort.GetComponents<Storage>().Sum(s => s.MassStored());
                Record("the owner runs the plant's consumer", ColonyActors.Delivered > recorded["events"], $"{ColonyActors.Delivered - recorded["events"]} consumed-mass events");
                Record("the plant returned the gas it took", Math.Abs(ArenaMass() + held - recorded["gas"]) < 1e-3 * recorded["gas"],
                    $"{recorded["gas"]:G6} -> {ArenaMass():G6} kg in the arena, {held:G4} kg held");
                Finish();
            }
        }

        // The dev life-support building's consumer and emitter run through the
        // owner: carbon dioxide goes in, oxygen comes out until the output cell
        // holds the emitter's maximum, and the building sees itself blocked.
        private static GameObject? lifeSupport;
        private static void LifeSupport(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.CarbonDioxide, 1.5f, 300f));
            if (Once(6, "build"))
            {
                recorded["oxygen"] = ArenaSpecies(SimHashes.Oxygen); recorded["co2"] = ArenaSpecies(SimHashes.CarbonDioxide);
                recorded["emitters"] = ColonyActors.Emitters; recorded["consumers"] = ColonyActors.Consumers; recorded["blocked"] = 0;
                lifeSupport = Build(DevLifeSupportConfig.ID, Cell(3, 1));
            }
            var emitter = lifeSupport != null ? lifeSupport.GetComponent<ElementEmitter>() : null;
            if (emitter != null && emitter.isEmitterBlocked) recorded["blocked"] = 1;
            if (Once(30, "check"))
            {
                var oxygen = ArenaSpecies(SimHashes.Oxygen) - recorded["oxygen"]; var co2 = ArenaSpecies(SimHashes.CarbonDioxide) - recorded["co2"];
                Record("the owner runs the building's emitter and consumer", ColonyActors.Emitters > recorded["emitters"] && ColonyActors.Consumers > recorded["consumers"] && ColonyActors.Native == 0,
                    $"{ColonyActors.Emitters} emitters and {ColonyActors.Consumers} consumers owned, {ColonyActors.Native} native");
                Record("oxygen came out and carbon dioxide went in", oxygen > 1 && co2 < -1, $"oxygen {oxygen:+0.###;-0.###} kg, carbon dioxide {co2:+0.###;-0.###} kg");
                Record("the emitter saw its full cell", recorded["blocked"] == 1, $"blocked {emitter?.isEmitterBlocked}, maximum {emitter?.maxPressure} kg");
                Finish();
            }
        }

        // An oxygen mask station draws the owner's oxygen into its storage.
        private static GameObject? station;
        private static void MaskStation(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1.5f, 300f));
            if (Once(6, "build"))
            {
                recorded["oxygen"] = ArenaSpecies(SimHashes.Oxygen); recorded["receipts"] = ColonyActors.DeliveredKg; recorded["mass"] = ArenaMass();
                station = Build(OxygenMaskStationConfig.ID, Cell(3, 1));
            }
            if (Once(36, "check"))
            {
                var held = station == null ? 0 : station.GetComponents<Storage>().Sum(s => s.GetMassAvailable(SimHashes.Oxygen));
                var taken = recorded["oxygen"] - ArenaSpecies(SimHashes.Oxygen);
                Record("the station filled with the owner's oxygen", held > .01 && Math.Abs(taken - held) < .01 * held + 1e-4,
                    $"took {taken:G4} kg, holds {held:G4} kg, receipts {ColonyActors.DeliveredKg - recorded["receipts"]:G4} kg, arena mass {recorded["mass"]:G6} -> {ArenaMass():G6} kg; " +
                    $"storages {string.Join(" ", station == null ? new string[0] : station.GetComponents<Storage>().Select(st => $"{st.MassStored():G4}/{st.capacityKg:G4}"))}; {MachineState(station)}");
                Finish();
            }
        }

        // A polymer press turns stored petroleum into plastic and exhausts its
        // steam and carbon dioxide through a pipe and a vent into the owner.
        private static GameObject? press;
        private static void PolymerPress(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .5f, 300f));
            if (Once(6, "build"))
            {
                recorded["out"] = ArenaSpecies(SimHashes.Steam) + ArenaSpecies(SimHashes.Water) + ArenaSpecies(SimHashes.CarbonDioxide);
                press = Build(PolymerizerConfig.ID, Cell(3, 1));
                press.GetComponent<Storage>().AddLiquid(SimHashes.Petroleum, 20, 300, byte.MaxValue, 0);
                // The press runs only with its exhaust piped: a gas pipe to a vent.
                var vent = Build(GasVentConfig.ID, Cell(6, 1));
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, Route(
                    Grid.OffsetCell(Grid.PosToCell(press), press.GetComponent<Building>().Def.UtilityOutputOffset),
                    Grid.OffsetCell(Grid.PosToCell(vent), vent.GetComponent<Building>().Def.UtilityInputOffset)));
            }
            if (Once(90, "check"))
            {
                var added = ArenaSpecies(SimHashes.Steam) + ArenaSpecies(SimHashes.Water) + ArenaSpecies(SimHashes.CarbonDioxide) - recorded["out"];
                var left = press == null ? 0 : press.GetComponents<Storage>().Sum(s => s.GetMassAvailable(SimHashes.Petroleum));
                Record("the press's exhaust reached the owner", added > 1e-3, $"+{added:G4} kg steam, water and carbon dioxide; petroleum left {left:G4} kg; {MachineState(press)}");
                Finish();
            }
        }

        // A wild oxyfern draws the owner's carbon dioxide through its consumer.
        private static GameObject? fern;
        private static void Oxyfern(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Dirt, 1000f, 300f) : (SimHashes.CarbonDioxide, 1.5f, 300f));
            if (Once(6, "plant"))
            {
                recorded["co2"] = ArenaSpecies(SimHashes.CarbonDioxide); recorded["oxygen"] = ArenaSpecies(SimHashes.Oxygen); recorded["events"] = ColonyActors.Delivered;
                fern = GameUtil.KInstantiate(Assets.GetPrefab(OxyfernConfig.ID), Grid.CellToPosCBC(Cell(3, 1), Grid.SceneLayer.BuildingFront), Grid.SceneLayer.BuildingFront);
                fern.SetActive(true);
            }
            if (Once(36, "check"))
            {
                var taken = recorded["co2"] - ArenaSpecies(SimHashes.CarbonDioxide); var oxygen = ArenaSpecies(SimHashes.Oxygen) - recorded["oxygen"];
                Record("the fern took carbon dioxide from the owner", ColonyActors.Delivered > recorded["events"] && taken > 0,
                    $"{ColonyActors.Delivered - recorded["events"]} consumed-mass events, carbon dioxide -{taken:G4} kg, oxygen {oxygen:+0.####;-0.####} kg");
                Finish();
            }
        }

        // Hot debris on the floor warms the owner's gas through the native shadow.
        private static void DebrisHeat(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "drop"))
            {
                recorded["t"] = GasTemperature(1);
                ElementLoader.FindElementByHash(SimHashes.IronOre).substance.SpawnResource(Grid.CellToPosCCC(Cell(3, 1), Grid.SceneLayer.Ore), 200, 500, byte.MaxValue, 0);
            }
            if (Once(30, "check"))
            {
                var t = GasTemperature(1);
                Record("the debris warmed the air around it", t > recorded["t"] + .05, $"{recorded["t"]:F3} -> {t:F3} K");
                Record("the owner's energy ledger stayed closed", status.MaximumRelativeEnergyError < 1e-9, $"{status.MaximumRelativeEnergyError:G3}");
                Finish();
            }
        }

        // A melting building (the game's StructureTemperatureComponents.DoMelt,
        // as when it overheats past its melting point) gives its metal to the
        // owner as liquid.
        private static GameObject? ladder;
        private static int ladderMeltHash;
        private static PipeAmount ladderInjectionBefore,ladderInjectionByDeadline;
        private static ulong ladderProofFrame,ladderLastProofPublished;
        private static bool ladderProofPassed;
        private static double ladderOwnedLiquidByDeadline;
        [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
        private struct MeltSampleCell { internal uint Cell,First,Count,Flags;internal double Temperature; }
        [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
        private struct MeltSamplePart { internal uint NativeIndex,Reserved;internal double Mass; }
        [System.Runtime.InteropServices.DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_colony_sample",CallingConvention=System.Runtime.InteropServices.CallingConvention.Cdecl)]
        private static extern int SampleMelt([System.Runtime.InteropServices.In] uint[] cells,uint count,out OniNativeColony.Status source,
            [System.Runtime.InteropServices.Out] MeltSampleCell[] rows,[System.Runtime.InteropServices.Out] MeltSamplePart[] parts,uint capacity,out uint countParts,out double bookedMass);
        private static double OwnedMeltSample(ulong frame)
        {
            var cells=new uint[Width*Height];
            for(var y=0;y<Height;++y)for(var x=0;x<Width;++x)cells[y*Width+x]=(uint)Cell(x,y);
            var rows=new MeltSampleCell[cells.Length];var parts=new MeltSamplePart[checked(cells.Length*ColonyOwner.Materials.Length)];
            OniNativeColony.Check(SampleMelt(cells,(uint)cells.Length,out var source,rows,parts,(uint)parts.Length,out var count,out _));
            if(source.CompletedFrames!=frame)throw new InvalidOperationException($"Melt sample frame {source.CompletedFrames} differs from ledger frame {frame}.");
            var native=ElementLoader.FindElementByHash((SimHashes)ladderMeltHash).idx;double total=0;
            for(var k=0U;k<count;++k)if(parts[k].NativeIndex==native)total+=parts[k].Mass;
            return total;
        }
        private static PipeAmount AdmittedMaterial(byte[] image,int hash)
        {
            var value=ColonyCheckpointMetadata.InjectedMass(image,hash);
            return new PipeAmount(value.High).Plus(new PipeAmount(value.Low));
        }
        private static double LocalLeadInventory()
        {
            var fluid=ArenaSpecies((SimHashes)ladderMeltHash);
            double solid=0;
            for(var y=0;y<Height;++y)for(var x=0;x<Width;++x)
                if(Grid.Solid[Cell(x,y)]&&Grid.Element[Cell(x,y)].id==SimHashes.Lead)solid+=Grid.Mass[Cell(x,y)];
            var debris=Components.Pickupables.Items.Where(item=>item!=null&&item.GetComponent<PrimaryElement>() is PrimaryElement element&&
                    (element.ElementID==SimHashes.Lead||(int)element.ElementID==ladderMeltHash))
                .Where(item=>{Grid.CellToXY(Grid.PosToCell(item),out var x,out var y);return x>=x0&&x<x0+Width&&y>=y0&&y<y0+Height;})
                .Sum(item=>(double)item.GetComponent<PrimaryElement>().Mass);
            return fluid+solid+debris;
        }
        private static void BuildingMelt(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "build"))
                ladder = Assets.GetBuildingDef(LadderConfig.ID).Build(Cell(3, 1), Orientation.Neutral, null, new[] { SimHashes.Lead.CreateTag() }, 300, playsound: false);
            if (Once(8, "melt"))
            {
                var element = ladder != null ? ladder.GetComponent<PrimaryElement>() : null;
                if (element == null) { Record("the ladder was built", false, "no ladder"); Finish(); return; }
                element.Temperature = element.Element.highTemp + 50;
                recorded["mass"] = element.Mass; recorded["injected"] = status.InjectedKg;
                ladderMeltHash=(int)element.Element.highTempTransitionTarget;
                var before=ColonyOwner.Checkpoint();var sourceFrame=ColonyCheckpointMetadata.CompletedFrames(before);
                if(sourceFrame>started+9)throw new InvalidOperationException("Melt source baseline is past its original setup frame.");
                ladderInjectionBefore=AdmittedMaterial(before,ladderMeltHash);recorded["local lead"]=LocalLeadInventory();
                var ownedBefore=OwnedMeltSample(sourceFrame);
                Record("the ladder is the arena's metal source",recorded["local lead"]==0&&ownedBefore==0,
                    $"native frame {sourceFrame-started}, local lead {recorded["local lead"]:R} kg, owned liquid {ownedBefore:R} kg");
                ladderProofFrame=ladderLastProofPublished=0;ladderProofPassed=false;ladderOwnedLiquidByDeadline=0;ladderInjectionByDeadline=PipeAmount.Zero;
                AccessTools.Method(typeof(StructureTemperatureComponents), "DoMelt").Invoke(null, new object[] { element });
            }
            // The owner may be one frame ahead. A coherent source observation
            // by native14 proves admission without requiring it to remain liquid
            // after its original deadline. Sample each publication at most once.
            if(frame>=9&&frame<=13&&!ladderProofPassed&&status.CompletedFrames!=ladderLastProofPublished)
            {
                ladderLastProofPublished=status.CompletedFrames;
                var image=ColonyOwner.Checkpoint();ladderProofFrame=ColonyCheckpointMetadata.CompletedFrames(image);
                if(ladderProofFrame>started+14)throw new InvalidOperationException("Melt source proof exceeded its original14-frame deadline.");
                ladderInjectionByDeadline=AdmittedMaterial(image,ladderMeltHash).Minus(ladderInjectionBefore);
                ladderOwnedLiquidByDeadline=OwnedMeltSample(ladderProofFrame);
                // A canonical injection credit proves liquid entered the owner
                // even if it has already frozen in this completed publication.
                ladderProofPassed=Math.Abs(ladderInjectionByDeadline.Value-recorded["mass"])<.01*recorded["mass"];
            }
            if (Once(14, "check"))
            {
                var added=ladderInjectionByDeadline.Value;
                var local=LocalLeadInventory()-recorded["local lead"];
                Record("the melted ladder's metal reached the owner", ladder == null && Math.Abs(added - recorded["mass"]) < .01 * recorded["mass"]&&
                    Math.Abs(local-recorded["mass"])<.01*recorded["mass"]&&ladderProofPassed&&ladderProofFrame>0&&ladderProofFrame<=started+14,
                    $"ladder {(ladder == null ? "gone" : "still standing")}, {recorded["mass"]:G4} kg melted, {added:G4} kg admitted metal, {local:G4} kg local liquid/solid/debris; {ladderOwnedLiquidByDeadline:G4} kg owned liquid at native frame {ladderProofFrame-started}; all-fluid injection {status.InjectedKg-recorded["injected"]:G4} kg");
                Finish();
            }
        }

        // A steam geyser shifted to its active period erupts into the owner.
        private static GameObject? geyser;
        private static void Geyser(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .3f, 300f));
            if (Once(6, "spawn"))
            {
                recorded["water"] = ArenaSpecies(SimHashes.Steam) + ArenaSpecies(SimHashes.Water);
                geyser = GameUtil.KInstantiate(Assets.GetPrefab(GeyserGenericConfig.ID + "_" + GeyserGenericConfig.Steam),
                    Grid.CellToPosCBC(Cell(3, 1), Grid.SceneLayer.BuildingBack), Grid.SceneLayer.BuildingBack);
                geyser.SetActive(true);
            }
            // Its configuration exists once it has spawned (a frame later).
            if (Once(7, "erupt")) geyser!.GetComponent<global::Geyser>().ShiftTimeTo(global::Geyser.TimeShiftStep.ActiveState);
            if (Once(40, "check"))
            {
                var added = ArenaSpecies(SimHashes.Steam) + ArenaSpecies(SimHashes.Water) - recorded["water"];
                Record("the geyser's steam reached the owner", added > .01, $"+{added:G4} kg steam and water");
                var sources = string.Join("; ", ColonyActors.SourceKg.Select(s => s.Key + " " + string.Join(",", s.Value.Select(e => e.Key + " " + e.Value.ToString("G4")))));
                Record("the geyser is named in the source ledger", ColonyActors.SourceKg.Keys.Any(k => k.StartsWith(GeyserGenericConfig.ID, StringComparison.Ordinal)), sources);
                Finish();
            }
        }

        // A steam rocket engine put into its burning state (as at launch)
        // exhausts hot steam into the owner.
        private static RocketEngine? engine;
        private static void RocketExhaust(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, .3f, 300f));
            if (Once(6, "burn"))
            {
                recorded["steam"] = ArenaSpecies(SimHashes.Steam) + ArenaSpecies(SimHashes.Water);
                engine = Build(SteamEngineConfig.ID, Cell(3, 1)).GetComponent<RocketEngine>();
            }
            // Its state machine exists once it has spawned (a frame later).
            if (Once(7, "ignite")) { var smi = engine!.GetSMI<RocketEngine.StatesInstance>(); smi.GoTo(smi.sm.burning); }
            if (Once(30, "check"))
            {
                var added = ArenaSpecies(SimHashes.Steam) + ArenaSpecies(SimHashes.Water) - recorded["steam"];
                Record("the engine's exhaust reached the owner", added > .01, $"+{added:G4} kg steam and water");
                Finish();
            }
        }

        // A rock meteor falls into the arena's air and strikes its floor; the
        // owner keeps its ledgers through the impact.
        private static void Meteor(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "spawn"))
            {
                var comet = GameUtil.KInstantiate(Assets.GetPrefab(RockCometConfig.ID), Grid.CellToPosCCC(Cell(3, Height - 1), Grid.SceneLayer.FXFront),
                    Grid.SceneLayer.FXFront);
                comet.SetActive(true); recorded["spawned"] = 1;
                meteor = comet;
            }
            if (Once(30, "check"))
            {
                Record("the meteor struck", meteor == null, meteor == null ? "impact" : "still falling");
                Record("the owner's ledgers stayed closed", status.MaximumRelativeMassError < 1e-9 && status.MaximumRelativeEnergyError < 1e-9,
                    $"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}");
                Finish();
            }
        }
        private static GameObject? meteor;

        // A gas element sensor set to carbon dioxide reads a cell where carbon
        // dioxide is a minority of the mixture (ONI's own test sees only oxygen).
        private static LogicElementSensor? sensor;
        private static void Sensor(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(4, "mix"))
                for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
                    SimMessages.AddRemoveSubstance(Cell(x, y), SimHashes.CarbonDioxide, CellEventLogger.Instance.OxygenModifierSimUpdate, .05f, 300, byte.MaxValue, 0);
            if (Once(8, "build")) sensor = Build(LogicElementSensorGasConfig.ID, Cell(3, 2)).GetComponent<LogicElementSensor>();
            // Spawning applies the sensor's filter, so the element is chosen a frame later.
            if (Once(9, "select")) sensor!.GetComponent<Filterable>().SelectedTag = ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).tag;
            // The sensor decides once per nine 200 ms samples, and its first
            // window may hold samples taken before its element was chosen:
            // the check waits up to two further windows for it to switch.
            if (frame >= 26 && (sensor != null && sensor.IsSwitchedOn || frame >= 44) && Once(frame, "check"))
            {
                var on = sensor != null && sensor.IsSwitchedOn;
                var samples = sensor == null ? "-" : string.Join("", (Traverse.Create(sensor).Field("samples").GetValue() as bool[] ?? new bool[0]).Select(s => s ? "1" : "0"));
                var desiredIdx = sensor == null ? -1 : Convert.ToInt32(Traverse.Create(sensor).Field("desiredElementIdx").GetValue());
                Record("the sensor found the minority carbon dioxide", on && Grid.Element[Cell(3, 2)].id == SimHashes.Oxygen,
                    $"switched on {on}, cell shows {Grid.Element[Cell(3, 2)].id}, CO2 {SpeciesMass(Cell(3, 2), SimHashes.CarbonDioxide):G3} kg, " +
                    $"present {ColonySensors.Present(Cell(3, 2), desiredIdx)}, owned {ColonyOwner.OwnsFluidCell(Cell(3, 2))}, samples {samples} at {(sensor == null ? "-" : Traverse.Create(sensor).Field("sampleIdx").GetValue())}, " +
                    $"sensing {(sensor != null ? ElementLoader.elements[Convert.ToInt32(Traverse.Create(sensor).Field("desiredElementIdx").GetValue())].id.ToString() : "-")}");
                Finish();
            }
        }

        // A pump sends the left chamber's oxygen and carbon dioxide through a
        // carbon dioxide filter: its filtered port carries only carbon dioxide
        // and its main output only oxygen.
        private static readonly List<int> filteredPipe = new List<int>(), passedPipe = new List<int>(), inputPipe = new List<int>();
        private static GameObject? filterBuilding;
        private static void Filter(int frame)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : x > 4 ? (SimHashes.Vacuum, 0f, 0f) :
                    ((x + y) % 2 == 0 ? (SimHashes.Oxygen, 1.2f, 300f) : (SimHashes.CarbonDioxide, 1.6f, 300f)));
            if (Once(6, "build"))
            {
                filteredPipe.Clear(); passedPipe.Clear();
                Build(GasPumpConfig.ID, Cell(1, 0));
                filterBuilding = Build(GasFilterConfig.ID, Cell(6, 3));
                // Each port's pipe runs straight down to its own vent: a route
                // crossing the input pipe would join the two networks.
                Build(GasVentConfig.ID, Cell(7, 0)); Build(GasVentConfig.ID, Cell(6, 0));
            }
            // The filter knows its port cells once it has spawned (a frame later).
            if (Once(7, "pipes"))
            {
                var filter = filterBuilding!;
                filter.GetComponent<Filterable>().SelectedTag = ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).tag;
                var into = (int)AccessTools.Field(typeof(ElementFilter), "inputCell").GetValue(filter.GetComponent<ElementFilter>());
                var output = (int)AccessTools.Field(typeof(ElementFilter), "outputCell").GetValue(filter.GetComponent<ElementFilter>());
                var filtered = (int)AccessTools.Field(typeof(ElementFilter), "filteredCell").GetValue(filter.GetComponent<ElementFilter>());
                if (!InArena(into) || !InArena(output) || !InArena(filtered)) throw new InvalidOperationException($"filter ports outside the arena: {into} {output} {filtered}");
                var pump = powered[0]; var a = powered.Single(b => b != null && b.GetComponent<Vent>() != null && Grid.PosToCell(b) == Cell(7, 0));
                var b = powered.Single(v => v != null && v.GetComponent<Vent>() != null && Grid.PosToCell(v) == Cell(6, 0));
                inputPipe.Clear(); inputPipe.AddRange(Route(Grid.OffsetCell(Grid.PosToCell(pump), pump.GetComponent<Building>().Def.UtilityOutputOffset), into));
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, inputPipe);
                passedPipe.AddRange(Route(output, Grid.OffsetCell(Grid.PosToCell(a), a.GetComponent<Building>().Def.UtilityInputOffset)));
                filteredPipe.AddRange(Route(filtered, Grid.OffsetCell(Grid.PosToCell(b), b.GetComponent<Building>().Def.UtilityInputOffset)));
                if (inputPipe.Intersect(passedPipe).Any() || inputPipe.Intersect(filteredPipe).Any() || passedPipe.Intersect(filteredPipe).Any())
                    throw new InvalidOperationException("The filter's three pipes share a cell.");
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, passedPipe);
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, filteredPipe);
            }
            // ONI's pipes move a packet a segment per second.
            if (Once(100, "check"))
            {
                var flow = Game.Instance.gasConduitFlow;
                (double selected, double other) PortMass(IEnumerable<int> path, SimHashes expected)
                {
                    double selected = 0, other = 0;
                    foreach (var c in path)
                        if (ColonyConduits.TryRead(flow, c, out var inventory))
                        {
                            // A scalar projection can hide a wrong minority.
                            // Purity observes every authoritative constituent.
                            foreach (var part in inventory.Parts)
                                if (ColonyConduits.ElementHash(part.Id) == expected) selected += part.Mass;
                                else other += part.Mass;
                        }
                        else
                        {
                            var packet = flow.GetContents(c);
                            if (packet.element == expected) selected += packet.mass;
                            else if (packet.mass > 0) other += packet.mass;
                        }
                    return (selected, other);
                }
                var (co2, o2InFiltered) = PortMass(filteredPipe, SimHashes.CarbonDioxide);
                var (o2, co2InPassed) = PortMass(passedPipe, SimHashes.Oxygen);
                var vented = ArenaSpecies(SimHashes.CarbonDioxide, 5) + ArenaSpecies(SimHashes.Oxygen, 5);
                Record("the filter separated the pumped mixture", (co2 > 0 || vented > .05) && o2InFiltered == 0 && co2InPassed == 0,
                    $"filtered port: CO2 {co2:G3} kg, other {o2InFiltered:G3} kg; output: O2 {o2:G3} kg, other {co2InPassed:G3} kg; vented {vented:G3} kg; " +
                    $"input pipe {inputPipe.Sum(c => ColonyConduits.TryRead(flow, c, out var held) ? held.Mass : flow.GetContents(c).mass):G3} kg; pump {MachineState(powered.FirstOrDefault())}; filter {MachineState(filterBuilding)}");
                Finish();
            }
        }
        // What a building holds: each storage's native items or, for a pump
        // or vent storage owned by mixed pipes (whose native storage writes are
        // suppressed), its owned inventory instead, so nothing counts twice.
        private static double HeldMass(GameObject building) => building.GetComponents<Storage>().Sum(st =>
            ColonyPipePumps.Owns(st) ? ColonyPipePumps.Inventory(st).Mass : ColonyPipeVents.Owns(st) ? ColonyPipeVents.Inventory(st).Mass :
            st.items.Where(i => i != null).Sum(i => (double)(i.GetComponent<PrimaryElement>()?.Mass ?? 0)));
        private static bool InArena(int cell)
        {
            if (!Grid.IsValidCell(cell)) return false;
            Grid.CellToXY(cell, out var x, out var y);
            return x >= x0 && x < x0 + Width && y >= y0 && y < y0 + Height;
        }
        // An L-shaped pipe route from one cell to another, both included.
        private static List<int> Route(int from, int to)
        {
            Grid.CellToXY(from, out var fx, out var fy); Grid.CellToXY(to, out var tx, out var ty);
            var path = new List<int>();
            for (var x = fx; x != tx; x += Math.Sign(tx - fx)) path.Add(Grid.XYToCell(x, fy));
            for (var y = fy; y != ty; y += Math.Sign(ty - fy)) path.Add(Grid.XYToCell(tx, y));
            path.Add(to);
            return path;
        }
        private static double ArenaSpecies(SimHashes element, int fromColumn) =>
            Enumerable.Range(fromColumn, Width - fromColumn).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), element)));

        // A pacu in an owned pool stays in its water.
        private static GameObject? pacu;
        private static void Pacu(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y <= 3 ? (SimHashes.Water, 990f, 300f) : (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "spawn"))
            {
                pacu = Util.KInstantiate(Assets.GetPrefab(PacuConfig.ID), Grid.CellToPosCBC(Cell(3, 1), Grid.SceneLayer.Creatures));
                pacu.SetActive(true);
            }
            if (Once(30, "check"))
            {
                var cell = pacu != null ? Grid.PosToCell(pacu) : -1;
                Record("the pacu swims in the owner's water", pacu != null && Grid.IsValidCell(cell) && Grid.IsSubstantialLiquid(cell),
                    pacu == null ? "gone" : $"at {cell}: {Grid.Element[cell].id} {Grid.Mass[cell]:G4} kg");
                Finish();
            }
        }

        // A liquid pump in a pool sends its water through a pipe into a
        // reservoir: what the owner lost is in the pump, the pipe and the reservoir.
        private static void LiquidPump(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y <= 2 ? (SimHashes.Water, 990f, 300f) : (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "build"))
            {
                recorded["water"] = ArenaSpecies(SimHashes.Water);
                var pump = Build(LiquidPumpConfig.ID, Cell(2, 0)); var tank = Build(LiquidReservoirConfig.ID, Cell(6, 3));
                var path = Route(Grid.OffsetCell(Cell(2, 0), pump.GetComponent<Building>().Def.UtilityOutputOffset),
                    Grid.OffsetCell(Cell(6, 3), tank.GetComponent<Building>().Def.UtilityInputOffset));
                Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, path); pipeCells.AddRange(path);
            }
            if (Once(30, "check"))
            {
                var held = powered.Where(b => b != null).Sum(b => HeldMass(b!));
                var inPipe = pipeCells.Sum(c => (double)Game.Instance.liquidConduitFlow.GetContents(c).mass);
                var drawn = recorded["water"] - ArenaSpecies(SimHashes.Water);
                Record("the pump drew the pool's water", drawn > 1 && Math.Abs(held + inPipe - drawn) <= .02 * drawn + .01,
                    $"drawn {drawn:G4} kg, in storage {held:G4} kg, in the pipe {inPipe:G4} kg; {MachineState(powered.FirstOrDefault())}");
                Finish();
            }
        }

        // Life support for an unattended colony (the prepared save of
        // docs/playtest-plan.md#ready-to-play), built around the printing pod:
        // the pod's room is widened and floored; a coal generator powers an
        // oxygen diffuser and two carbon dioxide scrubbers, one by the
        // generator and one where the duplicants eat and sleep (carbon dioxide
        // spreads slowly along the base); the scrubbers take water from a
        // reservoir and return polluted water each to its own; a
        // ration box holds the food. The duplicants supply coal and algae
        // from piles beside the machines. Harness only: instant building.
        private static readonly List<string> placed = new List<string>();
        private static void Base(int frame, OniNativeColony.Status status)
        {
            var pad = GameUtil.GetActiveTelepad();
            Grid.CellToXY(Grid.PosToCell(pad), out var px, out var py);
            if (Once(0, "dig"))
            {
                Current.Stage = $"pod at {px},{py}";
                for (var y = py; y <= py + 3; ++y)
                    for (var x = px - 24; x <= px + 24; ++x)
                    {
                        var cell = Grid.XYToCell(x, y);
                        if (Grid.IsValidCell(cell) && Grid.Solid[cell] && Grid.Element[cell].hardness < 150 && Grid.Objects[cell, (int)ObjectLayer.Building] == null)
                            SimMessages.Dig(cell);
                    }
                // Loose ground (sand, regolith) resting on the room would fall
                // in and entomb the machines: dig its columns out too.
                for (var x = px - 24; x <= px + 24; ++x)
                    for (var y = py + 4; ; ++y)
                    {
                        var cell = Grid.XYToCell(x, y);
                        if (!Grid.IsValidCell(cell) || !Grid.Solid[cell] || !Grid.Element[cell].IsUnstable || Grid.Objects[cell, (int)ObjectLayer.Building] != null) break;
                        SimMessages.Dig(cell);
                    }
            }
            if (Once(4, "floor"))
                for (var x = px - 24; x <= px + 24; ++x)
                {
                    var cell = Grid.XYToCell(x, py - 1);
                    if (Grid.IsValidCell(cell) && !Grid.Solid[cell] && Grid.Objects[cell, (int)ObjectLayer.Building] == null)
                        Assets.GetBuildingDef(TileConfig.ID).Build(cell, Orientation.Neutral, null, new[] { SimHashes.Granite.CreateTag() }, 300, playsound: false);
                }
            if (Once(8, "build"))
            {
                placed.Clear();
                var left = px - 3; var right = px + 3;
                // Left of the pod: generator, a carbon dioxide scrubber beside it
                // with its own waste reservoir (gases mix but spread slowly
                // along the base, so a scrubber sits by each source), diffuser,
                // ration box, coal and algae, enough for the ten-cycle run with
                // margin (the generator burns its coal whenever its loads draw
                // power).
                var generator = Place(GeneratorConfig.ID, ref left, -1);
                var scrubber = Place(CO2ScrubberConfig.ID, ref left, -1);
                var waste = Place(LiquidReservoirConfig.ID, ref left, -1);
                var diffuser = Place(MineralDeoxidizerConfig.ID, ref left, -1);
                var rations = Place(RationBoxConfig.ID, ref left, -1);
                Spill(SimHashes.Carbon, left, py, 5000); Spill(SimHashes.Carbon, left - 1, py, 5000); left -= 3;
                Spill(SimHashes.Algae, left, py, 4500); Spill(SimHashes.Algae, left - 1, py, 4500);
                // Right of the pod, where the duplicants sleep: the water
                // reservoir, the second scrubber with its own waste reservoir,
                // and a cot each (stress over the ten cycles); a cot that finds
                // no room goes left of the piles. Each scrubber has its own
                // waste line: polluted water from one output would back up into
                // the other's.
                var water = Place(LiquidReservoirConfig.ID, ref right, 1);
                var scrubber2 = Place(CO2ScrubberConfig.ID, ref right, 1);
                var waste2 = Place(LiquidReservoirConfig.ID, ref right, 1);
                left -= 2;
                for (var bed = 0; bed < 3; ++bed) if (Place(BedConfig.ID, ref right, 1) == null) Place(BedConfig.ID, ref left, -1);
                water?.GetComponent<Storage>().AddLiquid(SimHashes.Water, 4500, 300, byte.MaxValue, 0, false, true);
                if (rations != null)
                {
                    var food = GameUtil.KInstantiate(Assets.GetPrefab(FieldRationConfig.ID), rations.transform.GetPosition(), Grid.SceneLayer.Ore);
                    food.SetActive(true); food.GetComponent<PrimaryElement>().Units = 60;
                    rations.GetComponent<Storage>().Store(food, true);
                }
                if (generator != null)
                {
                    var power = Grid.OffsetCell(Grid.PosToCell(generator), generator.GetComponent<Building>().Def.PowerOutputOffset);
                    Wire(new[] { diffuser, scrubber, scrubber2 }.Where(load => load != null)
                        .Select(load => Route(power, Grid.OffsetCell(Grid.PosToCell(load), load!.GetComponent<Building>().Def.PowerInputOffset))).ToArray());
                }
                if (water != null)
                    foreach (var (filter, drain) in new[] { (scrubber, waste), (scrubber2, waste2) })
                    {
                        if (filter == null || drain == null) continue;
                        var def = filter.GetComponent<Building>().Def;
                        Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, Route(
                            Grid.OffsetCell(Grid.PosToCell(water), water.GetComponent<Building>().Def.UtilityOutputOffset), Grid.OffsetCell(Grid.PosToCell(filter), def.UtilityInputOffset)));
                        Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, Route(
                            Grid.OffsetCell(Grid.PosToCell(filter), def.UtilityOutputOffset), Grid.OffsetCell(Grid.PosToCell(drain), drain.GetComponent<Building>().Def.UtilityInputOffset)));
                    }
                Current.Stage = "built " + string.Join(", ", placed);
            }
            if (Once(30, "check"))
            {
                Record("the base was built", placed.Count == 11, string.Join(", ", placed));
                // Its life support is wired: the generator's circuit reaches both
                // loads (power flows once the duplicants have fed it coal).
                var loads = Components.BuildingCompletes.Items.Where(b => b.PrefabID().Name == MineralDeoxidizerConfig.ID || b.PrefabID().Name == CO2ScrubberConfig.ID).ToList();
                var circuit = Components.BuildingCompletes.Items.FirstOrDefault(b => b.PrefabID().Name == GeneratorConfig.ID)?.GetComponent<Generator>()?.CircuitID ?? ushort.MaxValue;
                Record("the life support shares the generator's circuit", loads.Count == 3 && circuit != ushort.MaxValue &&
                    loads.All(b => b.GetComponent<EnergyConsumer>()?.CircuitID == circuit),
                    $"generator circuit {circuit}; " + string.Join("; ", loads.Select(b => b.PrefabID().Name + ": " + MachineState(b.gameObject))));
                var buried = Components.BuildingCompletes.Items.Where(b => b.GetComponent<EntombVulnerable>() != null).Select(b => (b.PrefabID().Name, cells: Entombing(b.gameObject))).Where(b => b.cells.Length > 0).ToList();
                Record("no machine is entombed", buried.Count == 0, string.Join("; ", buried.Select(b => $"{b.Name}: {b.cells}")));
                Record("the owner's ledgers stayed closed", status.MaximumRelativeMassError < 1e-9 && status.MaximumRelativeEnergyError < 1e-9,
                    $"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}");
                Finish();
            }
        }
        // Places a building on the floor row next to the last one, walking
        // away from the pod; null if the row has no room.
        private static GameObject? Place(string id, ref int x, int step)
        {
            var def = Assets.GetBuildingDef(id);
            var pad = GameUtil.GetActiveTelepad(); Grid.CellToXY(Grid.PosToCell(pad), out _, out var py);
            for (var tries = 0; tries < 16; ++tries, x += step)
            {
                // The origin is the bottom middle (left of middle for even widths).
                var x0 = x - (def.WidthInCells - 1) / 2; var fits = true;
                for (var dx = 0; dx < def.WidthInCells && fits; ++dx)
                {
                    var below = Grid.XYToCell(x0 + dx, py - 1); fits = Grid.IsValidCell(below) && Grid.Solid[below];
                    for (var dy = 0; dy < def.HeightInCells && fits; ++dy)
                    {
                        var c = Grid.XYToCell(x0 + dx, py + dy);
                        fits = Grid.IsValidCell(c) && !Grid.Solid[c] && Grid.Objects[c, (int)ObjectLayer.Building] == null;
                    }
                }
                if (!fits) continue;
                var building = def.Build(Grid.XYToCell(x, py), Orientation.Neutral, null, def.DefaultElements(), 300, playsound: false);
                placed.Add(id + "@" + x); x += step * (def.WidthInCells + 1);
                return building;
            }
            return null;
        }
        private static void Spill(SimHashes element, int x, int y, float mass) =>
            ElementLoader.FindElementByHash(element).substance.SpawnResource(Grid.CellToPosCCC(Grid.XYToCell(x, y), Grid.SceneLayer.Ore), mass, 300, byte.MaxValue, 0);
        private static void Wire(params List<int>[] paths) => Pipe(Game.Instance.electricalConduitSystem, WireConfig.ID, paths);

        private static double OwnedMass(SimHashes element)
        {
            double total = 0;
            for (var cell = 0; cell < Grid.CellCount; ++cell) if (ColonyOwner.OwnsFluidCell(cell)) total += SpeciesMass(cell, element);
            return total;
        }

        private static void MixedPipeCatalogue(int frame)
        {
            if(Once(0,"catalogue arena"))Arena((x,y)=>(SimHashes.Oxygen,1f,300f));
            if(Once(5,"catalogue cells"))
            {
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{Cell(3,5)});
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{Cell(3,5)});
            }
            if(Once(8,"all catalogue fluid routes"))
            {ColonyConduits.StartControl();ColonyPipePorts.StartControl();CataloguePipeRoutes();Finish();}
        }
        private static void CataloguePipeRoutes()
        {
            for(var phase=0;phase<2;++phase)
            {
                var entries=ColonyOwner.Materials.Where(m=>m.Phase==(uint)(phase+1)).OrderBy(m=>m.NativeIndex).ToArray();
                var inventory=PipeInventory.Empty;
                for(var i=0;i<entries.Length;++i)inventory=inventory.Joined(PipeInventory.Single(ColonyConduits.Material((int)entries[i].NativeIndex),1e-6,300,i%2,i%2==0?.25:1e-240));
                var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                ColonyConduits.SupplyControlInventory(flow,Cell(3,5),inventory);ColonyConduits.TryRead(flow,Cell(3,5),out var routed);
                var okay=routed.Count==entries.Length&&entries.Any(m=>m.ElementHash<0)&&entries.All(m=>routed.SpeciesMass((int)m.NativeIndex)>0&&ColonyConduits.ElementHash((int)m.NativeIndex)==(SimHashes)m.ElementHash);
                Record((phase==0?"all catalogue gases":"all catalogue liquids")+" retain native ordinal and hash routing",okay,$"{routed.Count}/{entries.Length} constituents; {entries.Count(m=>m.ElementHash<0)} negative hashes");
                Record((phase==0?"gas":"liquid")+" catalogue cell retains simultaneous germ identities",routed.Germs.Length==2,$"{routed.Germs.Length} germ identities");
                ColonyPipePorts.HoldForPort(flow,Cell(3,5),double.MaxValue);
            }
            Record("catalogue routes conserve every account",ColonyConduits.Audit(out var detail),detail);
        }
        private static readonly List<GameObject> mixedValves=new List<GameObject>();
        private static readonly Dictionary<int,string> valvePartialSetup=new Dictionary<int,string>();
        private static string valveNegativePhaseProof="",valvePositivePhaseProof="";
        private static int ValveCell(GameObject valve,string name)=>(int)AccessTools.Field(typeof(ValveBase),name).GetValue(valve.GetComponent<ValveBase>());
        private static bool ValveAt(int at,string label)=>Once(at+(recorded.TryGetValue("valve setup delay",out var delay)?(int)delay:0),label);
        private static void MixedPipeValves(int frame)
        {
            if(Once(0,"mixed valve arena"))
            {
                valvePartialSetup.Clear();valveNegativePhaseProof=valvePositivePhaseProof="";Arena((x,y)=>(SimHashes.Oxygen,1f,300f));
                var controlled=Environment.GetEnvironmentVariable("MA_COLONY_VALVE_SETUP_DELAY");
                if(!string.IsNullOrEmpty(controlled))
                {
                    if(controlled!="1")throw new InvalidOperationException("Only the diagnostic one-frame closed preparation offset is supported.");
                    recorded["valve diagnostic setup delay"]=1;
                    Record("diagnostic controlled closed setup phase",true,"MA_COLONY_VALVE_SETUP_DELAY=1; diagnostic only, not ordinary acceptance; relative stages and bounds unchanged");
                }
                var negative=Environment.GetEnvironmentVariable("MA_COLONY_VALVE_SETUP_PHASE");
                if(!string.IsNullOrEmpty(negative))
                {
                    if(negative!="miss-liquid"||!string.IsNullOrEmpty(controlled))throw new InvalidOperationException("Invalid or combined diagnostic valve phase controls.");
                    recorded["valve diagnostic negative phase"]=1;
                    Record("diagnostic closed callback phase selection",true,"MA_COLONY_VALVE_SETUP_PHASE=miss-liquid; diagnostic only, not ordinary acceptance; relative stages and bounds unchanged");
                }
            }
            if(Once(5,"build closed manual valves and powered shutoffs"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var row=phase==0?1:4;
                    var manual=Build(phase==0?"GasValve":"LiquidValve",Cell(1,row));manual.GetComponent<ValveBase>().CurrentFlow=0;mixedValves.Add(manual);
                    var automatic=Build(phase==0?"GasLogicValve":"LiquidLogicValve",Cell(5,row));
                    automatic.GetComponent<LogicOperationalController>().unNetworkedValue=1;
                    automatic.GetComponent<Operational>().SetFlag(BuildingEnabledButton.EnabledFlag,false);mixedValves.Add(automatic);
                }
            }
            if(Once(7,"connect initialized native valve ports"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var network=phase==0?Game.Instance.gasConduitSystem:Game.Instance.liquidConduitSystem;var id=phase==0?GasConduitConfig.ID:LiquidConduitConfig.ID;
                    foreach(var valve in mixedValves.Skip(phase*2).Take(2))Pipe(network,id,new List<int>{ValveCell(valve,"inputCell")},new List<int>{ValveCell(valve,"outputCell")});
                    Pipe(network,id,new List<int>{Cell(7,phase==0?1:4)});
                }
                ColonyValveControl.Begin(mixedValves.Select(v=>v.GetComponent<ValveBase>()));
            }
            if(frame>=10&&step<=10)
            {
                var ready=true;
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    foreach(var valve in mixedValves.Skip(phase*2).Take(2))ready&=flow.HasConduit(ValveCell(valve,"inputCell"))&&flow.HasConduit(ValveCell(valve,"outputCell"));
                }
                if(!ready){Current.Stage="waiting for native valve topology";if(frame>=30){Record("valve topology became ready",false,"readiness expired: "+string.Join("; ",mixedValves.Select(v=>$"{v.name} {ValveCell(v,"inputCell")}/{ValveCell(v,"outputCell")}")));Finish();}return;}
                if(recorded.TryGetValue("valve diagnostic setup delay",out var controlledDelay)&&frame<10+controlledDelay)
                {Current.Stage="diagnostic closed preparation phase";return;}
                if(recorded.ContainsKey("valve diagnostic negative phase")&&
                    !ColonyValveControl.NegativePartialPhase(mixedValves[2].GetComponent<ValveBase>(),ColonyOwner.Status.CompletedFrames,out valveNegativePhaseProof))
                {
                    Current.Stage="diagnostic closed callback phase preparation";
                    if(frame>=30){Record("diagnostic closed phase prepared before expiry",false,valveNegativePhaseProof);Finish();}
                    return;
                }
                if(!recorded.ContainsKey("valve diagnostic negative phase")&&!recorded.ContainsKey("valve diagnostic setup delay")&&
                    !ColonyValveControl.PositivePartialPhase(new[]{mixedValves[0].GetComponent<ValveBase>(),mixedValves[2].GetComponent<ValveBase>()},ColonyOwner.Status.CompletedFrames,out valvePositivePhaseProof))
                {
                    Current.Stage="waiting for native valve callback preparation phase";
                    if(frame>=30){Record("closed valve callback phase became ready",false,valvePositivePhaseProof);Finish();}
                    return;
                }
                recorded["valve setup delay"]=frame-10;
            }
            if(ValveAt(10,"seed complete valve input mixtures"))
            {
                if(recorded.TryGetValue("valve diagnostic setup delay",out var controlledDelay))
                    Record("diagnostic requested setup phase was achieved",recorded["valve setup delay"]==controlledDelay,
                        $"requested offset {controlledDelay}, actual {recorded["valve setup delay"]}; owner frame {ColonyOwner.Status.CompletedFrames}, clock {GameClock.Instance.GetTime():R}s");
                if(recorded.ContainsKey("valve diagnostic negative phase"))Record("diagnostic negative callback phase selected",true,valveNegativePhaseProof);
                if(!recorded.ContainsKey("valve diagnostic negative phase")&&!recorded.ContainsKey("valve diagnostic setup delay"))
                {recorded["valve positive phase ready"]=1;Record("closed preparation gives both partial-capacity windows a native callback",true,valvePositivePhaseProof);}
                ColonyConduits.StartControl();
                for(var phase=0;phase<2;++phase)foreach(var valve in mixedValves.Skip(phase*2).Take(2))SupplyValve(phase,valve);
            }
            if(ValveAt(20,"closed valves retain mixtures then open"))
            {
                recorded["valve open clock"]=GameClock.Instance.GetTime();
                ColonyValveControl.Begin(mixedValves.Select(v=>v.GetComponent<ValveBase>()));
                for(var phase=0;phase<2;++phase)for(var device=0;device<2;++device)
                {
                    var valve=mixedValves[phase*2+device];var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    ColonyConduits.TryRead(flow,ValveCell(valve,"inputCell"),out var input);ColonyConduits.TryRead(flow,ValveCell(valve,"outputCell"),out var output);
                    Record($"{phase}/{device} closed valve has zero leakage",input.Mass==(phase==0?1:10)&&output.Count==0,$"input {input.Mass:R}; output {output.Mass:R}");
                    if(device==0)valve.GetComponent<ValveBase>().CurrentFlow=phase==0?.1f:1f;
                    else
                    {
                        AccessTools.Method(typeof(LogicOperationalController),"CheckWireState").Invoke(valve.GetComponent<LogicOperationalController>(),null);
                        valve.GetComponent<Operational>().SetFlag(BuildingEnabledButton.EnabledFlag,true);
                    }
                }
            }
            if(ValveAt(30,"native rates move complete mixtures then close"))
            {
                for(var phase=0;phase<2;++phase)for(var device=0;device<2;++device)
                {
                    var valve=mixedValves[phase*2+device];var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    ColonyConduits.TryRead(flow,ValveCell(valve,"outputCell"),out var output);var expected=device==0?(phase==0?.2:2):(phase==0?1:10);
                    var bound=device==0?(phase==0?.021:.21):1e-6;
                    var nativeValve=valve.GetComponent<ValveBase>();
                    Record($"{phase}/{device} native valve rate is preserved",Math.Abs(output.Mass-expected)<=bound,$"{output.Mass:R} kg; expected {expected:R} ± {bound:R}; native rate/max {nativeValve.CurrentFlow:R}/{nativeValve.MaxFlow:R}, type {nativeValve.conduitType}, ports {ValveCell(valve,"inputCell")}/{ValveCell(valve,"outputCell")}, capacity {AccessTools.Field(typeof(ConduitFlow),"MaxMass").GetValue(flow)}, clock elapsed {GameClock.Instance.GetTime()-recorded["valve open clock"]:R}s; {ColonyValveControl.Describe(nativeValve)}; {MachineState(valve)}");
                    var a=ColonyConduits.MaterialOf(phase==0?SimHashes.Oxygen:SimHashes.Water).Id;var fraction=phase==0?.375:.3;
                    Record($"{phase}/{device} valve transfers the full composition",output.Count==2&&output.Mass>0&&Math.Abs(output.SpeciesMass(a)/output.Mass-fraction)<1e-10,$"{output.Mass:R} kg; {output.Count} species; first {output.SpeciesMass(a):R} kg");
                    recorded[$"valve output {phase}/{device}"]=output.Mass;
                    if(device==0)valve.GetComponent<ValveBase>().CurrentFlow=0;else valve.GetComponent<Operational>().SetFlag(BuildingEnabledButton.EnabledFlag,false);
                }
                // Keep the readonly clock observer through the partial-capacity window.
            }
            if(ValveAt(34,"partial manual output and real room behind disabled shutoff"))
            {
                for(var phase=0;phase<2;++phase)for(var device=0;device<2;++device)
                {
                    var valve=mixedValves[phase*2+device];var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    ColonyConduits.TryRead(flow,ValveCell(valve,"outputCell"),out var output);
                    Record($"{phase}/{device} closed valve stops further transfer",output.Mass==recorded[$"valve output {phase}/{device}"],$"{output.Mass:R} kg");
                    if(device==0)
                    {
                        var material=ColonyConduits.MaterialOf(phase==0?SimHashes.Oxygen:SimHashes.Water);var gap=phase==0?.01:.1;
                        var mass=new PipeAmount(phase==0?1:10).Minus(output.Total).Minus(new PipeAmount(gap));
                        var energy=mass.Times(material.HeatCapacity*300).Plus(mass.Times(material.EnergyOffset));
                        ColonyConduits.SupplyControlInventory(flow,ValveCell(valve,"outputCell"),PipeInventory.Conserved(new[]{new PipePart(material.Id,mass)},energy,Array.Empty<PipePart>()));
                        ColonyConduits.TryRead(flow,ValveCell(valve,"inputCell"),out var before);recorded[$"valve partial source {phase}"]=before.Mass;
                        var native=valve.GetComponent<ValveBase>();native.CurrentFlow=native.MaxFlow;
                        recorded[$"valve partial calls {phase}"]=ColonyValveControl.Calls(native);
                        valvePartialSetup[phase]=ColonyValveControl.State(native);
                    }
                    else
                    {
                        ColonyConduits.TransferSelected(flow,ValveCell(valve,"outputCell"),Cell(7,phase==0?1:4),-1,true);
                        ColonyConduits.TryRead(flow,ValveCell(valve,"inputCell"),out var retained);
                        if(retained.Count==0)SupplyValve(phase,valve);
                        else Record($"{phase} shutoff emptied its original input before refill",false,$"retained {retained.Mass:R} kg");
                    }
                }
            }
            if(ValveAt(38,"partial capacity and disabled shutoff then reopen"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;var manual=mixedValves[phase*2];var automatic=mixedValves[phase*2+1];
                    ColonyConduits.TryRead(flow,ValveCell(manual,"outputCell"),out var output);ColonyConduits.TryRead(flow,ValveCell(manual,"inputCell"),out var input);
                    var cap=phase==0?1:10;var taken=recorded[$"valve partial source {phase}"]-input.Mass;
                    Record($"{phase} manual valve respects exact partial output capacity",output.Total.Minus(new PipeAmount(cap)).Sign<=0&&Math.Abs(output.Mass-cap)<1e-7&&Math.Abs(taken-(phase==0?.01:.1))<1e-7,$"output {output.Mass:R}; taken {taken:R}; setup [{valvePartialSetup[phase]}]; callbacks added {ColonyValveControl.Calls(manual.GetComponent<ValveBase>())-recorded[$"valve partial calls {phase}"]}; {ColonyValveControl.Describe(manual.GetComponent<ValveBase>())}");
                    if(recorded.ContainsKey("valve positive phase ready"))
                        Record($"{phase} partial-capacity window received a real native callback",ColonyValveControl.Calls(manual.GetComponent<ValveBase>())>recorded[$"valve partial calls {phase}"],valvePositivePhaseProof);
                    if(phase==1&&recorded.ContainsKey("valve diagnostic negative phase"))
                        Record("diagnostic fixed window received no liquid callback",ColonyValveControl.Calls(manual.GetComponent<ValveBase>())==recorded[$"valve partial calls {phase}"],
                            valveNegativePhaseProof+"; actual partial setup ["+valvePartialSetup[phase]+"]");
                    manual.GetComponent<ValveBase>().CurrentFlow=0;
                    ColonyConduits.TryRead(flow,ValveCell(automatic,"inputCell"),out input);ColonyConduits.TryRead(flow,ValveCell(automatic,"outputCell"),out output);
                    Record($"{phase} disabled powered shutoff holds with real output room",input.Mass==cap&&output.Count==0,$"input {input.Mass:R}; output {output.Mass:R}; {MachineState(automatic)}");
                    automatic.GetComponent<Operational>().SetFlag(BuildingEnabledButton.EnabledFlag,true);
                }
            }
            if(ValveAt(44,"reopened shutoffs and exact valve accounts"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;var valve=mixedValves[phase*2+1];ColonyConduits.TryRead(flow,ValveCell(valve,"outputCell"),out var output);
                    Record($"{phase} powered shutoff reopens with complete mixture",Math.Abs(output.Mass-(phase==0?1:10))<1e-6&&output.Count==2,$"{output.Mass:R} kg; {output.Count} species");
                }
                Record("all valve inventories close every account",ColonyConduits.Audit(out var detail),detail);ColonyValveControl.Reset();Finish();
            }
        }
        private static void SupplyValve(int phase,GameObject valve)
        {
            var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
            var a=ColonyConduits.MaterialOf(phase==0?SimHashes.Oxygen:SimHashes.Water);var b=ColonyConduits.MaterialOf(phase==0?SimHashes.CarbonDioxide:SimHashes.DirtyWater);
            var inventory=PipeInventory.Single(a,phase==0?.375:3,300,0,.25).Joined(PipeInventory.Single(b,phase==0?.625:7,300,1,1e-240));
            ColonyConduits.SupplyControlInventory(flow,ValveCell(valve,"inputCell"),inventory);
        }
        private static readonly List<GameObject> mixedTeardownPipes=new List<GameObject>();
        private static PipeInventory mixedTeardownWorldBefore=PipeInventory.Empty,mixedTeardownHeldBefore=PipeInventory.Empty;
        private static PipeAmount mixedTeardownPassiveBefore1,mixedTeardownPassiveBefore2;
        private static PipeAmount ScenarioPart(ReadOnlySpan<PipePart> parts,int id)
        {foreach(var part in parts)if(part.Id==id)return part.Amount;return PipeAmount.Zero;}
        private static PipeAmount ScenarioSpeciesDelta(PipeInventory current,PipeInventory before,int id)
            =>ScenarioPart(current.Parts,id).Minus(ScenarioPart(before.Parts,id));
        private static int ScenarioChangedSpecies(PipeInventory current,PipeInventory before)
            =>current.Parts.ToArray().Select(p=>p.Id).Union(before.Parts.ToArray().Select(p=>p.Id))
                .Count(id=>ScenarioSpeciesDelta(current,before,id).Sign!=0);
        private static bool SameScenarioInventory(PipeInventory current,PipeInventory before)
        {
            if(current.Count!=before.Count||current.Germs.Length!=before.Germs.Length||current.Energy.Minus(before.Energy).Sign!=0)return false;
            for(var i=0;i<current.Parts.Length;++i)
                if(current.Parts[i].Id!=before.Parts[i].Id||current.Parts[i].Amount.Minus(before.Parts[i].Amount).Sign!=0)return false;
            for(var i=0;i<current.Germs.Length;++i)
                if(current.Germs[i].Id!=before.Germs[i].Id||current.Germs[i].Amount.Minus(before.Germs[i].Amount).Sign!=0)return false;
            return true;
        }
        private static void MixedPipeTeardown(int frame)
        {
            if(Once(0,"open and fully sealed spill sites"))Arena((x,y)=>x>=2&&x<=6&&y>=1?(SimHashes.Unobtanium,20000f,300f):(SimHashes.Oxygen,1f,300f));
            if(Once(5,"build open and sealed gas/liquid pipes"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitSystem:Game.Instance.liquidConduitSystem;var id=phase==0?GasConduitConfig.ID:LiquidConduitConfig.ID;
                    var open=phase==0?Cell(0,0):Cell(7,0);Pipe(flow,id,new List<int>{open},new List<int>{Cell(4,3)});
                    var layer=phase==0?ObjectLayer.GasConduit:ObjectLayer.LiquidConduit;
                    mixedTeardownPipes.Add(Grid.Objects[open,(int)layer]);mixedTeardownPipes.Add(Grid.Objects[Cell(4,3),(int)layer]);
                }
            }
            if(Once(10,"demolish complete mixed pipe cells"))
            {
                ColonyConduits.StartControl();ColonyPipePorts.StartControl();ColonyPipeTeardown.StartControl();
                // Earlier scenarios retain their token/account history. This
                // fixture measures its own fixed additions without resetting it.
                mixedTeardownWorldBefore=ColonyPipePorts.ToWorld;mixedTeardownHeldBefore=ColonyPipePorts.Held;
                mixedTeardownPassiveBefore1=ColonyPipePorts.PassiveExchange(1);mixedTeardownPassiveBefore2=ColonyPipePorts.PassiveExchange(2);
                for(var phase=0;phase<2;++phase)
                    for(var site=0;site<2;++site)
                    {
                        var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                        var cell=site==1?Cell(4,3):phase==0?Cell(0,0):Cell(7,0);
                        var a=ColonyConduits.MaterialOf(phase==0?SimHashes.Oxygen:SimHashes.Water);var b=ColonyConduits.MaterialOf(phase==0?SimHashes.CarbonDioxide:SimHashes.DirtyWater);
                        var inventory=PipeInventory.Single(a,phase==0?.375:3,300,0,.25).Joined(PipeInventory.Single(b,phase==0?.625:7,300,1,1e-240));
                        ColonyConduits.SupplyControlInventory(flow,cell,inventory);
                        mixedTeardownPipes[phase*2+site].GetComponent<Deconstructable>().ForceDestroyAndGetMaterials();
                    }
            }
            if(Once(18,"open spills land and sealed spills stay owned"))
            {
                var removed=!Game.Instance.gasConduitFlow.HasConduit(Cell(0,0))&&!Game.Instance.liquidConduitFlow.HasConduit(Cell(7,0))&&
                    !Game.Instance.gasConduitFlow.HasConduit(Cell(4,3))&&!Game.Instance.liquidConduitFlow.HasConduit(Cell(4,3));
                Record("actual deconstruction removes every native conduit",removed,$"removed {removed}");
                var delivered=ColonyPipePorts.ToWorld.Total.Minus(mixedTeardownWorldBefore.Total);
                var species=ScenarioChangedSpecies(ColonyPipePorts.ToWorld,mixedTeardownWorldBefore);
                var held=ColonyPipePorts.Held.Total.Minus(mixedTeardownHeldBefore.Total);
                Record("open mixed spills deliver every constituent",delivered.Minus(new PipeAmount(11)).Sign==0&&species==4,$"{delivered.Value:R} kg added, {species} species");
                Record("sealed mixed spills retain their complete inventory",held.Minus(new PipeAmount(11)).Sign==0&&ColonyPipeTeardown.ActiveSpills==2,$"{held.Value:R} kg added, {ColonyPipeTeardown.ActiveSpills} spills; {ColonyPipeTeardown.LastAdmissionError}");
                ColonyPipeTeardown.Tick(ColonyOwner.Status.CompletedFrames);var attempts=ColonyPipeTeardown.Attempts;
                var inspection=MixtureInspection.ReadCellSpill(Cell(4,3));var text=inspection==null?"":inspection.Title+"\n"+string.Join("\n",inspection.Details);
                for(var repeat=0;repeat<100;++repeat){_=MixtureInspection.ReadCellSpill(Cell(4,3));ColonyPipeTeardown.Tick(ColonyOwner.Status.CompletedFrames);}
                Record("retained spill inspection shows both phases and all constituents",text.Contains("Fluid waiting for space")&&new[]{SimHashes.Oxygen,SimHashes.CarbonDioxide,SimHashes.Water,SimHashes.DirtyWater}.All(id=>text.Contains(ElementLoader.FindElementByHash(id).name)),text);
                Record("inspection and repeated publications do not retry within a physics frame",ColonyPipeTeardown.Attempts==attempts,$"{attempts} -> {ColonyPipeTeardown.Attempts} attempts");
                Record("blocked teardown closes every account",ColonyConduits.Audit(out var detail),detail);
            }
            if(Once(20,"open the sealed spill cell"))SimMessages.ReplaceElement(Cell(4,3),SimHashes.Oxygen,CellEventLogger.Instance.SandBoxTool,1,300);
            if(Once(32,"retained spills release after space opens"))
            {
                var delivered=ColonyPipePorts.ToWorld.Total.Minus(mixedTeardownWorldBefore.Total);
                var heldUnchanged=SameScenarioInventory(ColonyPipePorts.Held,mixedTeardownHeldBefore);
                Record("every demolished mixture eventually reaches the world",delivered.Minus(new PipeAmount(22)).Sign==0&&heldUnchanged&&ColonyPipePorts.PendingCount==0&&ColonyPipeTeardown.ActiveSpills==0,
                    $"world +{delivered.Value:R} kg; pre-existing held inventory unchanged {heldUnchanged}; pending {ColonyPipePorts.PendingCount}; spills {ColonyPipeTeardown.ActiveSpills}");
                Record("released spill inspection disappears",MixtureInspection.ReadCellSpill(Cell(4,3))==null,"read-only cell spill reader");
                var germ1=ColonyPipePorts.PassiveExchange(1).Minus(mixedTeardownPassiveBefore1);var germ2=ColonyPipePorts.PassiveExchange(2).Minus(mixedTeardownPassiveBefore2);
                Record("all teardown germ identities remain delivered or booked",germ1.Negated().Value==1&&germ2.Sign<0,
                    $"delivered increments {germ1.Negated().Value:R}/{germ2.Negated().Value:R}; remainders {ColonyPipePorts.PassiveRemainder(1).Value:R}/{ColonyPipePorts.PassiveRemainder(2).Value:R}");
                Record("completed teardown closes every account",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
        private static readonly List<ulong[]> mixedOutputJobs=new List<ulong[]>();
        private static ulong mixedOutputFirstNative;
        private static PipeInventory mixedOutputWorldBefore=PipeInventory.Empty;
        private static PipeAmount mixedOutputPassiveBefore1,mixedOutputPassiveBefore2;
        private static void MixedPipeOutput(int frame)
        {
            if(Once(0,"atomic mixed output arena"))Arena((x,y)=>(SimHashes.Oxygen,1f,300f));
            if(Once(5,"isolated output source pipes"))
            {
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{Cell(1,3)});
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{Cell(6,3)});
            }
            if(Once(10,"reserve and dispatch complete owned mixtures"))
            {
                ColonyConduits.StartControl();ColonyPipePorts.StartControl();ColonyPipePorts.BeginCompletionControl();
                mixedOutputFirstNative=ColonyPipePorts.NextNativeToken;mixedOutputWorldBefore=ColonyPipePorts.ToWorld;
                mixedOutputPassiveBefore1=ColonyPipePorts.PassiveExchange(1);mixedOutputPassiveBefore2=ColonyPipePorts.PassiveExchange(2);
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;var source=phase==0?Cell(1,3):Cell(6,3);
                    var a=ColonyConduits.MaterialOf(phase==0?SimHashes.Oxygen:SimHashes.Water);
                    var b=ColonyConduits.MaterialOf(phase==0?SimHashes.CarbonDioxide:SimHashes.DirtyWater);
                    var inventory=PipeInventory.Single(a,phase==0?.375:3,300,0,.25).Joined(PipeInventory.Single(b,phase==0?.625:7,300,1,1e-240));
                    ColonyConduits.SupplyControlInventory(flow,source,inventory);var id=ColonyPipePorts.HoldForPort(flow,source,double.MaxValue);
                    var before=ColonyPipePorts.BufferInventory(id);var rejected=false;
                    try{ColonyPipePorts.DispatchBuffer(id,uint.MaxValue);}catch(InvalidOperationException){rejected=true;}
                    var next=ColonyPipePorts.NextNativeToken;OniNativeColony.Check(OniNativeColony.PipeJobState(next,out var nativeState,out var last));
                    Record((phase==0?"gas":"liquid")+" rejected atomic output preserves source and native token",rejected&&ReferenceEquals(before,ColonyPipePorts.BufferInventory(id))&&next==mixedOutputFirstNative+(ulong)(phase*2)&&nativeState==0&&last==0,
                        $"rejected {rejected}; same source {ReferenceEquals(before,ColonyPipePorts.BufferInventory(id))}; next token {next}; native state {nativeState}/{last}");
                    var jobs=ColonyPipePorts.DispatchBuffer(id,(uint)(phase==0?Cell(1,1):Cell(6,1)));mixedOutputJobs.Add(jobs);
                    Record((phase==0?"gas":"liquid")+" complete mixture queued as two durable constituent jobs",jobs.Length==2&&ColonyPipePorts.BufferInventory(id).Count==0,$"{jobs.Length} jobs; source remainder {ColonyPipePorts.BufferInventory(id).Mass:R} kg");
                }
            }
            if(Once(18,"verify atomic mixed output receipts"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var frames=new List<ulong>();var complete=true;
                    foreach(var id in mixedOutputJobs[phase]){if(!ColonyPipePorts.TryControlFrame(id,out var completed))complete=false;else frames.Add(completed);}
                    Record((phase==0?"gas":"liquid")+" output constituents commit in the same physical frame",complete&&frames.Count==2&&frames.Distinct().Count()==1,string.Join(",",frames));
                }
                var delivered=ColonyPipePorts.ToWorld;
                var okay=ScenarioSpeciesDelta(delivered,mixedOutputWorldBefore,ColonyConduits.MaterialOf(SimHashes.Oxygen).Id).Value==.375&&
                    ScenarioSpeciesDelta(delivered,mixedOutputWorldBefore,ColonyConduits.MaterialOf(SimHashes.CarbonDioxide).Id).Value==.625&&
                    ScenarioSpeciesDelta(delivered,mixedOutputWorldBefore,ColonyConduits.MaterialOf(SimHashes.Water).Id).Value==3&&
                    ScenarioSpeciesDelta(delivered,mixedOutputWorldBefore,ColonyConduits.MaterialOf(SimHashes.DirtyWater).Id).Value==7;
                Record("all mixed-output species reach the world",okay,$"{delivered.Total.Minus(mixedOutputWorldBefore.Total).Value:R} kg added; {ScenarioChangedSpecies(delivered,mixedOutputWorldBefore)} constituents");
                var germ1=ColonyPipePorts.PassiveExchange(1).Minus(mixedOutputPassiveBefore1);var germ2=ColonyPipePorts.PassiveExchange(2).Minus(mixedOutputPassiveBefore2);
                Record("mixed-output preserves simultaneous fractional and tiny germ delivery",germ1.Sign<0&&germ2.Sign<0,
                    $"world change increments {germ1.Negated().Value:R}/{germ2.Negated().Value:R}; remainders {ColonyPipePorts.PassiveRemainder(1).Value:R}/{ColonyPipePorts.PassiveRemainder(2).Value:R}");
                OniNativeColony.Check(OniNativeColony.Wait());
                var state=ColonyPipePorts.ValidateSealedBoundary();Record("atomic output job suffix retires without token gaps",ColonyPipePorts.PendingCount==0&&state.NextNativeToken==mixedOutputFirstNative+4,$"pending {ColonyPipePorts.PendingCount}; next {state.NextNativeToken}; started {mixedOutputFirstNative}");
                Record("atomic mixed output closes every pipe and world boundary account",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
        private static PrimaryElement? mixedThermalBody;
        private static int ThermalCell=>Cell(3,3);
        private static bool ThermalAt(int at,string label)=>Once(at+(recorded.TryGetValue("thermal setup delay",out var delay)?(int)delay:0),label);
        private static void MixedPipeThermal(int frame)
        {
            if(Once(0,"isolated finite-body thermal arena"))Arena((x,y)=>(SimHashes.Vacuum,0f,300f));
            if(Once(5,"build finite radiant pipe body"))
            {
                Pipe(Game.Instance.gasConduitSystem,"GasConduitRadiant",new List<int>{ThermalCell});
                mixedThermalBody=Grid.Objects[ThermalCell,(int)ObjectLayer.GasConduit].GetComponent<PrimaryElement>();
            }
            if(frame>=8&&step<=8)
            {
                if(!Game.Instance.gasConduitFlow.HasConduit(ThermalCell))
                {
                    Current.Stage="waiting for native thermal pipe topology";
                    if(frame>=30){Record("native thermal topology became ready",false,"readiness expired");Finish();}return;
                }
                recorded["thermal setup delay"]=frame-8;
            }
            if(ThermalAt(8,"heat the finite native construction body"))mixedThermalBody!.Temperature=400;
            if(ThermalAt(14,"seed equal-mass gases with different Cv"))
            {
                ColonyConduits.StartControl();
                var a=ColonyConduits.MaterialOf(SimHashes.Oxygen);var b=ColonyConduits.MaterialOf(SimHashes.Hydrogen);
                var contents=PipeInventory.Single(a,.5,300,255,0).Joined(PipeInventory.Single(b,.5,300,255,0));
                ColonyConduits.SupplyControlInventory(Game.Instance.gasConduitFlow,ThermalCell,contents);
                if(Environment.GetEnvironmentVariable("MA_COLONY_PIPE_CONTACT_HEAT")=="1")ColonyPipeContactHeat.StartControl();
                var body=mixedThermalBody!;var thermalMass=body.GetComponent<Building>().Def.MassForTemperatureModification;
                var bodyCv=(double)thermalMass*body.Element.specificHeatCapacity*1000;var fluidCv=.5*(a.HeatCapacity+b.HeatCapacity);
                recorded["thermal body Cv"]=bodyCv;recorded["thermal fluid Cv"]=fluidCv;recorded["thermal body T"]=body.Temperature;
                recorded["thermal initial U"]=contents.Energy.Value;recorded["thermal initial exchange"]=ColonyConduits.ExchangedHeatJoules;
                recorded["thermal equilibrium"]=(bodyCv*body.Temperature+fluidCv*300)/(bodyCv+fluidCv);
                Record("thermal body is finite and both species Cv matter",bodyCv>0&&fluidCv>0&&Math.Max(a.HeatCapacity,b.HeatCapacity)>2*Math.Min(a.HeatCapacity,b.HeatCapacity)&&body.Temperature>399,
                    $"owner flags {ColonyOwner.Config.Flags}; body construction {body.Mass:R} kg, registered thermal mass {thermalMass:R} kg, Cv {bodyCv:R} J/K, T {body.Temperature:R} K; species Cv {a.HeatCapacity:R}/{b.HeatCapacity:R}; equilibrium {recorded["thermal equilibrium"]:R} K");
            }
            if(recorded.TryGetValue("thermal initial U",out var initialU)&&frame<600+(int)recorded["thermal setup delay"])
            {
                ColonyConduits.TryRead(Game.Instance.gasConduitFlow,ThermalCell,out var observation);
                var bodyT=(double)mixedThermalBody!.Temperature;
                var bodyHeat=recorded["thermal body Cv"]*(bodyT-recorded["thermal body T"]);
                Current.Stage=$"finite-body observation: body {bodyT:R} K; mixture {observation.Temperature(ColonyConduits.Material):R} K; truth {recorded["thermal equilibrium"]:R} K; body heat {bodyHeat:R} J; fluid heat {observation.Energy.Value-initialU:R} J";
            }
            if(ThermalAt(600,"finite-body heat and analytic equilibrium"))
            {
                ColonyConduits.TryRead(Game.Instance.gasConduitFlow,ThermalCell,out var contents);
                var bodyT=(double)mixedThermalBody!.Temperature;var fluidT=contents.Temperature(ColonyConduits.Material);var equilibrium=recorded["thermal equilibrium"];
                var bodyHeat=recorded["thermal body Cv"]*(bodyT-recorded["thermal body T"]);var fluidHeat=contents.Energy.Value-recorded["thermal initial U"];
                var exchange=ColonyConduits.ExchangedHeatJoules-recorded["thermal initial exchange"];
                // Native construction temperatures are binary32 readouts. The
                // heat check permits two endpoint ulps, not a mixed-Cv rescale.
                double Ulp(float value)=>Math.Abs((double)BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(value)+1)-value);
                var bound=2*recorded["thermal body Cv"]*(Ulp((float)bodyT)+Ulp((float)recorded["thermal body T"]))+1e-7*Math.Max(1,Math.Abs(fluidHeat));
                Record("finite body pays the actual fluid heat",Math.Abs(bodyHeat+fluidHeat)<=bound,
                    $"body {bodyHeat:R} J; fluid {fluidHeat:R} J; booked {exchange:R} J; residual {bodyHeat+fluidHeat:R} J; endpoint bound {bound:R} J");
                Record("finite body and mixed gas reach analytic equilibrium",Math.Abs(bodyT-equilibrium)<=.1&&Math.Abs(fluidT-equilibrium)<=.1,
                    $"body {bodyT:R} K; mixture {fluidT:R} K; truth {equilibrium:R} K");
                Record("finite-body pipe still retains both original masses",contents.Count==2&&contents.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.Oxygen).Id)==.5&&contents.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.Hydrogen).Id)==.5,$"{contents.Mass:R} kg, {contents.Count} constituents");
                Record("finite-body pipe accounts close",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
        private static readonly List<GameObject> mixedPumps=new List<GameObject>();
        private static readonly List<ulong> mixedPumpHeldOutputs=new List<ulong>();
        private static readonly List<PipeInventory> mixedPumpDischargeSources=new List<PipeInventory>();
        private static int PumpOutlet(GameObject pump)=>(int)AccessTools.Field(typeof(ConduitDispenser),"utilityCell").GetValue(pump.GetComponent<ConduitDispenser>());
        private static bool PumpAt(int at,string label)=>Once(at+(recorded.TryGetValue("pump setup delay",out var delay)?(int)delay:0),label);
        private static void MixedPipePumps(int frame)
        {
            if(Once(0,"mixed pump chambers"))Arena((x,y)=>x==4?(SimHashes.Unobtanium,20000f,300f):x<4?
                ((x+y)%2==0?SimHashes.Oxygen:SimHashes.CarbonDioxide,1.2f,300f):
                (y<=2?((x+y)%2==0?SimHashes.Water:SimHashes.DirtyWater,500f,300f):(SimHashes.Oxygen,1.2f,300f)));
            if(Once(5,"build inactive pumps"))
            {
                mixedPumps.Add(Build(GasPumpConfig.ID,Cell(1,0)));mixedPumps.Add(Build(LiquidPumpConfig.ID,Cell(5,0)));
                foreach(var pump in mixedPumps)pump.GetComponent<ElementConsumer>().EnableConsumption(false);
                SimMessages.AddRemoveSubstance(Cell(1,1),SimHashes.Oxygen,CellEventLogger.Instance.OxygenModifierSimUpdate,.01f,300,0,1000);
                SimMessages.AddRemoveSubstance(Cell(1,1),SimHashes.CarbonDioxide,CellEventLogger.Instance.OxygenModifierSimUpdate,.01f,300,1,2000);
                SimMessages.AddRemoveSubstance(Cell(5,1),SimHashes.Water,CellEventLogger.Instance.OxygenModifierSimUpdate,.01f,300,0,1000);
                SimMessages.AddRemoveSubstance(Cell(5,1),SimHashes.DirtyWater,CellEventLogger.Instance.OxygenModifierSimUpdate,.01f,300,1,2000);
            }
            if(Once(7,"isolated pump outputs"))
            {
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{PumpOutlet(mixedPumps[0])},new List<int>{Cell(0,4)},new List<int>{Cell(3,5)});
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{PumpOutlet(mixedPumps[1])},new List<int>{Cell(7,4)},new List<int>{Cell(3,5)});
            }
            if(frame>=8&&step<=8)
            {
                var ready=mixedPumps.Count==2;
                for(var phase=0;ready&&phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    ready=flow.HasConduit(PumpOutlet(mixedPumps[phase]))&&flow.HasConduit(Cell(3,5))&&flow.HasConduit(phase==0?Cell(0,4):Cell(7,4));
                }
                if(!ready)
                {
                    Current.Stage="waiting for native pump pipe topology";
                    if(frame>=30){Record("native pump pipe topology became ready",false,"readiness barrier expired before intake activation");Finish();}
                    return;
                }
                recorded["pump setup delay"]=frame-8;
            }
            if(PumpAt(8,"own fresh pump storage and catalogue routes"))
            {
                ColonyConduits.StartControl();ColonyPipePorts.StartControl();ColonyPipePumps.StartControl();
                CataloguePipeRoutes();
                foreach(var pump in mixedPumps)pump.GetComponent<ElementConsumer>().EnableConsumption(true);
            }
            if(PumpAt(18,"actual powered mixed pump outputs"))
            {
                Record("both real pump components own their intake",ColonyPipePumps.BoundPumps==2,$"{ColonyPipePumps.BoundPumps} pumps; {ColonyPipePumps.Queued} requests; {ColonyPipePumps.Notifications} notifications");
                for(var phase=0;phase<2;++phase)
                {
                    var pump=mixedPumps[phase];var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    ColonyConduits.TryRead(flow,PumpOutlet(pump),out var output);var held=ColonyPipePumps.Inventory(pump.GetComponent<Storage>());var all=output.Joined(held);
                    Record((phase==0?"gas":"liquid")+" pump emits a real one-cell mixture",output.Count>=2,$"{output.Count} pipe constituents; {output.Mass:R} kg; {MachineState(pump)}");
                    Record((phase==0?"gas":"liquid")+" pump retains simultaneous germ identities",all.Germs.Length>=2,$"{all.Germs.Length} germ identities");
                    Record((phase==0?"gas":"liquid")+" pump has one authoritative inventory",pump.GetComponent<Storage>().items.Count==0&&ColonyPipePumps.Owns(pump.GetComponent<Storage>()),$"native packets {pump.GetComponent<Storage>().items.Count}; owned buffer {held.Mass:R} kg");
                    Record((phase==0?"gas":"liquid")+" native pump accumulator receives consumed mass",pump.GetComponent<ElementConsumer>().AverageConsumeRate>0,$"{pump.GetComponent<ElementConsumer>().AverageConsumeRate:R} kg/s");
                }
            }
            if(PumpAt(20,"hold output while native storage fills"))
            {
                // RequireOutputs intentionally wants an empty outlet. Stop the
                // native dispenser while keeping that outlet empty to measure
                // the real storage limit without replacing its operational gate.
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    mixedPumps[phase].GetComponent<ConduitDispenser>().SetOnState(false);
                    mixedPumpHeldOutputs.Add(ColonyPipePorts.HoldForPort(flow,PumpOutlet(mixedPumps[phase]),double.MaxValue));
                }
            }
            if(PumpAt(42,"restore controlled full outputs"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;var cell=PumpOutlet(mixedPumps[phase]);
                    ColonyPipePorts.PlaceBuffer(mixedPumpHeldOutputs[phase]);ColonyConduits.TryRead(flow,cell,out var output);
                    var room=new PipeAmount(phase==0?1:10).Minus(output.Total);
                    if(room.Sign>0)
                    {
                        var material=ColonyConduits.MaterialOf(phase==0?SimHashes.Oxygen:SimHashes.Water);
                        var energy=room.Times(material.HeatCapacity*300).Plus(room.Times(material.EnergyOffset));
                        ColonyConduits.SupplyControlInventory(flow,cell,PipeInventory.Conserved(new[]{new PipePart(material.Id,room)},energy,Array.Empty<PipePart>()));
                    }
                    mixedPumps[phase].GetComponent<ConduitDispenser>().SetOnState(true);
                }
            }
            if(PumpAt(45,"blocked output preserves native storage capacity"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var pump=mixedPumps[phase];var storage=pump.GetComponent<Storage>();var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    ColonyConduits.TryRead(flow,PumpOutlet(pump),out var output);var held=ColonyPipePumps.Inventory(storage);
                    Record((phase==0?"gas":"liquid")+" pump stops at blocked capacity",Math.Abs(output.Mass-(phase==0?1:10))<1e-5&&Math.Abs(held.Mass-storage.capacityKg)<1e-5&&pump.GetComponent<ConduitDispenser>().blocked,
                        $"pipe {output.Mass:R}; exact room {new PipeAmount(phase==0?1:10).Minus(output.Total).Value:R}; buffer {held.Mass:R}/{storage.capacityKg:R}; blocked {pump.GetComponent<ConduitDispenser>().blocked}; isOn {pump.GetComponent<ConduitDispenser>().isOn}; always {pump.GetComponent<ConduitDispenser>().alwaysDispense}; {MachineState(pump)}");
                }
                Record("native pump stored notifications and accumulators still run",ColonyPipePumps.Notifications>0&&mixedPumps.All(p=>p.GetComponent<ElementConsumer>().AverageConsumeRate>=0),$"notifications {ColonyPipePumps.Notifications}");
                var text=MixtureInspection.GetContainerInspectionText(mixedPumps[0]);
                Record("pump inspection exposes actual held mixture",text.Contains("Pump contents")&&!text.Contains("Native inventory"),text);
            }
            if(PumpAt(46,"release partial output room"))
            {
                recorded["pump requests before room"]=ColonyPipePumps.Queued;
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    mixedPumpDischargeSources.Add(ColonyPipePumps.Inventory(mixedPumps[phase].GetComponent<Storage>()));
                    ColonyConduits.TransferSelected(flow,PumpOutlet(mixedPumps[phase]),phase==0?Cell(0,4):Cell(7,4),-1,true,phase==0?.25:3);
                }
            }
            if(PumpAt(52,"partial output refills and disable pumps"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;ColonyConduits.TryRead(flow,PumpOutlet(mixedPumps[phase]),out var output);
                    Record((phase==0?"gas":"liquid")+" pump fills partial room without overflow",Math.Abs(output.Mass-(phase==0?1:10))<1e-5&&output.Total.Minus(new PipeAmount(phase==0?1:10)).Sign<=0,$"{output.Mass:R} kg; {output.Count} constituents");
                    var before=mixedPumpDischargeSources[phase];var after=ColonyPipePumps.Inventory(mixedPumps[phase].GetComponent<Storage>());
                    var fraction=after.Mass/before.Mass;var largest=0.0;var okay=before.Count>=2&&after.Count==before.Count&&fraction>0&&fraction<1;
                    foreach(var part in before.Parts){var error=Math.Abs(after.SpeciesMass(part.Id)-part.Mass*fraction);largest=Math.Max(largest,error);okay&=error<=1e-11*Math.Max(1,part.Mass);}
                    foreach(var germ in before.Germs)
                    {
                        var amount=0.0;foreach(var remaining in after.Germs)if(remaining.Id==germ.Id)amount=remaining.Mass;
                        okay&=Math.Abs(amount-germ.Mass*fraction)<=1e-11*Math.Max(1,germ.Mass);
                    }
                    okay&=Math.Abs(after.Energy.Value-before.Energy.Value*fraction)<=1e-10*Math.Max(1,Math.Abs(before.Energy.Value));
                    Record((phase==0?"gas":"liquid")+" partial pump discharge samples the complete stored mixture",okay,$"held {before.Mass:R} -> {after.Mass:R} kg; remaining fraction {fraction:R}; largest species residual {largest:R} kg");
                    mixedPumps[phase].GetComponent<ConduitDispenser>().SetOnState(false);
                    ColonyPipePorts.HoldForPort(flow,PumpOutlet(mixedPumps[phase]),double.MaxValue);
                }
                recorded["requests before empty outlet"]=ColonyPipePumps.Queued;
            }
            if(PumpAt(64,"native empty outlet resumes intake then disable"))
            {
                Record("pumps resume intake after output room opens",ColonyPipePumps.Queued>recorded["requests before empty outlet"],$"{recorded["requests before empty outlet"]:R} -> {ColonyPipePumps.Queued}");
                for(var phase=0;phase<2;++phase)
                {
                    var pump=mixedPumps[phase];var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    // Make real storage room and leave the outlet empty. The
                    // disabled intake check then cannot pass because of either
                    // full storage or a blocked native outlet.
                    pump.GetComponent<ConduitDispenser>().SetOnState(true);
                    ColonyPipePumps.Dispense(pump.GetComponent<ConduitDispenser>());
                    pump.GetComponent<ConduitDispenser>().SetOnState(false);
                    ColonyPipePorts.HoldForPort(flow,PumpOutlet(pump),double.MaxValue);
                    pump.GetComponent<Operational>().SetFlag(BuildingEnabledButton.EnabledFlag,false);
                    var storage=pump.GetComponent<Storage>();ColonyConduits.TryRead(flow,PumpOutlet(pump),out var empty);
                    Record((phase==0?"gas":"liquid")+" disabled pump still has real intake room",ColonyPipePumps.Inventory(storage).Mass<storage.capacityKg&&empty.Count==0,$"{ColonyPipePumps.Inventory(storage).Mass:R}/{storage.capacityKg:R} kg; outlet {empty.Mass:R} kg");
                }
            }
            if(PumpAt(66,"record disabled requests"))recorded["pump requests"]=ColonyPipePumps.Queued;
            if(PumpAt(70,"disabled pumps and exact final accounts"))
            {
                Record("disabled pumps queue no further world consumption",ColonyPipePumps.Queued==recorded["pump requests"],$"{recorded["pump requests"]:R} -> {ColonyPipePumps.Queued}");
                Record("pump pipe and world accounts close",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
        private static readonly List<ulong> mixedBoundaryJobs=new List<ulong>();
        private static void MixedPipeBoundary(int frame)
        {
            if(Once(0,"arena"))Arena((x,y)=>(SimHashes.Oxygen,1f,300f));
            if(Once(5,"isolated boundary pipes"))
            {
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{Cell(1,3)},new List<int>{Cell(1,5)});
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{Cell(6,3)},new List<int>{Cell(6,5)});
            }
            if(Once(8,"precise pipe injections"))
            {
                ColonyConduits.StartControl();ColonyPipePorts.StartControl();
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    var first=ColonyConduits.MaterialOf(phase==0?SimHashes.Oxygen:SimHashes.Water);
                    var second=ColonyConduits.MaterialOf(phase==0?SimHashes.CarbonDioxide:SimHashes.DirtyWater);
                    var source=phase==0?Cell(1,3):Cell(6,3);var world=(uint)(phase==0?Cell(2,1):Cell(5,1));
                    var massA=phase==0?.25:3;var massB=phase==0?.75:7;var temperature=phase==0?311.123456789:300.123456789;
                    var mixture=PipeInventory.Single(first,massA,temperature,0,.25).Joined(PipeInventory.Single(second,massB,temperature,1,1e-240));
                    ColonyConduits.SupplyControlInventory(flow,source,mixture);
                    mixedBoundaryJobs.Add(ColonyPipePorts.Inject(flow,source,world,first.Id,massA));
                    mixedBoundaryJobs.Add(ColonyPipePorts.Inject(flow,source,world,second.Id,massB));
                }
                Record("queued reservations remain fully accounted",ColonyConduits.Audit(out var detail),detail);
            }
            if(Once(14,"committed injection parcels"))
            {
                Record("all four precise injection jobs retired",ColonyPipePorts.PendingCount==0,ColonyPipePorts.Report);
                Record("injections retain exact mass U and passive remainders",ColonyConduits.Audit(out var detail),detail);
                mixedBoundaryJobs.Add(ColonyPipePorts.Extract(Cell(1,5),1,(uint)Cell(2,1),1,2));
                mixedBoundaryJobs.Add(ColonyPipePorts.Extract(Cell(6,5),2,(uint)Cell(5,1),10,2));
            }
            if(Once(22,"actual mixed extraction groups"))
            {
                Record("all phase extraction outcomes retired",ColonyPipePorts.PendingCount==0,ColonyPipePorts.Report);
                for(var phase=0;phase<2;++phase)
                {
                    var id=mixedBoundaryJobs[4+phase];var buffer=ColonyPipePorts.BufferInventory(id);
                    Record((phase==0?"gas":"liquid")+" extraction keeps every committed constituent",buffer.Count>=2&&buffer.Mass>0,$"{buffer.Count} constituents, {buffer.Mass:R} kg, {buffer.Germs.Length} germ identities");
                    ColonyPipePorts.PlaceBuffer(id);
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    ColonyConduits.TryRead(flow,phase==0?Cell(1,5):Cell(6,5),out var received);
                    Record((phase==0?"gas":"liquid")+" extraction enters one actual mixed pipe cell",received.Count>=2&&received.Mass>0,$"{received.Count} constituents, {received.Mass:R} kg; retained port buffer {ColonyPipePorts.BufferInventory(id).Mass:R} kg");
                }
                Record("world pipe boundary accounts all close",ColonyConduits.Audit(out var detail),detail);
                // The sealed border has no extractable gas. Its zero outcome
                // must still retire a token and preserve the queue watermark.
                mixedBoundaryJobs.Add(ColonyPipePorts.Extract(Cell(1,5),1,(uint)Cell(-1,0),.001,0));
            }
            if(Once(28,"zero outcome and sealed token boundary"))
            {
                Record("zero extraction also retires its native token",ColonyPipePorts.PendingCount==0&&ColonyPipePorts.BufferInventory(mixedBoundaryJobs[6]).Count==0,ColonyPipePorts.Report);
                OniNativeColony.Check(OniNativeColony.Wait());var boundary=ColonyPipePorts.ValidateSealedBoundary();
                Record("managed native pending suffix agrees",boundary.Jobs.Length==0&&boundary.NextNativeToken==8,$"next token {boundary.NextNativeToken}; pending {boundary.Jobs.Length}");
                Record("final precise port accounts close",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
        private static readonly List<int> pipeCells = new List<int>();
        private static readonly List<GameObject> mixedFilters=new List<GameObject>(),mixedSensors=new List<GameObject>();
        private static GameObject? mixedDiseaseSensor;
        private static int MixedPort(GameObject building,string name)=>(int)AccessTools.Field(typeof(ElementFilter),name).GetValue(building.GetComponent<ElementFilter>());
        private static void MixedPipeFilter(int frame)
        {
            if(Once(0,"arena"))Arena((x,y)=>(SimHashes.Oxygen,1f,300f));
            if(Once(5,"build filters and sensors"))
            {
                mixedFilters.Add(Build(GasFilterConfig.ID,Cell(5,1)));mixedFilters.Add(Build(LiquidFilterConfig.ID,Cell(5,3)));
                for(var phase=0;phase<2;++phase)
                    for(var sample=0;sample<3;++sample)mixedSensors.Add(Build(phase==0?"GasConduitElementSensor":"LiquidConduitElementSensor",Cell(1+sample,4+phase)));
                mixedDiseaseSensor=Build("GasConduitDiseaseSensor",Cell(0,4));
            }
            if(Once(7,"connect isolated filter ports"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    var network=phase==0?Game.Instance.gasConduitSystem:Game.Instance.liquidConduitSystem;
                    var id=phase==0?GasConduitConfig.ID:LiquidConduitConfig.ID;
                    var paths=new List<List<int>>();
                    foreach(var name in new[]{"inputCell","outputCell","filteredCell"})
                    {var cell=MixedPort(mixedFilters[phase],name);if(!InArena(cell))throw new InvalidOperationException("Mixed filter port leaves its arena.");paths.Add(new List<int>{cell});}
                    for(var sample=0;sample<3;++sample)paths.Add(new List<int>{Cell(1+sample,4+phase)});
                    if(phase==0)paths.Add(new List<int>{Cell(0,4)});
                    Pipe(network,id,paths.ToArray());
                }
            }
            if(Once(8,"seed actual mixed filter inputs"))
            {
                ColonyConduits.StartControl();
                for(var phase=0;phase<2;++phase)
                {
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    var a=phase==0?SimHashes.Oxygen:SimHashes.Water;var b=phase==0?SimHashes.CarbonDioxide:SimHashes.DirtyWater;
                    mixedFilters[phase].GetComponent<Filterable>().SelectedTag=ElementLoader.FindElementByHash(b).tag;
                    ColonyConduits.Seed(flow,MixedPort(mixedFilters[phase],"inputCell"),a,phase==0?.375f:3,300,0,4000);
                    ColonyConduits.Seed(flow,MixedPort(mixedFilters[phase],"inputCell"),b,phase==0?.625f:7,300,1,6000);
                    ColonyConduits.TryRead(flow,MixedPort(mixedFilters[phase],"inputCell"),out var mixture);
                    Record((phase==0?"gas":"liquid")+" filter receives two constituents in one cell",mixture.Count==2,$"{mixture.Count} constituents, {mixture.Mass:R} kg");
                    for(var sample=0;sample<3;++sample)
                    {
                        mixedSensors[phase*3+sample].GetComponent<Filterable>().SelectedTag=ElementLoader.FindElementByHash(a).tag;
                        var first=sample==0?(phase==0?.375f:3):sample==1?(phase==0?.0005f:.005f):(phase==0?1f/1024:10f/1024);
                        var second=sample==0?(phase==0?.625f:7):sample==1?(phase==0?.9995f:9.995f):(phase==0?999f/1024:9990f/1024);
                        ColonyConduits.Seed(flow,Cell(1+sample,4+phase),a,first,300,255,0);ColonyConduits.Seed(flow,Cell(1+sample,4+phase),b,second,300,255,0);
                    }
                }
                ColonyConduits.Seed(Game.Instance.gasConduitFlow,Cell(0,4),SimHashes.Oxygen,.375f,300,0,400000);
                ColonyConduits.Seed(Game.Instance.gasConduitFlow,Cell(0,4),SimHashes.CarbonDioxide,.625f,300,1,600000);
            }
            if(Once(16,"check selective filter and composition sensors"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    CheckMixedFilter(phase,phase==0?.375:3,phase==0?.625:7,0,0,"unblocked");
                    var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                    ColonyConduits.Seed(flow,MixedPort(mixedFilters[phase],"inputCell"),phase==0?SimHashes.Oxygen:SimHashes.Water,phase==0?.375f:3,300,0,4000);
                    ColonyConduits.Seed(flow,MixedPort(mixedFilters[phase],"inputCell"),phase==0?SimHashes.CarbonDioxide:SimHashes.DirtyWater,phase==0?.625f:7,300,1,6000);
                    for(var sample=0;sample<3;++sample)
                    {
                        var on=mixedSensors[phase*3+sample].GetComponent<ConduitElementSensor>().IsSwitchedOn;
                        Record((phase==0?"gas":"liquid")+" element sensor "+new[]{"finds a minority","excludes a trace","includes the exact threshold"}[sample],on==(sample!=1),$"on {on}; sample {sample}");
                    }
                }
                var values=new object[]{0,0,false};AccessTools.Method(typeof(ConduitDiseaseSensor),"GetContentsDisease").Invoke(mixedDiseaseSensor!.GetComponent<ConduitDiseaseSensor>(),values);
                ColonyConduits.TryRead(Game.Instance.gasConduitFlow,Cell(0,4),out var germs);
                var expected=(int)Math.Floor(germs.Germs.ToArray().Sum(g=>g.Mass));var most=germs.Germs.ToArray().Max(g=>g.Mass);
                Record("pipe germ sensor counts every identity",(bool)values[2]&&(int)values[1]==expected&&expected>most,$"sensor {values[1]}, total {expected}, largest identity {most:R}");
                var pipeObject=Grid.Objects[Cell(1,4),(int)ObjectLayer.GasConduit];
                var inspection=MixtureInspection.GetContainerInspectionText(pipeObject);
                var oxygenName=ElementLoader.FindElementByHash(SimHashes.Oxygen).name;var co2Name=ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).name;
                Record("pipe inspection shows both actual constituents",inspection.Contains(oxygenName)&&inspection.Contains(co2Name)&&!inspection.Contains("Native inventory"),inspection);
            }
            if(Once(24,"check capacity remainder"))
            {
                for(var phase=0;phase<2;++phase)
                {
                    CheckMixedFilter(phase,phase==0?.75:6,phase==0?1:10,0,phase==0?.25:4,"filtered capacity");
                    ColonyConduits.Seed(phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow,MixedPort(mixedFilters[phase],"inputCell"),
                        phase==0?SimHashes.Oxygen:SimHashes.Water,phase==0?.375f:5,300,0,4000);
                }
            }
            if(Once(32,"check blocked selected output still passes remainder"))
            {
                CheckMixedFilter(0,1,1,.125,.25,"blocked selected output");CheckMixedFilter(1,10,10,1,4,"blocked selected output");
                Record("filter and sensor inventories conserve every account",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
        private static void CheckMixedFilter(int phase,double passed,double filtered,double keptA,double keptB,string stage)
        {
            var flow=phase==0?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;var a=ColonyConduits.MaterialOf(phase==0?SimHashes.Oxygen:SimHashes.Water).Id;
            var b=ColonyConduits.MaterialOf(phase==0?SimHashes.CarbonDioxide:SimHashes.DirtyWater).Id;
            ColonyConduits.TryRead(flow,MixedPort(mixedFilters[phase],"inputCell"),out var source);ColonyConduits.TryRead(flow,MixedPort(mixedFilters[phase],"outputCell"),out var rest);
            ColonyConduits.TryRead(flow,MixedPort(mixedFilters[phase],"filteredCell"),out var chosen);
            var okay=Math.Abs(rest.SpeciesMass(a)-passed)<1e-9&&rest.SpeciesMass(b)==0&&Math.Abs(chosen.SpeciesMass(b)-filtered)<1e-9&&chosen.SpeciesMass(a)==0&&
                Math.Abs(source.SpeciesMass(a)-keptA)<1e-9&&Math.Abs(source.SpeciesMass(b)-keptB)<1e-9;
            Record((phase==0?"gas":"liquid")+" filter "+stage,okay,$"main {rest.SpeciesMass(a):R}/{rest.SpeciesMass(b):R}; selected {chosen.SpeciesMass(a):R}/{chosen.SpeciesMass(b):R}; held {source.SpeciesMass(a):R}/{source.SpeciesMass(b):R}; {MachineState(mixedFilters[phase])}");
        }
        private static void MixedPipes(int frame)
        {
            if(Once(0,"arena"))Arena((x,y)=>(SimHashes.Oxygen,1f,300f));
            if(Once(5,"build"))
            {
                Build("GasConduitBridge",Cell(2,1));Build("LiquidConduitBridge",Cell(2,4));
                Build("GasConduitBridge",Cell(6,1));Build("GasConduitBridge",Cell(6,2));
                Build("LiquidConduitBridge",Cell(6,4));Build("LiquidConduitBridge",Cell(6,3));
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{Cell(1,1),Cell(1,2)},
                    new List<int>{Cell(3,1),Cell(4,1),Cell(5,1)},new List<int>{Cell(4,1),Cell(4,2),Cell(5,2)},
                    new List<int>{Cell(7,1)},new List<int>{Cell(7,2)});
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{Cell(1,4),Cell(1,3)},
                    new List<int>{Cell(3,4),Cell(4,4),Cell(5,4)},new List<int>{Cell(4,4),Cell(4,3),Cell(5,3)},
                    new List<int>{Cell(7,4)},new List<int>{Cell(7,3)});
            }
            if(Once(8,"fill mixed cells"))
            {
                ColonyConduits.StartControl();
                var gas=Game.Instance.gasConduitFlow;var liquid=Game.Instance.liquidConduitFlow;
                var a=ColonyConduits.Seed(gas,Cell(1,1),SimHashes.Oxygen,.375f,280,0,375000);
                var b=ColonyConduits.Seed(gas,Cell(1,1),SimHashes.CarbonDioxide,.625f,320,1,625000);
                var c=ColonyConduits.Seed(liquid,Cell(1,4),SimHashes.Water,3,285,0,300000);
                var d=ColonyConduits.Seed(liquid,Cell(1,4),SimHashes.DirtyWater,7,315,1,700000);
                ColonyConduits.TryRead(gas,Cell(1,1),out var gasMix);ColonyConduits.TryRead(liquid,Cell(1,4),out var liquidMix);
                Record("one gas pipe cell owns both constituents",a==.375f&&b==.625f&&gasMix.Count==2,$"{a:R}/{b:R} kg; {gasMix.Count} constituents");
                Record("one liquid pipe cell owns both constituents",c==3&&d==7&&liquidMix.Count==2,$"{c:R}/{d:R} kg; {liquidMix.Count} constituents");
                var oxygen=.375*ColonyConduits.MaterialOf(SimHashes.Oxygen).HeatCapacity;var co2=.625*ColonyConduits.MaterialOf(SimHashes.CarbonDioxide).HeatCapacity;
                var water=3*ColonyConduits.MaterialOf(SimHashes.Water).HeatCapacity;var dirty=7*ColonyConduits.MaterialOf(SimHashes.DirtyWater).HeatCapacity;
                var gasExpected=(oxygen*280+co2*320)/(oxygen+co2);var liquidExpected=(water*285+dirty*315)/(water+dirty);
                Record("mixture equilibrium temperature uses every constituent heat capacity",Math.Abs(gasMix.Temperature(ColonyConduits.Material)-gasExpected)<1e-9&&
                    Math.Abs(liquidMix.Temperature(ColonyConduits.Material)-liquidExpected)<1e-9,$"gas {gasMix.Temperature(ColonyConduits.Material):R}/{gasExpected:R} K; liquid {liquidMix.Temperature(ColonyConduits.Material):R}/{liquidExpected:R} K");
            }
            foreach(var fillFrame in new[]{10,12,14,16})
                if(Once(fillFrame,"additional mixed packet "+fillFrame))
                {
                    ColonyConduits.Seed(Game.Instance.gasConduitFlow,Cell(1,1),SimHashes.Oxygen,.375f,280,0,375000);
                    ColonyConduits.Seed(Game.Instance.gasConduitFlow,Cell(1,1),SimHashes.CarbonDioxide,.625f,320,1,625000);
                    ColonyConduits.Seed(Game.Instance.liquidConduitFlow,Cell(1,4),SimHashes.Water,3,285,0,300000);
                    ColonyConduits.Seed(Game.Instance.liquidConduitFlow,Cell(1,4),SimHashes.DirtyWater,7,315,1,700000);
                }
            if(Once(25,"check internal flow"))
            {
                foreach(var phase in new[]{1,2})
                {
                    var flow=phase==1?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;var y=phase==1?1:4;
                    var mixed=0;var reached=0.0;
                    foreach(var cell in new[]{Cell(3,y),Cell(4,y),Cell(5,y),Cell(4,y+(phase==1?1:-1)),Cell(5,y+(phase==1?1:-1)),Cell(7,y),Cell(7,y+(phase==1?1:-1))})
                        if(ColonyConduits.TryRead(flow,cell,out var inventory)){if(inventory.Count==2)++mixed;reached+=inventory.Mass;}
                    Record((phase==1?"gas":"liquid")+" bridge and native branches carry actual mixtures",mixed>=2&&reached>0,$"{mixed} downstream mixed cells; {reached:R} kg");
                }
                Record("pipe mass, energy and every germ account close exactly",ColonyConduits.Audit(out var detail),detail);
                Record("native pipe conduction exchanged booked heat",Math.Abs(ColonyConduits.ExchangedHeatJoules)>1e-6,$"{ColonyConduits.ExchangedHeatJoules:R} J");
                Record("native pipe disease evolution exchanged booked germs",ColonyConduits.HasDiseaseExchange,detail);Finish();
            }
        }
        // A utility path as ONI's build tool lays it: each segment records its
        // connections (KAnimGraphTileVisualizer.UpdateConnections), which also
        // joins it to the network and survives a save and load.
        private static readonly System.Reflection.MethodInfo? updateConnections = AccessTools.Method(typeof(KAnimGraphTileVisualizer), "UpdateConnections");
        private static void Pipe(IUtilityNetworkMgr network, string id, params List<int>[] paths)
        {
            // Paths may share cells (two loads on one generator's port), and a
            // cell may already hold a segment of this utility: each cell gets
            // one segment carrying the union of its connections, so a second
            // path joins the network instead of rewriting it.
            var def = Assets.GetBuildingDef(id);
            var wanted = new Dictionary<int, UtilityConnections>(); var cells = new List<int>();
            foreach (var path in paths)
                for (var k = 0; k < path.Count; ++k)
                {
                    var connections = (UtilityConnections)0;
                    if (k > 0) connections |= UtilityConnectionsExtensions.DirectionFromToCell(path[k], path[k - 1]);
                    if (k + 1 < path.Count) connections |= UtilityConnectionsExtensions.DirectionFromToCell(path[k], path[k + 1]);
                    if (wanted.TryGetValue(path[k], out var had)) wanted[path[k]] = had | connections;
                    else { wanted[path[k]] = connections; cells.Add(path[k]); }
                }
            var segments = cells.Select(cell =>
            {
                var existing = Grid.Objects[cell, (int)def.ObjectLayer];
                if (existing != null) { wanted[cell] |= network.GetConnections(cell, true); return existing; }
                return def.Build(cell, Orientation.Neutral, null, def.DefaultElements(), 300, playsound: false);
            }).ToList();
            for (var k = 0; k < cells.Count; ++k)
            {
                var visualizer = segments[k] != null ? segments[k].GetComponent<KAnimGraphTileVisualizer>() : null;
                if (visualizer == null || updateConnections == null) throw new InvalidOperationException("A utility segment cannot record its connections.");
                updateConnections.Invoke(visualizer, new object[] { wanted[cells[k]] });
            }
        }

        // ---- helpers

        private static void Anchor()
        {
            var pad = GameUtil.GetActiveTelepad();
            var cell = pad != null ? Grid.PosToCell(pad) : Grid.XYToCell(Grid.WidthInCells / 2, Grid.HeightInCells / 2);
            Grid.CellToXY(cell, out var px, out var py);
            x0 = px + 9 + 11 * index; y0 = py;
            Current.Stage = $"arena at {x0},{y0}";
        }
        private static int Cell(int x, int y) => Grid.XYToCell(x0 + x, y0 + y);
        // The arena's border is sealed first (its fluid displaced outward)
        // and its inside replaced with vacuum (the owner removes what was
        // there), then filled two frames later, so placed fluid never mixes
        // with what the terrain held and no gas is squeezed out at once.
        // Solid cells of the inside (walls, floors) go in with the border:
        // the owner closes a cell the frame after the game places its solid,
        // so a wall placed with the fill would let fill gas cross it first.
        private static Func<int, int, (SimHashes element, float mass, float temperature)>? filling;
        private static void Arena(Func<int, int, (SimHashes element, float mass, float temperature)> inside)
        {
            // The world's plants, critters and items in the arena would take
            // part (and block what the scenario builds there).
            FloorArenas.Clear(x0 - 1, y0 - 1, Width + 2, Height + 2, out _);
            var tool = CellEventLogger.Instance.SandBoxTool;
            for (var y = -1; y <= Height; ++y)
                for (var x = -1; x <= Width; ++x)
                {
                    var border = x == -1 || y == -1 || x == Width || y == Height;
                    if (border) { SimMessages.ReplaceElement(Cell(x, y), SimHashes.Unobtanium, tool, 20000, 300); continue; }
                    var (element, mass, temperature) = inside(x, y);
                    if (IsSolid(element)) SimMessages.ReplaceElement(Cell(x, y), element, tool, mass, temperature);
                    else SimMessages.ReplaceElement(Cell(x, y), SimHashes.Vacuum, tool, 0, 0);
                }
            filling = inside;
        }
        private static void Fill()
        {
            if (filling == null || ColonyFrame() < 2) return;
            var tool = CellEventLogger.Instance.SandBoxTool;
            for (var y = 0; y < Height; ++y)
                for (var x = 0; x < Width; ++x)
                {
                    var (element, mass, temperature) = filling(x, y);
                    if (!IsSolid(element)) SimMessages.ReplaceElement(Cell(x, y), element, tool, mass, temperature);
                }
            filling = null;
        }
        private static bool IsSolid(SimHashes element) => ElementLoader.FindElementByHash(element)?.IsSolid == true;
        private static GameObject Build(string id, int cell)
        {
            var def = Assets.GetBuildingDef(id);
            var building = def.Build(cell, Orientation.Neutral, null, def.DefaultElements(), 300, playsound: false);
            powered.Add(building);
            if (def.RequiresPowerInput)
            {
                Grid.CellToXY(cell, out var bx, out _); var right = bx - x0 > 4;
                if (!generators.TryGetValue(right, out var generator))
                {
                    var dev = Assets.GetBuildingDef(DevGeneratorConfig.ID);
                    generator = dev.Build(Cell(right ? Width - 1 : 0, Height - 1), Orientation.Neutral, null, dev.DefaultElements(), 300, playsound: false);
                    generators[right] = generator;
                }
                Wire(Route(Grid.OffsetCell(Grid.PosToCell(generator), generator.GetComponent<Building>().Def.PowerOutputOffset),
                    Grid.OffsetCell(Grid.PosToCell(building), def.PowerInputOffset)));
            }
            return building;
        }
        private static readonly Dictionary<bool, GameObject> generators = new Dictionary<bool, GameObject>();
        // A machine's operating state for a check's detail.
        internal static string MachineState(GameObject? building)
        {
            if (building == null) return "no building";
            var operational = building.GetComponent<Operational>(); var power = building.GetComponent<EnergyConsumer>();
            var consumer = building.GetComponent<ElementConsumer>();
            var flags = "?";
            try
            {
                if (operational != null && AccessTools.Field(typeof(Operational), "Flags").GetValue(operational) is IDictionary<Operational.Flag, bool> all)
                    flags = string.Join(",", all.Where(f => !f.Value).Select(f => f.Key.Name));
            }
            catch (Exception) { }
            var sources = string.Join(" ", generators.Values.Where(g => g != null).Select(g =>
            {
                var source = g.GetComponent<Generator>(); var running = g.GetComponent<Operational>();
                return $"[generator circuit {source?.CircuitID}, operational {running?.IsOperational}, joules {source?.JoulesAvailable:G4}]";
            }));
            var solid = Entombing(building);
            var origin = Grid.PosToCell(building);
            var wet = string.Join(" ", (building.GetComponent<OccupyArea>()?.OccupiedCellsOffsets ?? new CellOffset[0])
                .Select(o => Grid.OffsetCell(origin, o)).Where(c => Grid.IsValidCell(c) && Grid.IsSubstantialLiquid(c))
                .Select(c => { Grid.CellToXY(c, out var x, out var y); return $"{Grid.Element[c].id} {Grid.Mass[c]:G3} kg@{x},{y}"; }));
            return $"operational {operational?.IsOperational}, active {operational?.IsActive}, unmet [{flags}], powered {power?.IsPowered}, circuit {power?.CircuitID}, " +
                $"consumer rate {consumer?.consumptionRate}, element available {consumer?.IsElementAvailable}; {sources}" + (solid.Length > 0 ? $"; solid cells {solid}" : "") +
                (wet.Length > 0 ? $"; liquid cells {wet}" : "");
        }
        // What entombs a building: the solid cells of its footprint.
        internal static string Entombing(GameObject building)
        {
            var origin = Grid.PosToCell(building);
            return string.Join(" ", (building.GetComponent<OccupyArea>()?.OccupiedCellsOffsets ?? new CellOffset[0])
                .Select(o => Grid.OffsetCell(origin, o)).Where(c => Grid.IsValidCell(c) && Grid.Solid[c])
                .Select(c => { Grid.CellToXY(c, out var x, out var y); return $"{Grid.Element[c].id}@{x},{y}"; }));
        }
        private static double CellMass(int cell)
        {
            var count = ColonyOwner.Composition(cell, out _, out var masses);
            double total = 0; for (var k = 0; k < count; ++k) total += masses[k];
            return total;
        }
        private static double SpeciesMass(int cell, SimHashes element)
        {
            var count = ColonyOwner.Composition(cell, out var indices, out var masses);
            for (var k = 0; k < count; ++k) if (ElementLoader.elements[(int)indices[k]].id == element) return masses[k];
            return 0;
        }
        // The ring of cells just outside the arena's border.
        private static double OutsideMass()
        {
            double total = 0;
            for (var y = -2; y <= Height + 1; ++y) for (var x = -2; x <= Width + 1; ++x)
                if (x == -2 || x == Width + 1 || y == -2 || y == Height + 1) total += CellMass(Cell(x, y));
            return total;
        }
        // The arena's gas: liquid booked at import (a reservoir) may seep back
        // into cells near its anchor, inside an arena too.
        private static double ArenaMass()
        {
            double total = 0;
            for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x) total += CellMass(Cell(x, y)) - LiquidMass(Cell(x, y));
            return total;
        }
        private static double GasTemperature(int row) => Enumerable.Range(0, Width).Average(x => (double)Grid.Temperature[Cell(x, row)]);
        private static bool Once(int frame, string stage)
        {
            if (step > frame || ColonyFrame() < frame) return false;
            step = frame + 1; Current.Stage = stage; return true;
        }
        private static int ColonyFrame() => (int)(ColonyOwner.Status.CompletedFrames - started);
        private static void Record(string name, bool passed, string detail) => Current.Checks.Add(new Check { Name = name, Passed = passed, Detail = detail });
        private static void Finish()
        {
            Current.Finished = true; Current.Stage = "finished";
            Current.Passed = Current.Checks.Count > 0 && Current.Checks.All(check => check.Passed);
        }
    }
}
