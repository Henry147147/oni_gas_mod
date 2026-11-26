using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Rephysicalized's own gas and pipe components on the owner's mixtures
    // (docs/colony-owner.md#other-mods-rephysicalized). Needs Rephysicalized
    // loaded (tools/oni_headless.py --extra-mod-first FOLDER); without it the
    // scenario fails at once. Four sealed rooms side by side, 27 x 6, a
    // granite floor:
    //   A (columns 0-5)   a Coal Generator with 200 kg of coal in air where
    //                     carbon dioxide outweighs oxygen in every cell
    //                     (0.6 kg oxygen + 1.4 kg carbon dioxide per cell);
    //   B (7-12)          the same generator in thin air (0.02 kg oxygen +
    //                     0.012 kg carbon dioxide per cell, less than its
    //                     0.04 kg ask per tick);
    //   C (14-17)         a wild Mealwood in the air of room A;
    //   D (19-26)         a Liquid Reservoir with 50 kg of petroleum piped to
    //                     a Polymerizer, whose intake is Rephysicalized's own
    //                     RephysicalizedConduitConsumer. The press runs as a
    //                     player's does: a dev generator wired to its power
    //                     input and its gas output piped to a vent. Its
    //                     intake takes only while the press meets its
    //                     operating requirements (power, output connected).
    // ONI's own simulation (--mode colony-vanilla) holds one element per
    // cell, so there rooms A and C are layered (carbon dioxide below, oxygen
    // above) and room B holds oxygen only: that run shows Rephysicalized's
    // own behaviour as the reference. At frame 56 the generators' intakes are
    // switched off so that every request has its receipt by the checks.
    // MA_OTHER_MOD_COMPAT=off leaves Rephysicalized's intake unadapted (a
    // control run): its first check then fails, and the numbers show what
    // the original intake does on the owner.
    internal static partial class ColonyScenarios
    {
        private static GameObject? rpBurner, rpThin, rpCrop, rpReservoir, rpPress, rpVent;
        // Rephysicalized's polymerizer recipe (PolymerizerConfig_ConfigureBuildingTemplate_Patch):
        // 0.8 kg/s of plastifiable liquid -> 0.5 kg/s polypropylene, 0.15 steam, 0.15 carbon dioxide.
        private const double RpPetroleumPerPlastic = 0.8 / 0.5;
        private static List<int> rpPipe = new List<int>();
        private static int rpActiveA, rpActiveB;
        private const int RpWidth = 27;
        private static (int from, int to) RpRoom(char room) => room switch { 'A' => (0, 5), 'B' => (7, 12), 'C' => (14, 17), _ => (19, 26) };
        private static (SimHashes, float, float) RephysicalizedCell(int x, int y)
        {
            if (y == 0) return (SimHashes.Granite, 1000f, 300f);
            if (x == 6 || x == 13 || x == 18) return (SimHashes.Unobtanium, 20000f, 300f);
            if (x >= 19) return (SimHashes.Oxygen, .5f, 300f);
            if (x >= 7 && x <= 12) return (SimHashes.Oxygen, .02f, 300f);
            // Rooms A and C: the owner adds carbon dioxide to every cell (RephysicalizedMix); ONI's simulation gets layers.
            if (Vanilla) return y <= 2 ? (SimHashes.CarbonDioxide, 2f, 300f) : (SimHashes.Oxygen, 1f, 300f);
            return (SimHashes.Oxygen, .6f, 300f);
        }
        private static double RpSpecies(char room, SimHashes element)
        {
            var (from, to) = RpRoom(room); double total = 0;
            for (var x = from; x <= to; ++x) for (var y = 1; y < Height; ++y) total += SpeciesMass(Cell(x, y), element);
            return total;
        }
        private static double RpOxidizer(char room) => RpSpecies(room, SimHashes.Oxygen) + RpSpecies(room, SimHashes.ContaminatedOxygen);
        private static double RpHeld(GameObject? building) => building == null ? 0 :
            building.GetComponents<Storage>().Sum(s => (double)s.GetMassAvailable(SimHashes.Oxygen) + s.GetMassAvailable(SimHashes.ContaminatedOxygen));
        private static Component? RpIntake(GameObject? building) =>
            building == null || RephysicalizedCompat.ConsumerType == null ? null : building.GetComponent(RephysicalizedCompat.ConsumerType);
        private static string RpLedger(GameObject? building)
        {
            var ledger = RephysicalizedCompat.LedgerOf(RpIntake(building));
            return ledger == null ? "no owner-aware requests" :
                $"requested {ledger.Requested:G6} kg, credited {ledger.Credited:G6} kg, returned {ledger.Returned:G4} kg, in flight {ledger.InFlight:G4} kg ({ledger.Pending} pending, {ledger.Expired} expired)";
        }
        // Debris of one element lying in a room.
        private static double RpDebris(SimHashes element, char room)
        {
            var (from, to) = RpRoom(room); double total = 0;
            foreach (var item in Components.Pickupables.Items)
            {
                if (item == null || item.GetComponent<PrimaryElement>() is not PrimaryElement e || e.ElementID != element) continue;
                Grid.CellToXY(Grid.PosToCell(item), out var x, out var y);
                if (x >= x0 + from && x <= x0 + to && y >= y0 && y < y0 + Height) total += e.Mass;
            }
            return total;
        }
        private static GameObject RpBurner(int x)
        {
            var burner = Build(GeneratorConfig.ID, Cell(x, 1)); Store(burner, SimHashes.Carbon, 200);
            Wire(new List<int> { Grid.OffsetCell(Grid.PosToCell(burner), burner.GetComponent<Building>().Def.PowerOutputOffset) });
            return burner;
        }
        private static void Rephysicalized(int frame)
        {
            if (Once(0, "arena"))
            {
                rpBurner = rpThin = rpCrop = rpReservoir = rpPress = rpVent = null; rpPipe = new List<int>(); rpActiveA = rpActiveB = 0;
                if (RephysicalizedCompat.FindType(RephysicalizedCompat.ConsumerTypeName) == null)
                {
                    Record("Rephysicalized is loaded", false, "Rephysicalized is not loaded: run with --extra-mod-first local/other-mods/src/Rephysicalized " +
                        "(python3 tools/workshop.py fetch-mod 3609531831 puts Workshop item 3609531831 there)");
                    Finish(); return;
                }
                pendingShift = RpWidth - Width;
                Arena(RephysicalizedCell, RpWidth, Height);
            }
            if (!Vanilla && Once(4, "mix"))
                for (var y = 1; y < Height; ++y) for (var x = 0; x < RpWidth; ++x)
                {
                    var added = x <= 5 || (x >= 14 && x <= 17) ? 1.4f : x >= 7 && x <= 12 ? .012f : 0f;
                    if (added > 0) SimMessages.AddRemoveSubstance(Cell(x, y), SimHashes.CarbonDioxide, CellEventLogger.Instance.OxygenModifierSimUpdate, added, 300, byte.MaxValue, 0);
                }
            if (Once(8, "build"))
            {
                // Carbon dioxide outweighs oxygen in every gas cell of rooms A and C (the owner's mixture).
                var outweighed = 0; var cells = 0;
                foreach (var room in new[] { 'A', 'C' })
                {
                    var (from, to) = RpRoom(room);
                    for (var x = from; x <= to; ++x) for (var y = 1; y < Height; ++y)
                    { ++cells; if (SpeciesMass(Cell(x, y), SimHashes.CarbonDioxide) > SpeciesMass(Cell(x, y), SimHashes.Oxygen)) ++outweighed; }
                }
                recorded["outweighed"] = outweighed; recorded["mixed cells"] = cells;
                recorded["A oxygen"] = RpOxidizer('A'); recorded["B oxygen"] = RpOxidizer('B');
                recorded["B shown"] = Grid.Mass[Cell(11, 4)]; recorded["B cell oxygen"] = SpeciesMass(Cell(11, 4), SimHashes.Oxygen);
                rpBurner = RpBurner(2); rpThin = RpBurner(9);
                recorded["A coal"] = Stored(rpBurner, SimHashes.Carbon); recorded["B coal"] = Stored(rpThin, SimHashes.Carbon);
                rpCrop = GameUtil.KInstantiate(Assets.GetPrefab(BasicSingleHarvestPlantConfig.ID), Grid.CellToPosCBC(Cell(15, 1), Grid.SceneLayer.BuildingFront), Grid.SceneLayer.BuildingFront);
                rpCrop.SetActive(true);
                rpReservoir = Build(LiquidReservoirConfig.ID, Cell(20, 1));
                // The press with its own dev generator in its room (Build's would stand in room B), and a vent for its exhaust.
                var pressDef = Assets.GetBuildingDef(PolymerizerConfig.ID);
                rpPress = pressDef.Build(Cell(24, 1), Orientation.Neutral, null, pressDef.DefaultElements(), 300, playsound: false);
                var dev = Assets.GetBuildingDef(DevGeneratorConfig.ID);
                var source = dev.Build(Cell(19, 5), Orientation.Neutral, null, dev.DefaultElements(), 300, playsound: false);
                Wire(Route(Grid.OffsetCell(Grid.PosToCell(source), dev.PowerOutputOffset), Grid.OffsetCell(Grid.PosToCell(rpPress), pressDef.PowerInputOffset)));
                rpVent = Build(GasVentConfig.ID, Cell(26, 4));
            }
            if (Once(10, "pipe"))
            {
                rpPipe = Route(rpReservoir!.GetComponent<Building>().GetUtilityOutputCell(), rpPress!.GetComponent<Building>().GetUtilityInputCell());
                Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, rpPipe);
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, Route(rpPress.GetComponent<Building>().GetUtilityOutputCell(),
                    Grid.OffsetCell(Grid.PosToCell(rpVent), rpVent!.GetComponent<Building>().Def.UtilityInputOffset)));
            }
            if (Once(12, "petroleum")) { Store(rpReservoir!, SimHashes.Petroleum, 50); recorded["D petroleum"] = Stored(rpReservoir, SimHashes.Petroleum); }
            if (frame > 8 && frame < 56)
            {
                if (rpBurner != null && rpBurner.GetComponent<Operational>()?.IsActive == true) ++rpActiveA;
                if (rpThin != null && rpThin.GetComponent<Operational>()?.IsActive == true) ++rpActiveB;
            }
            if (Once(56, "intakes off"))
                foreach (var burner in new[] { rpBurner, rpThin })
                    if (RpIntake(burner) is Component intake) RephysicalizedCompat.SetRequired(intake, false);
            if (Once(64, "check"))
            {
                var compat = Vanilla ? "ONI's own simulation: Rephysicalized's intake runs unchanged" :
                    RephysicalizedCompat.Installed ? "owner-aware intake installed" + (RephysicalizedCompat.SuffocationInstalled ? ", critter oxidizer search too" : "") :
                    "not installed: " + RephysicalizedCompat.Problem;
                if (RephysicalizedCompat.SwitchedOff) compat += " [CONTROL RUN: MA_OTHER_MOD_COMPAT=off]";
                if (!Vanilla) Record("Rephysicalized's oxygen intake is adapted to the owner", RephysicalizedCompat.Installed, compat);
                if (!Vanilla) Record("carbon dioxide outweighs oxygen in every cell of the mixed rooms", recorded["outweighed"] == recorded["mixed cells"],
                    $"{recorded["outweighed"]} of {recorded["mixed cells"]} cells at the start");

                // A: the burner in mixed air.
                var heldA = RpHeld(rpBurner); var lostA = recorded["A oxygen"] - RpOxidizer('A'); var burnedA = recorded["A coal"] - Stored(rpBurner, SimHashes.Carbon);
                Record("the burner's oxidizer storage filled", heldA > .05, $"holds {heldA:G4} kg oxidizer; room lost {lostA:G6} kg; {RpLedger(rpBurner)}; {compat}");
                Record("the burner ran", rpActiveA > 0 && burnedA > 0, $"active in {rpActiveA} of 47 frames, coal burned {burnedA:G4} kg; {MachineState(rpBurner)}");
                var ledgerA = RephysicalizedCompat.LedgerOf(RpIntake(rpBurner));
                if (!Vanilla) Record("the oxygen credited to the burner is the oxygen its room lost", ledgerA != null && ledgerA.Pending == 0 &&
                    Math.Abs(ledgerA.Credited - lostA) <= 1e-5 * recorded["A oxygen"] + 1e-6,
                    $"room {recorded["A oxygen"]:G9} -> {RpOxidizer('A'):G9} kg (lost {lostA:G9}); credited {ledgerA?.Credited:G9} kg; stored now {heldA:G6} kg, so {(ledgerA?.Credited ?? 0) - heldA:G6} kg burned");

                // B: thin air; nothing may be stored that the room did not lose.
                var heldB = RpHeld(rpThin); var lostB = recorded["B oxygen"] - RpOxidizer('B'); var ledgerB = RephysicalizedCompat.LedgerOf(RpIntake(rpThin));
                Record("in thin air the burner stores no oxygen its room did not lose", lostB > 0 && heldB <= lostB + 1e-6 && (Vanilla || ledgerB != null && ledgerB.Credited <= lostB + 1e-6 * recorded["B oxygen"] + 1e-7),
                    $"a cell showed {recorded["B shown"]:G4} kg of gas holding {recorded["B cell oxygen"]:G4} kg oxygen; room {recorded["B oxygen"]:G6} -> {RpOxidizer('B'):G6} kg (lost {lostB:G6}); " +
                    $"stored {heldB:G6} kg; {RpLedger(rpThin)}; active in {rpActiveB} frames");

                // C: the crop.
                var gasStorage = rpCrop == null ? 0 : RpHeld(rpCrop);
                object? starved = null, consumer = null;
                var consumerType = RephysicalizedCompat.FindType("Rephysicalized.Content.Plant_patches.PlantGasConsumer");
                if (rpCrop != null && consumerType != null && rpCrop.GetComponent(consumerType) is Component found) { consumer = found; starved = RephysicalizedCompat.Property(found, "Starved"); }
                var wilting = rpCrop != null && rpCrop.GetComponent<WiltCondition>() is WiltCondition wilt && wilt.IsWilting();
                Record("the crop's gas buffer filled and it does not starve", consumer != null && gasStorage > .02 && starved is bool hungry && !hungry,
                    $"buffer {gasStorage:G4} kg, starved {starved?.ToString() ?? "unknown"}, wilting {wilting}; {RpLedger(rpCrop)}");

                // D: the polymerizer's own pipe intake. What left the reservoir and is no longer in the pipe
                // reached the press: it holds it, or converted it (its polypropylene, stored or dropped as debris
                // in room D, at the recipe's 1.6 kg petroleum per kg).
                double piped = 0;
                foreach (var cell in rpPipe) piped += Game.Instance.liquidConduitFlow.GetContents(cell).mass;
                var left = Stored(rpReservoir, SimHashes.Petroleum); var taken = recorded.TryGetValue("D petroleum", out var start) ? start - left - piped : 0;
                var pressHeld = Stored(rpPress, SimHashes.Petroleum);
                var plastic = Stored(rpPress, SimHashes.Polypropylene) + RpDebris(SimHashes.Polypropylene, 'D');
                var converted = plastic * RpPetroleumPerPlastic;
                Record("the polymerizer's intake took piped petroleum", taken > .1,
                    $"reservoir {start:G6} -> {left:G6} kg, {piped:G4} kg in the {rpPipe.Count}-segment pipe, so {taken:G6} kg reached the press; {MachineState(rpPress)}");
                Record("what the press took is in it or converted (within 0.01 kg + 0.5 %)", Math.Abs(pressHeld + converted - taken) <= .01 + .005 * taken,
                    $"the press holds {pressHeld:G6} kg petroleum and made {plastic:G6} kg polypropylene ({converted:G6} kg petroleum); {pressHeld + converted:G6} kg against {taken:G6} kg taken");
                if (!Vanilla) LedgersClosed("rephysicalized");
                Finish();
            }
        }
    }
}
