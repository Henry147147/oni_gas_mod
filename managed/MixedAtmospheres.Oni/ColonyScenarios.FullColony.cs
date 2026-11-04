using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Full-colony benchmark builds (docs/performance-acceptance.md#full-colony-benchmarks).
    // `full-colony-base` turns the pinned Terra start (pod, 3 duplicants) into a
    // dug-out and occupied colony in one instant, deterministic build: three
    // stacked halls, tunnels into the neighbouring biomes, sealed vaults, piped
    // machines, 18 duplicants, critters and plants. Harness only: buildings are
    // built instantly, but nothing is invincible, free or refilled afterwards.
    // The scenario ends after the colony has run for a while; `--save-colony`
    // then writes the built save that a long run settles.
    internal static partial class ColonyScenarios
    {
        private enum FcKind { Gas, Liquid, Wire }
        private sealed class FcNet
        {
            public readonly string Name; public readonly FcKind Kind;
            public readonly HashSet<int> Cells = new HashSet<int>();
            public readonly List<List<int>> Paths = new List<List<int>>();
            public FcNet(string name, FcKind kind) { Name = name; Kind = kind; }
        }
        private sealed class FcRoom { public string Name = ""; public int X, Y, W, H; public SimHashes Gas = SimHashes.Oxygen; public float Mass, Temperature; public SimHashes Extra; public float ExtraMass; }

        private static int fcX, fcY, fcWorld;                      // the pod's cell and its world
        private static readonly List<string> fcNotes = new List<string>();
        private static readonly List<FcRoom> fcRooms = new List<FcRoom>();
        private static readonly List<int> fcRoomCells = new List<int>();
        private static readonly Dictionary<FcKind, Dictionary<int, FcNet>> fcUsed = new Dictionary<FcKind, Dictionary<int, FcNet>>
            { [FcKind.Gas] = new Dictionary<int, FcNet>(), [FcKind.Liquid] = new Dictionary<int, FcNet>(), [FcKind.Wire] = new Dictionary<int, FcNet>() };
        private static readonly Dictionary<FcKind, HashSet<int>> fcPorts = new Dictionary<FcKind, HashSet<int>>
            { [FcKind.Gas] = new HashSet<int>(), [FcKind.Liquid] = new HashSet<int>(), [FcKind.Wire] = new HashSet<int>() };
        private static readonly List<FcNet> fcNets = new List<FcNet>();
        private static readonly Dictionary<string, GameObject> fcMachines = new Dictionary<string, GameObject>();
        private static readonly List<GameObject> fcLoads = new List<GameObject>();
        private static int fcBuildings, fcSolidSkipped, fcSealed;
        private static readonly List<GameObject> fcDuplicants = new List<GameObject>();

        private static int FC(int dx, int dy) => Grid.XYToCell(fcX + dx, fcY + dy);
        private static bool FcHome(int cell) => Grid.IsValidCell(cell) && Grid.WorldIdx[cell] == fcWorld;
        private static void FcNote(string text) { fcNotes.Add(text); Debug.Log("[FC-NOTE] " + text); }

        // ---- terrain

        // Removes plants, critters and items in a rectangle (never the pod's
        // footprint or a duplicant).
        private static void FcClear(int dx, int dy, int w, int h)
        {
            var pad = GameUtil.GetActiveTelepad();
            var cells = Grid.WorldIdx[Grid.PosToCell(pad)] == fcWorld ? pad.GetComponent<Building>().PlacementCells : new int[] { Grid.XYToCell(0, 0) };
            var minX = cells.Min(c => { Grid.CellToXY(c, out var x, out _); return x; }) - 1; var maxX = cells.Max(c => { Grid.CellToXY(c, out var x, out _); return x; }) + 1;
            var minY = cells.Min(c => { Grid.CellToXY(c, out _, out var y); return y; }) - 1; var maxY = cells.Max(c => { Grid.CellToXY(c, out _, out var y); return y; }) + 1;
            // The rectangle minus the pod's box: up to four rectangles.
            var left = fcX + dx; var bottom = fcY + dy; var right = left + w - 1; var top = bottom + h - 1;
            void Part(int l, int b, int r, int t) { if (r >= l && t >= b) FloorArenas.Clear(l, b, r - l + 1, t - b + 1, out _); }
            if (right < minX || left > maxX || top < minY || bottom > maxY) { Part(left, bottom, right, top); return; }
            Part(left, bottom, Math.Min(right, minX - 1), top);
            Part(Math.Max(left, maxX + 1), bottom, right, top);
            Part(Math.Max(left, minX), bottom, Math.Min(right, maxX), Math.Min(top, minY - 1));
            Part(Math.Max(left, minX), Math.Max(bottom, maxY + 1), Math.Min(right, maxX), top);
        }
        // Empties a rectangle of everything the dig tool may remove; the cells
        // are remembered for the later fill. Hard rock (Abyssalite and up) and
        // cells holding a building stay.
        private static void FcCarve(FcRoom room, bool ring = false)
        {
            var tool = CellEventLogger.Instance.SandBoxTool;
            if (ring) FcClear(room.X - 1, room.Y - 1, room.W + 2, room.H + 2); else FcClear(room.X, room.Y, room.W, room.H);
            for (var y = -1; y <= room.H; ++y)
                for (var x = -1; x <= room.W; ++x)
                {
                    var c = FC(room.X + x, room.Y + y);
                    if (!FcHome(c)) continue;
                    var border = x == -1 || y == -1 || x == room.W || y == room.H;
                    if (border)
                    {
                        if (!ring || Grid.Solid[c]) continue;
                        SimMessages.ReplaceElement(c, SimHashes.Granite, tool, 1000, 300, byte.MaxValue, 0, -1); ++fcSealed;
                        continue;
                    }
                    var element = Grid.Element[c];
                    if (Grid.Objects[c, (int)ObjectLayer.Building] != null && Grid.Objects[c, (int)ObjectLayer.Building].GetComponent<Telepad>() != null) continue;
                    if (element.IsSolid)
                    {
                        if (element.hardness >= 150) { ++fcSolidSkipped; continue; }
                        SimMessages.ReplaceElement(c, SimHashes.Vacuum, tool, 0, 0, byte.MaxValue, 0, -1);
                    }
                    fcRoomCells.Add(c);
                }
            // Loose ground resting on the room would fall in: dig its columns out.
            for (var x = 0; x < room.W; ++x)
                for (var y = room.Y + room.H; ; ++y)
                {
                    var c = FC(room.X + x, y);
                    if (!FcHome(c) || !Grid.Solid[c] || !Grid.Element[c].IsUnstable || Grid.Objects[c, (int)ObjectLayer.Building] != null) break;
                    SimMessages.ReplaceElement(c, SimHashes.Vacuum, tool, 0, 0, byte.MaxValue, 0, -1);
                }
            fcRooms.Add(room);
        }
        private static void FcFillRoom(FcRoom room)
        {
            var tool = CellEventLogger.Instance.SandBoxTool;
            for (var y = 0; y < room.H; ++y)
                for (var x = 0; x < room.W; ++x)
                {
                    var c = FC(room.X + x, room.Y + y);
                    if (!FcHome(c) || Grid.Solid[c] || Grid.IsSubstantialLiquid(c) || Grid.Objects[c, (int)ObjectLayer.Building] != null && Grid.Objects[c, (int)ObjectLayer.Building].GetComponent<Telepad>() != null) continue;
                    SimMessages.ReplaceElement(c, room.Gas, tool, room.Mass, room.Temperature, byte.MaxValue, 0, -1);
                    if (room.Extra != SimHashes.Vacuum && room.ExtraMass > 0)
                        SimMessages.AddRemoveSubstance(c, room.Extra, CellEventLogger.Instance.OxygenModifierSimUpdate, room.ExtraMass, room.Temperature, byte.MaxValue, 0, true, -1);
                }
        }
        private static void FcPool(int dx, int dy, int w, int h, SimHashes liquid, float mass, float temperature)
        {
            var tool = CellEventLogger.Instance.SandBoxTool;
            for (var y = 0; y < h; ++y) for (var x = 0; x < w; ++x)
            {
                var c = FC(dx + x, dy + y);
                if (FcHome(c) && !Grid.Solid[c]) SimMessages.ReplaceElement(c, liquid, tool, mass, temperature, byte.MaxValue, 0, -1);
            }
        }
        // A floor tile under every open cell of a hall's floor row.
        private static int FcFloor(int dx0, int dx1, int dy)
        {
            var tiles = 0; var def = Assets.GetBuildingDef(TileConfig.ID);
            for (var x = dx0; x <= dx1; ++x)
            {
                var c = FC(x, dy);
                if (!FcHome(c) || Grid.Solid[c] || Grid.Objects[c, (int)ObjectLayer.FoundationTile] != null || Grid.IsSubstantialLiquid(c)) continue;
                if (x == 4 || x == 5 || x == 40 || x == 41) continue;           // the ladder shafts
                def.Build(c, Orientation.Neutral, null, new[] { SimHashes.Granite.CreateTag() }, 300, playsound: false); ++tiles;
            }
            return tiles;
        }

        // ---- buildings

        private static bool FcFree(BuildingDef def, int cell)
        {
            foreach (var offset in def.PlacementOffsets)
            {
                var c = Grid.OffsetCell(cell, offset);
                if (!FcHome(c) || Grid.Solid[c] || Grid.Objects[c, (int)def.ObjectLayer] != null) return false;
            }
            return true;
        }
        private static GameObject? FcBuild(string label, string id, int dx, int dy, bool load = true)
        {
            var def = Assets.GetBuildingDef(id); var cell = FC(dx, dy);
            if (!FcFree(def, cell)) { FcNote($"{label}: no room at {fcX + dx},{fcY + dy}"); return null; }
            var building = def.Build(cell, Orientation.Neutral, null, def.DefaultElements(), 300, playsound: false);
            fcMachines[fcPrefix + label] = building; ++fcBuildings;
            Debug.Log($"[FC-BUILT] {label} {id} at {dx},{dy} size {def.WidthInCells}x{def.HeightInCells}");
            if (def.InputConduitType == ConduitType.Gas) fcPorts[FcKind.Gas].Add(Grid.OffsetCell(cell, def.UtilityInputOffset));
            if (def.InputConduitType == ConduitType.Liquid) fcPorts[FcKind.Liquid].Add(Grid.OffsetCell(cell, def.UtilityInputOffset));
            if (def.OutputConduitType == ConduitType.Gas) fcPorts[FcKind.Gas].Add(Grid.OffsetCell(cell, def.UtilityOutputOffset));
            if (def.OutputConduitType == ConduitType.Liquid) fcPorts[FcKind.Liquid].Add(Grid.OffsetCell(cell, def.UtilityOutputOffset));
            if (def.RequiresPowerInput) fcPorts[FcKind.Wire].Add(Grid.OffsetCell(cell, def.PowerInputOffset));
            if (def.RequiresPowerOutput) fcPorts[FcKind.Wire].Add(Grid.OffsetCell(cell, def.PowerOutputOffset));
            if (load && (def.RequiresPowerInput)) fcLoads.Add(building);
            return building;
        }
        // The next free spot on a floor row, walking right (or left) from x.
        private static GameObject? FcRow(string label, string id, ref int x, int dy, int xMax = 42, int gap = 1)
        {
            var def = Assets.GetBuildingDef(id);
            for (; x + def.WidthInCells - 1 <= xMax; ++x)
            {
                if (!FcFree(def, FC(x, dy))) continue;
                var building = FcBuild(label, id, x, dy);
                x += def.WidthInCells + gap;
                return building;
            }
            FcNote($"{label}: row is full near {fcX + x},{fcY + dy}");
            return null;
        }
        private static int FcIn(GameObject b) => Grid.OffsetCell(Grid.PosToCell(b), b.GetComponent<Building>().Def.UtilityInputOffset);
        private static int FcOut(GameObject b) => Grid.OffsetCell(Grid.PosToCell(b), b.GetComponent<Building>().Def.UtilityOutputOffset);
        private static int FcPowerIn(GameObject b) => Grid.OffsetCell(Grid.PosToCell(b), b.GetComponent<Building>().Def.PowerInputOffset);
        private static int FcPowerOut(GameObject b) => Grid.OffsetCell(Grid.PosToCell(b), b.GetComponent<Building>().Def.PowerOutputOffset);

        // ---- networks (pipes and wires)

        private static List<int> FcPath(int from, int to, int mode, int lane)
        {
            Grid.CellToXY(from, out var fx, out var fy); Grid.CellToXY(to, out var tx, out var ty);
            var points = new List<(int x, int y)>();
            if (mode == 0) points.AddRange(new[] { (fx, fy), (tx, fy), (tx, ty) });
            else if (mode == 1) points.AddRange(new[] { (fx, fy), (fx, ty), (tx, ty) });
            else points.AddRange(new[] { (fx, fy), (fx, lane), (tx, lane), (tx, ty) });
            var path = new List<int>();
            for (var k = 0; k + 1 < points.Count; ++k)
            {
                var (ax, ay) = points[k]; var (bx, by) = points[k + 1];
                var sx = Math.Sign(bx - ax); var sy = Math.Sign(by - ay);
                for (var x = ax; x != bx; x += sx) path.Add(Grid.XYToCell(x, ay));
                for (var y = ay; y != by; y += sy) path.Add(Grid.XYToCell(bx, y));
            }
            path.Add(to);
            return path.Where((c, k) => k == 0 || c != path[k - 1]).ToList();
        }
        // Adds a route from one port to another to a net. A route may share
        // cells with its own net only; it never crosses another net of the
        // same kind or another machine's port.
        private static bool FcConnect(FcNet net, int from, int to)
        {
            Grid.CellToXY(from, out _, out var fy); Grid.CellToXY(to, out _, out var ty);
            var lanes = new List<int>();
            for (var d = 0; d < 14; ++d) { lanes.Add(Math.Max(fy, ty) + 1 + d); if (d < 3) lanes.Add(Math.Min(fy, ty) - 1 - d); }
            var tries = new List<(int mode, int lane)> { (0, 0), (1, 0) };
            tries.AddRange(lanes.Select(l => (2, l)));
            foreach (var (mode, lane) in tries)
            {
                var path = FcPath(from, to, mode, lane);
                var ok = path.All(c => FcHome(c) && !Grid.Solid[c] && (!fcUsed[net.Kind].TryGetValue(c, out var owner) || owner == net) &&
                    (net.Cells.Contains(c) || c == from || c == to || !fcPorts[net.Kind].Contains(c)));
                if (!ok) continue;
                net.Paths.Add(path); foreach (var c in path) { net.Cells.Add(c); fcUsed[net.Kind][c] = net; }
                return true;
            }
            FcNote($"net {net.Name}: no clear route {from} -> {to}");
            return false;
        }
        private static FcNet FcNew(string name, FcKind kind) { var net = new FcNet(name, kind); fcNets.Add(net); return net; }
        private static void FcCommit(IUtilityNetworkMgr network, string id, FcKind kind)
        {
            foreach (var net in fcNets.Where(n => n.Kind == kind && n.Paths.Count > 0).ToList())
            {
                Pipe(network, id, net.Paths.ToArray());
                fcBuildings += net.Cells.Count;
            }
        }
        private static void FcCommitAll()
        {
            FcCommit(Game.Instance.gasConduitSystem, GasConduitConfig.ID, FcKind.Gas);
            FcCommit(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, FcKind.Liquid);
            FcCommit(Game.Instance.electricalConduitSystem, WireRefinedConfig.ID, FcKind.Wire);
            foreach (var net in fcNets) net.Paths.Clear();
        }
        // A vent for a machine's output, a few cells along its row (or above it), joined to a net.
        private static GameObject? FcOutlet(string label, GameObject source, FcKind kind, FcNet net, int reach = 3)
        {
            var from = FcOut(source); Grid.CellToXY(from, out var x, out var y);
            var id = kind == FcKind.Gas ? GasVentConfig.ID : LiquidVentConfig.ID;
            foreach (var (step, rise) in new[] { (reach, 0), (reach + 1, 0), (reach + 2, 0), (-reach, 0), (-reach - 1, 0), (reach + 3, 0), (-reach - 2, 0), (reach, 1), (-reach, 1), (reach + 4, 0), (-reach - 3, 0) })
            {
                var vent = FcBuild(label, id, x + step - fcX, y + rise - fcY, false);
                if (vent == null) { fcNotes.RemoveAt(fcNotes.Count - 1); continue; }
                if (FcConnect(net, from, FcIn(vent))) return vent;
                fcNotes.RemoveAt(fcNotes.Count - 1);
                fcMachines.Remove(label); --fcBuildings; Util.KDestroyGameObject(vent);
            }
            FcNote(label + ": no vent position");
            return null;
        }

        // ---- power

        // One Dev generator joins the loads of a group by a bus along `busY`
        // (the group's own span, so the groups never meet).
        private static void FcPower(string name, IEnumerable<GameObject> members, int busDy)
        {
            var list = members.Where(m => m != null).ToList();
            if (list.Count == 0) return;
            var net = FcNew("wire:" + name, FcKind.Wire);
            var ports = list.Select(m => m.GetComponent<Building>().Def.RequiresPowerInput ? FcPowerIn(m) : FcPowerOut(m)).ToList();
            var xs = ports.Select(p => { Grid.CellToXY(p, out var x, out _); return x; }).ToList();
            var busY = fcY + busDy; var minX = xs.Min() - 2; var maxX = xs.Max();
            var dev = Assets.GetBuildingDef(DevGeneratorConfig.ID);
            var devCell = -1; var devX = 0;
            foreach (var candidate in new[] { minX, minX - 1, minX - 3, maxX + 1, maxX + 2, maxX + 4, minX - 5 })
            {
                var cell = Grid.XYToCell(candidate, busY);
                if (FcFree(dev, cell) && !fcPorts[FcKind.Wire].Contains(cell)) { devCell = cell; devX = candidate; break; }
            }
            if (devCell < 0) { FcNote("power " + name + ": no room for its generator"); return; }
            var generator = dev.Build(devCell, Orientation.Neutral, null, dev.DefaultElements(), 300, playsound: false);
            fcMachines[fcPrefix + "devgen:" + name] = generator; ++fcBuildings;
            var start = Grid.OffsetCell(devCell, dev.PowerOutputOffset);
            fcPorts[FcKind.Wire].Add(start);
            var bus = new List<int>();
            for (var x = Math.Min(minX, devX); x <= Math.Max(maxX, devX); ++x) bus.Add(Grid.XYToCell(x, busY));
            if (!bus.Contains(start)) bus.Insert(0, start);
            net.Paths.Add(bus); foreach (var c in bus) { net.Cells.Add(c); fcUsed[FcKind.Wire][c] = net; }
            foreach (var port in ports)
            {
                Grid.CellToXY(port, out var px, out _);
                FcConnect(net, Grid.XYToCell(px, busY), port);
            }
        }

        // ---- the build

        private static string fcPrefix = "";
        private static bool fcCompact;
        private static int fcExtraDuplicants;
        private static void FullColonyBase(int frame, OniNativeColony.Status status)
        {
            FcSite(0, false, 15, status);
            if (Once(420, "check")) { FcReport(status, 17); Finish(); }
        }
        // Spaced Out!: a base on the starting asteroid and a compact one on a second asteroid.
        private static void FullColonySpacedOut(int frame, OniNativeColony.Status status)
        {
            if (!DlcManager.IsExpansion1Active()) throw new InvalidOperationException("This scenario requires Spaced Out!.");
            FcSite(0, false, 11, status);
            FcSite(100, true, 6, status);
            if (Once(520, "check")) { FcReport(status, 19); Finish(); }
        }
        private static void FcSite(int o, bool second, int duplicants, OniNativeColony.Status status)
        {
            if (Once(o + 0, second ? "dig the second asteroid" : "dig"))
            {
                int anchor;
                if (!second)
                {
                    anchor = Grid.PosToCell(GameUtil.GetActiveTelepad());
                    fcNotes.Clear(); fcNets.Clear(); fcMachines.Clear(); fcLoads.Clear(); fcBuildings = fcSolidSkipped = fcSealed = 0; fcDuplicants.Clear();
                    foreach (var kind in fcUsed.Keys.ToList()) { fcUsed[kind].Clear(); fcPorts[kind].Clear(); }
                }
                else
                {
                    var world = ClusterManager.Instance.WorldContainers.First(w => !w.IsStartWorld && w.WorldSize.x >= 120 && w.WorldSize.y >= 120);
                    world.SetDiscovered(false);
                    anchor = Grid.XYToCell(world.WorldOffset.x + 64, world.WorldOffset.y + 55);
                    FcNote($"second asteroid {world.GetProperName()} (world {world.id}) anchored at {world.WorldOffset.x + 64},{world.WorldOffset.y + 55}");
                }
                fcPrefix = second ? "b:" : ""; fcCompact = second; fcExtraDuplicants = duplicants;
                Grid.CellToXY(anchor, out fcX, out fcY); fcWorld = Grid.WorldIdx[anchor];
                fcRooms.Clear(); fcRoomCells.Clear(); fcFilterPump = fcFilter = fcFilterPassVent = fcFilterCo2Vent = null;
                Current.Stage = $"site at {fcX},{fcY}";
                // Three stacked halls plus a fourth above (a 1-thick floor between them), ladder shafts joining them.
                FcCarve(new FcRoom { Name = "hall0", X = -8, Y = 0, W = 52, H = 10, Mass = 1.2f, Temperature = 295f });
                FcCarve(new FcRoom { Name = "hall1", X = -8, Y = 13, W = 52, H = 8, Mass = 1.2f, Temperature = 293f, Extra = SimHashes.CarbonDioxide, ExtraMass = .06f });
                if (!fcCompact) FcCarve(new FcRoom { Name = "hall2", X = -8, Y = 22, W = 52, H = 6, Mass = 1.2f, Temperature = 297f });
                FcCarve(new FcRoom { Name = "hall-1", X = -3, Y = -11, W = 47, H = 10, Mass = 1.2f, Temperature = 295f });
                foreach (var (name, x, y, h) in new[] { ("shaft-0-1", 4, 10, 3), ("shaft-0-1-east", 40, 10, 3), ("shaft-1-2", 4, 21, 1), ("shaft-1-2-east", 40, 21, 1), ("shaft-down", 4, -1, 1), ("shaft-down-east", 40, -1, 1) })
                    if (!fcCompact || y < 20) FcCarve(new FcRoom { Name = name, X = x, Y = y, W = 2, H = h, Mass = 1.2f, Temperature = 295f });
                // Tunnels into the neighbouring biomes (west swamp caves, east caves, a west cave on the upper hall).
                FcCarve(new FcRoom { Name = "tunnel-west", X = -60, Y = 1, W = 52, H = 2, Mass = 1.2f, Temperature = 295f });
                FcCarve(new FcRoom { Name = "tunnel-east", X = 44, Y = 1, W = 12, H = 2, Mass = 1.2f, Temperature = 295f });
                if (!fcCompact)
                {
                    FcCarve(new FcRoom { Name = "tunnel-upper-west", X = -40, Y = 15, W = 32, H = 2, Mass = 1.0f, Temperature = 293f });
                    // Sealed vaults west of the upper halls: hot (geyser, turbine), natural gas (geyser, generator), cold, pools.
                    FcCarve(new FcRoom { Name = "vault-hot", X = -52, Y = 18, W = 22, H = 8, Gas = SimHashes.Oxygen, Mass = .4f, Temperature = 420f }, true);
                    FcCarve(new FcRoom { Name = "vault-gas", X = -52, Y = 29, W = 22, H = 8, Gas = SimHashes.Oxygen, Mass = .4f, Temperature = 300f }, true);
                    FcCarve(new FcRoom { Name = "vault-cold", X = -27, Y = 18, W = 16, H = 8, Gas = SimHashes.CarbonDioxide, Mass = .9f, Temperature = 235f }, true);
                    FcCarve(new FcRoom { Name = "vault-pool", X = -27, Y = 29, W = 16, H = 8, Gas = SimHashes.Oxygen, Mass = .8f, Temperature = 300f }, true);
                    // The cold vault leaks into the upper hall through a one-cell-high tunnel.
                    FcCarve(new FcRoom { Name = "tunnel-cold", X = -11, Y = 19, W = 3, H = 1, Mass = 1.2f, Temperature = 280f });
                }
            }
            if (Once(o + 4, "gas"))
            {
                foreach (var room in fcRooms) FcFillRoom(room);
                if (!fcCompact)
                {
                    FcPool(-26, 29, 7, 3, SimHashes.Water, 900f, 295f);       // pool A of the pool vault (interior x -27..-12, y 29..36)
                    FcPool(-17, 29, 5, 2, SimHashes.Water, 900f, 335f);       // pool B, warm
                }
                Current.Stage = $"filled {fcRooms.Count} rooms, {fcRoomCells.Count} cells; {fcSolidSkipped} hard cells kept, {fcSealed} wall cells sealed";
            }
            if (Once(o + 10, "floors and ladders"))
            {
                var tiles = FcFloor(-8, 43, -1) + FcFloor(-8, 43, 12) + (fcCompact ? 0 : FcFloor(-8, 34, 21)) + FcFloor(-3, 43, -12);
                var ladder = Assets.GetBuildingDef(LadderConfig.ID); var ladders = 0;
                foreach (var (lx, ly0, ly1) in new[] { (4, -11, fcCompact ? 20 : 27), (41, -11, fcCompact ? 20 : 27) })
                    for (var y = ly0; y <= ly1; ++y)
                    {
                        var c = FC(lx, y);
                        if (!FcHome(c) || Grid.Solid[c] || Grid.Objects[c, (int)ladder.ObjectLayer] != null) continue;
                        ladder.Build(c, Orientation.Neutral, null, ladder.DefaultElements(), 300, playsound: false); ++ladders;
                    }
                fcBuildings += tiles + ladders;
                FcNote($"{tiles} floor tiles and {ladders} ladders");
            }
            if (Once(o + 14, "machines")) FcMachinesHalls();
            if (Once(o + 18, "machines of the vaults") && !fcCompact) FcMachinesVaults();
            if (Once(o + 22, "filter ports")) FcFilterNets();
            if (Once(o + 26, "pipes and wires"))
            {
                FcCommitAll();
                FcNote($"{fcMachines.Count} machines, {fcBuildings} buildings and conduit cells so far");
            }
            if (Once(o + 40, "stock")) FcStock();
            if (Once(o + 46, "life")) FcLife();
        }
    }
}
