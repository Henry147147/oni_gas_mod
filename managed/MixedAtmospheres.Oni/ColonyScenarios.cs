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
    internal static class ColonyScenarios
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
        private static void Next() { Current = new Result(); started = 0; step = 0; recorded.Clear(); powered.Clear(); filling = null; pipeCells.Clear(); generators.Clear(); }

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
        private static void FreezeMelt(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x < 4 ? (y == 0 ? (SimHashes.Granite, 4000f, 40f) : y == 1 ? (SimHashes.Water, 20f, 274f) : (SimHashes.Oxygen, 1.2f, 280f))
                    : (y == 2 && x == 6 ? (SimHashes.Ice, 400f, 290f) : (SimHashes.Oxygen, 1.2f, 300f)));
            // Before the fill (frame 2): the warm ice melts within frames of being placed.
            if (Once(1, "start")) { recorded["opened"] = status.OpenedCells; recorded["closed"] = status.ClosedCells; recorded["injected"] = status.InjectedKg; }
            if (Once(42, "check"))
            {
                string Describe(int cell) => $"{Grid.Element[cell].id} {Grid.Mass[cell]:G4} kg {Grid.Temperature[cell]:F1} K{(Grid.Solid[cell] ? " solid" : "")}{(ColonyOwner.OwnsFluidCell(cell) ? " owned" : "")}";
                Record("ice melted into an owned cell", status.OpenedCells > recorded["opened"] && !Grid.Solid[Cell(6, 2)],
                    $"opened {status.OpenedCells - recorded["opened"]}; ice cell {Describe(Cell(6, 2))}");
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
            // ONI's pipes move a packet one segment per second: the first
            // reaches the vent after about eight seconds.
            if (Once(100, "check"))
            {
                var right = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => CellMass(Cell(x, y))));
                var oxygen = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Oxygen)));
                var carbon = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.CarbonDioxide)));
                var inPipe = pipeCells.Sum(c => (double)Game.Instance.gasConduitFlow.GetContents(c).mass);
                var stored = powered.Where(b => b != null).Sum(b => b.GetComponents<Storage>().Sum(st => st.items.Where(i => i != null).Sum(i => (double)(i.GetComponent<PrimaryElement>()?.Mass ?? 0))));
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
                // A generator runs only on a circuit: a wire on its output.
                Wire(new List<int> { Grid.OffsetCell(Grid.PosToCell(generator), generator.GetComponent<Building>().Def.PowerOutputOffset) });
            }
            if (Once(32, "check"))
            {
                var added = ArenaSpecies(SimHashes.CarbonDioxide) - recorded["co2"];
                Record("the generator's exhaust reached the owner", added > .005, $"+{added:G4} kg carbon dioxide; {MachineState(powered.FirstOrDefault())}");
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
            if (Once(6, "build")) { recorded["oxygen"] = ArenaSpecies(SimHashes.Oxygen); station = Build(OxygenMaskStationConfig.ID, Cell(3, 1)); }
            if (Once(36, "check"))
            {
                var held = station == null ? 0 : station.GetComponents<Storage>().Sum(s => s.GetMassAvailable(SimHashes.Oxygen));
                var taken = recorded["oxygen"] - ArenaSpecies(SimHashes.Oxygen);
                Record("the station filled with the owner's oxygen", held > .01 && Math.Abs(taken - held) < .01 * held + 1e-4,
                    $"took {taken:G4} kg, holds {held:G4} kg; {MachineState(station)}");
                Finish();
            }
        }

        // A polymer press turns stored petroleum into plastic and exhausts its
        // steam and carbon dioxide into the owner.
        private static GameObject? press;
        private static void PolymerPress(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .5f, 300f));
            if (Once(6, "build"))
            {
                recorded["out"] = ArenaSpecies(SimHashes.Steam) + ArenaSpecies(SimHashes.Water) + ArenaSpecies(SimHashes.CarbonDioxide);
                press = Build(PolymerizerConfig.ID, Cell(3, 1));
                press.GetComponent<Storage>().AddLiquid(SimHashes.Petroleum, 20, 300, byte.MaxValue, 0);
            }
            if (Once(40, "check"))
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
                AccessTools.Method(typeof(StructureTemperatureComponents), "DoMelt").Invoke(null, new object[] { element });
            }
            if (Once(14, "check"))
            {
                var added = status.InjectedKg - recorded["injected"];
                Record("the melted ladder's metal reached the owner", ladder == null && Math.Abs(added - recorded["mass"]) < .01 * recorded["mass"],
                    $"ladder {(ladder == null ? "gone" : "still standing")}, {recorded["mass"]:G4} kg melted, {added:G4} kg injected");
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
                Finish();
            }
        }

        // A steam rocket engine put into its burning state (as at launch)
        // exhausts hot steam into the owner.
        private static void RocketExhaust(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, .3f, 300f));
            if (Once(6, "burn"))
            {
                recorded["steam"] = ArenaSpecies(SimHashes.Steam) + ArenaSpecies(SimHashes.Water);
                var engine = Build(SteamEngineConfig.ID, Cell(3, 1)).GetComponent<RocketEngine>();
                var smi = engine.GetSMI<RocketEngine.StatesInstance>(); smi.GoTo(smi.sm.burning);
            }
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
            if (Once(26, "check"))
            {
                var on = sensor != null && sensor.IsSwitchedOn;
                Record("the sensor found the minority carbon dioxide", on && Grid.Element[Cell(3, 2)].id == SimHashes.Oxygen,
                    $"switched on {on}, cell shows {Grid.Element[Cell(3, 2)].id}, CO2 {SpeciesMass(Cell(3, 2), SimHashes.CarbonDioxide):G3} kg, " +
                    $"sensing {(sensor != null ? ElementLoader.elements[Traverse.Create(sensor).Field("desiredElementIdx").GetValue<int>()].id.ToString() : "-")}");
                Finish();
            }
        }

        // A pump sends the left chamber's oxygen and carbon dioxide through a
        // carbon dioxide filter: its filtered port carries only carbon dioxide
        // and its main output only oxygen.
        private static readonly List<int> filteredPipe = new List<int>(), passedPipe = new List<int>();
        private static void Filter(int frame)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : x > 4 ? (SimHashes.Vacuum, 0f, 0f) :
                    ((x + y) % 2 == 0 ? (SimHashes.Oxygen, 1.2f, 300f) : (SimHashes.CarbonDioxide, 1.6f, 300f)));
            if (Once(6, "build"))
            {
                filteredPipe.Clear(); passedPipe.Clear();
                var pump = Build(GasPumpConfig.ID, Cell(1, 0));
                var filter = Build(GasFilterConfig.ID, Cell(6, 3));
                filter.GetComponent<Filterable>().SelectedTag = ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).tag;
                var into = (int)AccessTools.Field(typeof(ElementFilter), "inputCell").GetValue(filter.GetComponent<ElementFilter>());
                var output = (int)AccessTools.Field(typeof(ElementFilter), "outputCell").GetValue(filter.GetComponent<ElementFilter>());
                var filtered = (int)AccessTools.Field(typeof(ElementFilter), "filteredCell").GetValue(filter.GetComponent<ElementFilter>());
                var from = Grid.OffsetCell(Cell(1, 0), pump.GetComponent<Building>().Def.UtilityOutputOffset);
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, Route(from, into));
                var a = Build(GasVentConfig.ID, Cell(7, 0)); var b = Build(GasVentConfig.ID, Cell(5, 0));
                passedPipe.AddRange(Route(output, Grid.OffsetCell(Cell(7, 0), a.GetComponent<Building>().Def.UtilityInputOffset)));
                filteredPipe.AddRange(Route(filtered, Grid.OffsetCell(Cell(5, 0), b.GetComponent<Building>().Def.UtilityInputOffset)));
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, passedPipe);
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, filteredPipe);
            }
            if (Once(40, "check"))
            {
                var flow = Game.Instance.gasConduitFlow;
                double co2 = 0, o2InFiltered = 0, o2 = 0, co2InPassed = 0;
                foreach (var c in filteredPipe) { var x = flow.GetContents(c); if (x.element == SimHashes.CarbonDioxide) co2 += x.mass; else if (x.mass > 0) o2InFiltered += x.mass; }
                foreach (var c in passedPipe) { var x = flow.GetContents(c); if (x.element == SimHashes.Oxygen) o2 += x.mass; else if (x.mass > 0) co2InPassed += x.mass; }
                var vented = ArenaSpecies(SimHashes.CarbonDioxide, 5) + ArenaSpecies(SimHashes.Oxygen, 5);
                Record("the filter separated the pumped mixture", (co2 > 0 || vented > .05) && o2InFiltered == 0 && co2InPassed == 0,
                    $"filtered port: CO2 {co2:G3} kg, other {o2InFiltered:G3} kg; output: O2 {o2:G3} kg, other {co2InPassed:G3} kg; vented {vented:G3} kg");
                Finish();
            }
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
                var held = powered.Where(b => b != null).Sum(b => b.GetComponents<Storage>().Sum(s => s.items.Where(i => i != null).Sum(i => (double)(i.GetComponent<PrimaryElement>()?.Mass ?? 0))));
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
        // oxygen diffuser and a carbon dioxide scrubber; the scrubber takes
        // water from a reservoir and returns polluted water to another; a
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
                // Left of the pod: generator, diffuser, coal and algae, enough
                // for the ten-cycle run with margin (the generator burns its
                // coal whenever its loads draw power).
                var generator = Place(GeneratorConfig.ID, ref left, -1);
                var diffuser = Place(MineralDeoxidizerConfig.ID, ref left, -1);
                Spill(SimHashes.Carbon, left, py, 5000); Spill(SimHashes.Carbon, left - 1, py, 5000); left -= 3;
                Spill(SimHashes.Algae, left, py, 4500); Spill(SimHashes.Algae, left - 1, py, 4500);
                // Right of the pod: scrubber, the water reservoirs and the ration box.
                var scrubber = Place(CO2ScrubberConfig.ID, ref right, 1);
                var water = Place(LiquidReservoirConfig.ID, ref right, 1);
                var waste = Place(LiquidReservoirConfig.ID, ref right, 1);
                var rations = Place(RationBoxConfig.ID, ref right, 1);
                // A cot each keeps the duplicants' stress down over the ten cycles.
                for (var bed = 0; bed < 3; ++bed) Place(BedConfig.ID, ref right, 1);
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
                    foreach (var load in new[] { diffuser, scrubber })
                        if (load != null) Wire(Route(power, Grid.OffsetCell(Grid.PosToCell(load), load.GetComponent<Building>().Def.PowerInputOffset)));
                }
                if (water != null && scrubber != null && waste != null)
                {
                    var def = scrubber.GetComponent<Building>().Def;
                    Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, Route(
                        Grid.OffsetCell(Grid.PosToCell(water), water.GetComponent<Building>().Def.UtilityOutputOffset), Grid.OffsetCell(Grid.PosToCell(scrubber), def.UtilityInputOffset)));
                    Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, Route(
                        Grid.OffsetCell(Grid.PosToCell(scrubber), def.UtilityOutputOffset), Grid.OffsetCell(Grid.PosToCell(waste), waste.GetComponent<Building>().Def.UtilityInputOffset)));
                }
                Current.Stage = "built " + string.Join(", ", placed);
            }
            if (Once(30, "check"))
            {
                Record("the base was built", placed.Count == 9, string.Join(", ", placed));
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
            for (var tries = 0; tries < 12; ++tries, x += step)
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
        private static void Wire(List<int> path) => Pipe(Game.Instance.electricalConduitSystem, WireConfig.ID, path);

        private static double OwnedMass(SimHashes element)
        {
            double total = 0;
            for (var cell = 0; cell < Grid.CellCount; ++cell) if (ColonyOwner.OwnsFluidCell(cell)) total += SpeciesMass(cell, element);
            return total;
        }

        private static readonly List<int> pipeCells = new List<int>();
        // A utility path as ONI's build tool lays it: each segment records its
        // connections (KAnimGraphTileVisualizer.UpdateConnections), which also
        // joins it to the network and survives a save and load.
        private static readonly System.Reflection.MethodInfo? updateConnections = AccessTools.Method(typeof(KAnimGraphTileVisualizer), "UpdateConnections");
        private static void Pipe(IUtilityNetworkMgr network, string id, List<int> path)
        {
            var def = Assets.GetBuildingDef(id);
            var segments = path.Select(cell => def.Build(cell, Orientation.Neutral, null, def.DefaultElements(), 300, playsound: false)).ToList();
            for (var k = 0; k < path.Count; ++k)
            {
                var connections = (UtilityConnections)0;
                if (k > 0) connections |= UtilityConnectionsExtensions.DirectionFromToCell(path[k], path[k - 1]);
                if (k + 1 < path.Count) connections |= UtilityConnectionsExtensions.DirectionFromToCell(path[k], path[k + 1]);
                var visualizer = segments[k] != null ? segments[k].GetComponent<KAnimGraphTileVisualizer>() : null;
                if (visualizer == null || updateConnections == null) throw new InvalidOperationException("A utility segment cannot record its connections.");
                updateConnections.Invoke(visualizer, new object[] { connections });
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
        private static Func<int, int, (SimHashes element, float mass, float temperature)>? filling;
        private static void Arena(Func<int, int, (SimHashes element, float mass, float temperature)> inside)
        {
            var tool = CellEventLogger.Instance.SandBoxTool;
            for (var y = -1; y <= Height; ++y)
                for (var x = -1; x <= Width; ++x)
                {
                    var border = x == -1 || y == -1 || x == Width || y == Height;
                    if (border) SimMessages.ReplaceElement(Cell(x, y), SimHashes.Unobtanium, tool, 20000, 300);
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
                    SimMessages.ReplaceElement(Cell(x, y), element, tool, mass, temperature);
                }
            filling = null;
        }
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
        private static string MachineState(GameObject? building)
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
            return $"operational {operational?.IsOperational}, active {operational?.IsActive}, unmet [{flags}], powered {power?.IsPowered}, circuit {power?.CircuitID}, " +
                $"consumer rate {consumer?.consumptionRate}, element available {consumer?.IsElementAvailable}; {sources}";
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
