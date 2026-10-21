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
    // throughout. Harness only: scenario buildings are powered by setting
    // their powered flag.
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
        private static void Next() { Current = new Result(); started = 0; step = 0; recorded.Clear(); powered.Clear(); filling = null; pipeCells.Clear(); }

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
            foreach (var building in powered)
                if (building != null) building.GetComponent<Operational>()?.SetFlag(EnergyConsumer.PoweredFlag, true);
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
                    case "pump": Pump(frame); break;
                    case "deoxidizer": Deoxidizer(frame, status); break;
                    case "pipes": Pipes(frame); break;
                    case "door": DoorCycle(frame, status); break;
                    case "sublimation": Sublimation(frame, status); break;
                    case "falling": Falling(frame); break;
                    case "breathing": Breathing(frame, status); break;
                    case "airflow": Airflow(frame); break;
                    default: throw new InvalidOperationException("Unknown colony scenario " + name);
                }
            }
            catch (Exception error) { Record("scenario ran", false, error.Message); Finish(); }
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
                recorded["gas"] = ArenaMass(); recorded["opened"] = ColonyOwner.Status.OpenedCells;
                for (var y = 0; y < Height; ++y) SimMessages.Dig(Cell(4, y));
            }
            if (Once(14, "build"))
            {
                var right = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => CellMass(Cell(x, y))));
                Record("dug column opened", ColonyOwner.Status.OpenedCells >= recorded["opened"] + Height, $"opened {ColonyOwner.Status.OpenedCells - recorded["opened"]}");
                Record("gas reached the far chamber", right > .5, $"{right:G4} kg");
                Record("dig conserved the gas", Math.Abs(ArenaMass() - recorded["gas"]) < 1e-6 * recorded["gas"], $"{recorded["gas"]:G9} -> {ArenaMass():G9} kg");
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
            if (Once(6, "start")) { recorded["t"] = GasTemperature(1); recorded["heat"] = status.NativeHeatJoules; }
            if (Once(27, "check"))
            {
                var t = GasTemperature(1);
                Record("air above the floor warmed", t > recorded["t"] + .2, $"{recorded["t"]:F3} -> {t:F3} K");
                Record("native heat booked to the owner", status.NativeHeatJoules > recorded["heat"], $"{status.NativeHeatJoules - recorded["heat"]:G4} J");
                Finish();
            }
        }

        // Water over a very hot floor evaporates into the air above it.
        private static void Phase(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena"))
                Arena((x, y) => y == 0 ? (SimHashes.Granite, 4000f, 900f) : y <= 2 ? (SimHashes.Water, 990f, 365f) : (SimHashes.Oxygen, 1.2f, 300f));
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
                Arena((x, y) => x < 4 ? (y == 0 ? (SimHashes.Granite, 4000f, 40f) : y == 1 ? (SimHashes.Water, 990f, 274f) : (SimHashes.Oxygen, 1.2f, 280f))
                    : (y == 2 && x == 6 ? (SimHashes.Ice, 400f, 290f) : (SimHashes.Oxygen, 1.2f, 300f)));
            if (Once(6, "start")) { recorded["opened"] = status.OpenedCells; recorded["closed"] = status.ClosedCells; recorded["injected"] = status.InjectedKg; }
            if (Once(42, "check"))
            {
                Record("ice melted into an owned cell", status.OpenedCells > recorded["opened"] && !Grid.Solid[Cell(6, 2)], $"opened {status.OpenedCells - recorded["opened"]}");
                Record("melt water booked", status.InjectedKg > recorded["injected"] + 10, $"{status.InjectedKg - recorded["injected"]:G4} kg");
                var frozen = Enumerable.Range(0, 4).Count(x => Grid.Solid[Cell(x, 1)]);
                Record("water froze on the cold block", status.ClosedCells > recorded["closed"] && frozen > 0, $"{frozen} frozen cells");
                Finish();
            }
        }

        // A gas pump draws the arena's air into its storage through ONI's
        // native element consumer, which the shadow books.
        private static void Pump(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "build")) { recorded["gas"] = ArenaMass(); Build(GasPumpConfig.ID, Cell(3, 1)); }
            if (Once(27, "check"))
            {
                var pump = powered.FirstOrDefault();
                var stored = pump == null ? 0 : pump.GetComponents<Storage>().Sum(s => s.items.Where(i => i != null).Sum(i => i.GetComponent<PrimaryElement>()?.Mass ?? 0));
                var drawn = recorded["gas"] - ArenaMass();
                Record("the pump drew gas", drawn > .05, $"{drawn:G4} kg");
                Record("the pump stores what the owner lost", Math.Abs(stored - drawn) <= .02 * drawn + 1e-3, $"stored {stored:G4} kg");
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
                var building = Build(MineralDeoxidizerConfig.ID, Cell(3, 1));
                building.GetComponent<Storage>().AddOre(SimHashes.Algae, 100, 300, byte.MaxValue, 0);
            }
            if (Once(32, "check"))
            {
                var added = ArenaMass() - recorded["gas"];
                Record("the machine's oxygen reached the owner", status.InjectedKg > recorded["injected"] + .01 && added > .01, $"{added:G4} kg");
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
                recorded["gas"] = ArenaMass();
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
            if (Once(40, "check"))
            {
                var right = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => CellMass(Cell(x, y))));
                var oxygen = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Oxygen)));
                var carbon = Enumerable.Range(5, 3).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.CarbonDioxide)));
                var inPipe = pipeCells.Sum(c => (double)Game.Instance.gasConduitFlow.GetContents(c).mass);
                var stored = powered.Where(b => b != null).Sum(b => b.GetComponents<Storage>().Sum(st => st.items.Where(i => i != null).Sum(i => (double)(i.GetComponent<PrimaryElement>()?.Mass ?? 0))));
                Record("gas crossed through the pipe", right > .05, $"{right:G4} kg in the far chamber");
                Record("the pipe carried both gases", oxygen > 0 && carbon > 0, $"oxygen {oxygen:G4} kg, carbon dioxide {carbon:G4} kg");
                var total = ArenaMass() + inPipe + stored;
                Record("owner, pipe and pump keep the gas", Math.Abs(total - recorded["gas"]) < .01 * recorded["gas"], $"{recorded["gas"]:G6} -> {total:G6} kg");
                Finish();
            }
        }
        // A pneumatic door between oxygen and vacuum: closed it seals, opened
        // the gas passes, and closing again displaces the gas in its cells.
        private static Door? door;
        private static void DoorCycle(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena"))
                Arena((x, y) => x == 4 ? (y >= 2 ? (SimHashes.Unobtanium, 20000f, 300f) : (SimHashes.Vacuum, 0f, 0f)) :
                    x < 4 ? (SimHashes.Oxygen, 1.2f, 300f) : (SimHashes.Vacuum, 0f, 0f));
            if (Once(6, "build")) { door = Build(DoorConfig.ID, Cell(4, 0)).GetComponent<Door>(); SetDoor(Door.ControlState.Locked); }
            if (Once(10, "closed")) { recorded["gas"] = ArenaMass(); recorded["right"] = RightMass(); }
            if (Once(16, "open"))
            {
                Record("the closed door sealed", RightMass() <= recorded["right"] + 1e-9, $"{RightMass():G4} kg beyond the door");
                recorded["opened"] = status.OpenedCells; SetDoor(Door.ControlState.Opened);
            }
            if (Once(28, "close"))
            {
                Record("the open door let gas through", status.OpenedCells >= recorded["opened"] + 2 && RightMass() > .05, $"{RightMass():G4} kg beyond the door");
                Record("opening conserved the gas", Math.Abs(ArenaMass() - recorded["gas"]) < 1e-6 * recorded["gas"], $"{recorded["gas"]:G9} -> {ArenaMass():G9} kg");
                recorded["closed"] = status.ClosedCells; SetDoor(Door.ControlState.Locked);
            }
            if (Once(34, "check"))
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
                SimMessages.ReplaceElement(Cell(3, 4), SimHashes.Sand, CellEventLogger.Instance.SandBoxTool, 500, 300);
            }
            if (Once(30, "check"))
            {
                Record("the sand fell", !Grid.Solid[Cell(3, 4)] && Grid.Solid[Cell(3, 0)], $"top solid {Grid.Solid[Cell(3, 4)]}, floor solid {Grid.Solid[Cell(3, 0)]}");
                Record("the falling cell reopened and the landing cell closed", ColonyOwner.Status.OpenedCells > recorded["opened"] &&
                    ColonyOwner.Status.ClosedCells > recorded["closed"] + 1, $"opened {ColonyOwner.Status.OpenedCells - recorded["opened"]}, closed {ColonyOwner.Status.ClosedCells - recorded["closed"]}");
                Record("the gas was kept", Math.Abs(ArenaMass() - recorded["gas"]) < 1e-6 * recorded["gas"], $"{recorded["gas"]:G9} -> {ArenaMass():G9} kg");
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

        private static double OwnedMass(SimHashes element)
        {
            double total = 0;
            for (var cell = 0; cell < Grid.CellCount; ++cell) if (ColonyOwner.OwnsFluidCell(cell)) total += SpeciesMass(cell, element);
            return total;
        }

        private static readonly List<int> pipeCells = new List<int>();
        private static void Pipe(IUtilityNetworkMgr network, string id, List<int> path)
        {
            for (var k = 0; k + 1 < path.Count; ++k)
            {
                var direction = UtilityConnectionsExtensions.DirectionFromToCell(path[k], path[k + 1]);
                network.AddConnection(direction, path[k], false);
                network.AddConnection(direction.InverseDirection(), path[k + 1], false);
            }
            var def = Assets.GetBuildingDef(id);
            foreach (var cell in path) def.Build(cell, Orientation.Neutral, null, def.DefaultElements(), 300, playsound: false);
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
        // The arena is sealed first (every cell solid, so the owner moves
        // whatever fluid was there out of it) and filled two frames later, so
        // placed fluid never mixes with what the terrain held.
        private static Func<int, int, (SimHashes element, float mass, float temperature)>? filling;
        private static void Arena(Func<int, int, (SimHashes element, float mass, float temperature)> inside)
        {
            var tool = CellEventLogger.Instance.SandBoxTool;
            for (var y = -1; y <= Height; ++y)
                for (var x = -1; x <= Width; ++x) SimMessages.ReplaceElement(Cell(x, y), SimHashes.Unobtanium, tool, 20000, 300);
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
            return building;
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
        private static double ArenaMass()
        {
            double total = 0;
            for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x) total += CellMass(Cell(x, y));
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
