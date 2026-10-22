using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // The fixed comparison arenas F1-F5 of docs/base-game-floor.md. One arena
    // is carved beside the printing pod of a new colony and sampled at every
    // 200 ms simulation tick from that tick's published game data, under ONI's
    // own simulation (mode colony-vanilla: the colony owner never starts) or
    // under the colony owner. Coordinates are local and include the Unobtanium
    // border. Numbers only: tools/floor_metrics.py computes the metrics.
    // Harness only: cells change through the sandbox tool's messages and the
    // F2 door is built instantly.
    internal static unsafe class FloorArenas
    {
        internal static readonly string[] Ids = { "f1", "f2", "f3", "f4", "f5" };
        private const int PrepareGameData = 1078620451;
        private const double Atmosphere = 101325, GasConstant = 8.314462618, Room = 293.15;
        // Gas, door, levelling and layering runs last 120 s after release; F4 20000 s.
        private static int Duration => Id == "f4" ? 100000 : 600;

        internal static string Id { get; private set; } = "";
        private static long ticks;
        private static long built = -1, filled = -1, locked = -1, commanded = -1, opened = -1, release = -1;
        private static double releasedAt = -1, simulatedAt = -1; // game clock and simulated time at release, s
        private static int width, height, x0, y0;
        private static Door? door;
        private static StreamWriter? samples;
        private static readonly SortedDictionary<int, Dictionary<string, object>> seen = new SortedDictionary<int, Dictionary<string, object>>();
        private static readonly Dictionary<string, double> placed = new Dictionary<string, double>();
        private static string output = "";

        internal static void Configure(string id, string directory)
        {
            if (Array.IndexOf(Ids, id) < 0) throw new ArgumentException("Unknown floor arena " + id);
            Id = id; output = directory; ticks = 0; built = filled = locked = commanded = opened = release = -1; releasedAt = -1;
            door = null; simulated = 0; seen.Clear(); placed.Clear(); samples?.Dispose(); samples = null;
            (width, height) = id switch { "f1" or "f2" => (19, 10), "f3" => (22, 14), "f4" => (7, 4), _ => (10, 14) };
        }

        // Simulated time: every native frame the game posts (NewGameFrame),
        // before it is sent.
        private static double simulated;
        internal static void OnFrame(float seconds) { if (Id.Length != 0 && seconds > 0) simulated += seconds; }
        // Every native message, after the owner's publication. The game asks
        // for its data about once per Unity frame; the first update after the
        // simulated time passes each 0.2 s mark is that tick's sample.
        internal static void OnNativeMessage(int message, IntPtr data)
        {
            if (Id.Length == 0 || message != PrepareGameData || data == IntPtr.Zero) return;
            var tick = (long)Math.Floor(simulated / .2 + 1e-9);
            if (tick <= ticks) return;
            ticks = tick;
            var update = (Sim.GameDataUpdate*)data;
            if (filled < 0 || samples == null || release >= 0 && ticks > release + Duration) return;
            var line = new System.Text.StringBuilder(32 * width * height);
            line.Append(ticks.ToString(CultureInfo.InvariantCulture));
            var fluid = false;
            // Per cell: its constituents (count, then element and kg each) and
            // its temperature. ONI's cell holds one element; an owned cell
            // holds the owner's mixture (the published element is only its
            // dominant one).
            for (var y = 1; y < height - 1; ++y)
                for (var x = 1; x < width - 1; ++x)
                {
                    var cell = Cell(x, y);
                    if (ColonyOwner.OwnsFluidCell(cell))
                    {
                        var count = ColonyOwner.Composition(cell, out var indices, out var masses);
                        line.Append(' ').Append(count);
                        for (var k = 0; k < count; ++k) Part((int)indices[k], masses[k]);
                    }
                    else { line.Append(" 1"); Part(update->elementIdx[cell], update->mass[cell]); }
                    line.Append(' ').Append(update->temperature[cell].ToString("R", CultureInfo.InvariantCulture));
                }
            void Part(int element, double kg)
            {
                if (kg > 0 && (ElementLoader.elements[element].IsGas || ElementLoader.elements[element].IsLiquid)) fluid = true;
                if (!seen.ContainsKey(element)) seen[element] = Describe(ElementLoader.elements[element]);
                line.Append(' ').Append(element).Append(' ').Append(kg.ToString("R", CultureInfo.InvariantCulture));
            }
            samples.WriteLine(line.ToString());
            // F4: heat exchange starts with the fill; t = 0 is the first sample
            // holding it (the game applies the fill at its next step).
            if (Id == "f4" && release < 0 && fluid) release = ticks;
            // F2: t = 0 is the first sample after the door is fully open.
            if (commanded >= 0 && opened < 0 && !ElementLoader.elements[update->elementIdx[Cell(9, 4)]].IsSolid &&
                !ElementLoader.elements[update->elementIdx[Cell(9, 5)]].IsSolid) opened = release = ticks;
        }

        // Every Unity frame: build, fill and release at their ticks. True
        // when the arena's run is complete and its record written.
        internal static bool Update()
        {
            if (Id.Length == 0) return false;
            if (built < 0)
            {
                if (ticks < 2) return false;
                Anchor(); Build(); built = ticks; return false;
            }
            if (filled < 0)
            {
                if (ticks < built + 2) return false;
                Fill(); filled = ticks;
                samples = new StreamWriter(Path.Combine(output, "floor-" + Id + ".txt"));
                return false;
            }
            switch (Id)
            {
                case "f2":
                    if (door == null && ticks >= filled + 1)
                        door = Assets.GetBuildingDef(ManualPressureDoorConfig.ID).Build(Cell(9, 4), Orientation.Neutral, null,
                            Assets.GetBuildingDef(ManualPressureDoorConfig.ID).DefaultElements(), (float)Room, playsound: false).GetComponent<Door>();
                    // The door takes control-state changes once it has spawned.
                    if (door != null && locked < 0 && ticks >= filled + 3) { SetDoor(Door.ControlState.Locked); locked = ticks; }
                    // Hold closed for 10 s (50 ticks), then open.
                    if (locked >= 0 && commanded < 0 && ticks >= locked + 50) { SetDoor(Door.ControlState.Opened); commanded = ticks; }
                    break;
                case "f1": case "f3": case "f5":
                    if (release < 0 && ticks >= filled + 5)
                    {
                        // The t = 0 sample is this tick's: the wall goes at the next.
                        var wall = Id == "f1" ? Enumerable.Range(1, 8).Select(y => (9, y)) : Id == "f3" ? Enumerable.Range(1, 8).Select(y => (6, y))
                            : Enumerable.Range(1, 8).Select(x => (x, 6));
                        foreach (var (x, y) in wall) Replace(x, y, SimHashes.Vacuum, 0, 0);
                        release = ticks;
                    }
                    break;
            }
            if (release >= 0 && releasedAt < 0) { releasedAt = GameClock.Instance.GetTime(); simulatedAt = simulated; }
            if (release < 0 || ticks < release + Duration) return false;
            Finish(); return true;
        }

        // The element's identity and the properties the metrics need, read
        // from the running game (never committed).
        private static Dictionary<string, object> Describe(Element e) => new Dictionary<string, object>
        {
            ["id"] = e.id.ToString(), ["phase"] = e.IsVacuum ? "vacuum" : e.IsGas ? "gas" : e.IsLiquid ? "liquid" : "solid",
            ["molar_mass_kg_per_mol"] = e.molarMass / 1000.0, ["specific_heat_j_per_kg_k"] = e.specificHeatCapacity * 1000.0,
            ["conductivity_w_per_m_k"] = e.thermalConductivity,
        };
        private static void Anchor()
        {
            var pad = GameUtil.GetActiveTelepad();
            var cell = pad != null ? Grid.PosToCell(pad) : Grid.XYToCell(Grid.WidthInCells / 2, Grid.HeightInCells / 2);
            Grid.CellToXY(cell, out var px, out var py);
            x0 = px + 9; y0 = py;
        }
        private static int Cell(int x, int y) => Grid.XYToCell(x0 + x, y0 + y);
        private static void Replace(int x, int y, SimHashes element, double mass, double temperature) =>
            SimMessages.ReplaceElement(Cell(x, y), element, CellEventLogger.Instance.SandBoxTool, (float)mass, (float)temperature);

        // The removable walls (F4's divider is granite, not a wall).
        private static bool Wall(int x, int y) => y >= 1 && y <= 8 && (Id == "f1" || Id == "f2") && x == 9 || Id == "f3" && x == 6 && y <= 8 ||
            Id == "f5" && y == 6 && x <= 8;
        private static bool Solid(int x, int y) => Wall(x, y) || Id == "f4" && x == 3;
        // The border, the solid walls and vacuum inside; F4's granite divider.
        private static void Build()
        {
            for (var y = 0; y < height; ++y)
                for (var x = 0; x < width; ++x)
                {
                    var border = x == 0 || y == 0 || x == width - 1 || y == height - 1;
                    if (border || Wall(x, y)) Replace(x, y, SimHashes.Unobtanium, 20000, Room);
                    else if (Id == "f4" && x == 3)
                    {
                        var tile = Assets.GetBuildingDef(TileConfig.ID).Mass[0];
                        placed["granite_tile_kg"] = tile;
                        Replace(x, y, SimHashes.Granite, tile, 318.15);
                    }
                    else Replace(x, y, SimHashes.Vacuum, 0, 0);
                }
        }

        // Gas from the ideal-gas law at the stated pressure (1 m^3 cells);
        // liquid as full cells at 1 atm in the colony catalogue's reference EOS.
        // Every other open cell is emptied again: under the owner, closing the
        // border pushed its fluid into the arena.
        private static void Fill()
        {
            for (var y = 1; y < height - 1; ++y)
                for (var x = 1; x < width - 1; ++x)
                    if (!Solid(x, y)) Replace(x, y, SimHashes.Vacuum, 0, 0);
            var oxygen = ElementLoader.FindElementByHash(SimHashes.Oxygen);
            double Gas(double pa, double kelvin) => pa * oxygen.molarMass / 1000 / (GasConstant * kelvin);
            placed["oxygen_molar_mass_kg_per_mol"] = oxygen.molarMass / 1000;
            switch (Id)
            {
                case "f1": case "f2":
                    var left = Gas(Id == "f1" ? Atmosphere : 2 * Atmosphere, Room);
                    var right = Id == "f1" ? 0 : Gas(.5 * Atmosphere, Room);
                    placed["left_cell_kg"] = left; placed["right_cell_kg"] = right;
                    for (var y = 1; y <= 8; ++y)
                        for (var x = 1; x <= 17; ++x)
                            if (x < 9) Replace(x, y, SimHashes.Oxygen, left, Room);
                            else if (x > 9 && right > 0) Replace(x, y, SimHashes.Oxygen, right, Room);
                    break;
                case "f3":
                    var water = FullCell(SimHashes.Water);
                    for (var y = 1; y <= 8; ++y) for (var x = 1; x <= 5; ++x) Replace(x, y, SimHashes.Water, water, Room);
                    break;
                case "f4":
                    placed["hot_cell_kg"] = Gas(Atmosphere, 353.15); placed["cold_cell_kg"] = Gas(Atmosphere, 283.15);
                    for (var y = 1; y <= 2; ++y)
                    {
                        for (var x = 1; x <= 2; ++x) Replace(x, y, SimHashes.Oxygen, placed["hot_cell_kg"], 353.15);
                        for (var x = 4; x <= 5; ++x) Replace(x, y, SimHashes.Oxygen, placed["cold_cell_kg"], 283.15);
                    }
                    break;
                case "f5":
                    var w = FullCell(SimHashes.Water); var oil = FullCell(SimHashes.CrudeOil);
                    for (var x = 1; x <= 8; ++x)
                    {
                        for (var y = 1; y <= 4; ++y) Replace(x, y, SimHashes.Water, w, Room);
                        for (var y = 7; y <= 8; ++y) Replace(x, y, SimHashes.CrudeOil, oil, Room);
                    }
                    break;
            }
        }
        private static double FullCell(SimHashes liquid)
        {
            var materials = ColonyOwner.BuildCatalogue();
            OniNativeColony.Check(OniNativeColony.LiquidDensity(materials, (uint)materials.Length,
                (uint)ElementLoader.FindElementByHash(liquid).idx, Atmosphere, out var density));
            placed[liquid + "_full_cell_kg"] = density;
            return density;
        }
        // The game changes a door's state through an errand unless instant
        // building is on; the arena applies the request at once.
        private static void SetDoor(Door.ControlState state)
        {
            door!.QueueStateChange(state);
            AccessTools.Method(typeof(Door), "ApplyRequestedControlState").Invoke(door, Array.Empty<object>());
        }

        private static void Finish()
        {
            samples?.Dispose(); samples = null;
            var record = new Dictionary<string, object>
            {
                ["arena"] = Id, ["width"] = width, ["height"] = height, ["tick_seconds"] = .2,
                ["simulation"] = ColonyOwner.Active ? "colony owner" : "ONI native",
                ["cells"] = "interior, rows y=1..height-2, x=1..width-2; per cell: constituent count, (element index, kg) each, temperature K",
                ["elements"] = seen.ToDictionary(e => e.Key.ToString(CultureInfo.InvariantCulture), e => (object)e.Value),
                ["placed"] = placed,
                ["ticks"] = new Dictionary<string, long> { ["built"] = built, ["filled"] = filled, ["locked"] = locked, ["commanded"] = commanded,
                    ["opened"] = opened, ["release"] = release, ["last"] = release + Duration },
                // A check on the tick length: game seconds from release to the end.
                ["game_seconds_after_release"] = GameClock.Instance.GetTime() - releasedAt,
                ["simulated_seconds_after_release"] = simulated - simulatedAt,
            };
            File.WriteAllText(Path.Combine(output, "floor-" + Id + ".json"), ReportJson.Serialize(record));
        }
    }
}
