using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Duplicant hazards on the owned asteroid read the owner's cells through
    // the game's own monitors (docs/colony-owner.md#scenarios).
    internal static partial class ColonyScenarios
    {
        private static readonly List<(MinionIdentity minion, Vector3 home)> visitors = new List<(MinionIdentity, Vector3)>();
        private static void Teleport(MinionIdentity minion, int cell)
        {
            var nav = minion.GetComponent<Navigator>();
            nav?.Stop();
            minion.transform.SetPosition(Grid.CellToPosCBC(cell, Grid.SceneLayer.Move));
            Singleton<CellChangeMonitor>.Instance.MarkDirty(minion.transform);
            if (nav != null) { nav.SetCurrentNavType(NavType.Floor); nav.UpdateProbe(true); }
        }
        private static MinionIdentity Visit(int index, int cell)
        {
            var minion = Components.LiveMinionIdentities.Items[index % Components.LiveMinionIdentities.Items.Count];
            visitors.Add((minion, minion.transform.GetPosition()));
            Teleport(minion, cell);
            return minion;
        }
        // Visitors go home healed and breathing.
        private static void SendVisitorsHome()
        {
            foreach (var (minion, home) in visitors)
            {
                if (minion == null) continue;
                Teleport(minion, Grid.PosToCell(home));
                Db.Get().Amounts.Breath.Lookup(minion.gameObject)?.SetValue(100);
                var health = minion.GetComponent<Health>(); if (health != null) health.hitPoints = health.maxHitPoints;
            }
            visitors.Clear();
        }
        private static bool HasEffect(MinionIdentity minion, string id) => minion.GetComponent<Klei.AI.Effects>()?.HasEffect(id) == true;

        // Three sealed rooms: hot air, air at high pressure, and water with no
        // gas. A duplicant in each meets the game's scalding, eardrum and
        // breath monitors through the owner's cells.
        private static void Hazards(int frame)
        {
            if (Once(0, "rooms"))
                Arena((x, y) => x == 2 || x == 5 || y == Height - 1 ? (SimHashes.Unobtanium, 20000f, 300f) :
                    y == 0 ? (SimHashes.Granite, 1000f, x < 2 ? 430f : 300f) :
                    x < 2 ? (SimHashes.Oxygen, 1.2f, 430f) : x < 5 ? (SimHashes.Oxygen, 6f, 300f) : (SimHashes.Water, 990f, 300f));
            if (Once(8, "visit"))
            {
                if (Components.LiveMinionIdentities.Items.Count == 0) { Record("duplicants to visit", false, "none alive"); Finish(); return; }
                var hot = Visit(0, Cell(0, 1)); var dense = Visit(1, Cell(3, 1)); var wet = Visit(2, Cell(6, 1));
                recorded["hot-health"] = hot.GetComponent<Health>().hitPoints;
                Db.Get().Amounts.Breath.Lookup(wet.gameObject).SetValue(100);
            }
            if (Once(48, "check"))
            {
                var hot = visitors[0].minion; var dense = visitors[1].minion; var wet = visitors[2].minion;
                var hotCell = Grid.PosToCell(hot);
                var health = hot.GetComponent<Health>().hitPoints;
                Record("hot air scalds", health < recorded["hot-health"] || HasEffect(hot, "Scalding"),
                    $"health {recorded["hot-health"]:F1} -> {health:F1}; cell {Grid.Element[hotCell].id} {Grid.Temperature[hotCell]:F1} K");
                var denseCell = Grid.PosToCell(dense);
                Record("dense air pops eardrums", HasEffect(dense, "PoppedEarDrums"),
                    $"cell {Grid.Element[denseCell].id} {Grid.Mass[denseCell]:G4} kg, {Grid.Pressure[denseCell]:G4} kPa; effects {string.Join(",", dense.GetComponent<Klei.AI.Effects>().GetTimeLimitedEffects().Select(e => e.effect.Id))}");
                var breath = Db.Get().Amounts.Breath.Lookup(wet.gameObject).value; var wetCell = Grid.PosToCell(wet);
                Record("a submerged duplicant holds its breath", breath < 95,
                    $"breath {breath:F1}; cell {Grid.Element[wetCell].id} {Grid.Mass[wetCell]:G4} kg");
                SendVisitorsHome();
                Finish();
            }
        }

        // In carbon dioxide a suited duplicant breathes its suit's oxygen and an
        // unsuited one holds its breath (the owner's air fails easy mode).
        private static GameObject? suit;
        private static void Suit(int frame)
        {
            if (Once(0, "room")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : y == Height - 1 ? (SimHashes.Unobtanium, 20000f, 300f) :
                (SimHashes.CarbonDioxide, 1.5f, 300f));
            if (Once(8, "visit"))
            {
                if (Components.LiveMinionIdentities.Items.Count < 2) { Record("two duplicants to visit", false, $"{Components.LiveMinionIdentities.Items.Count} alive"); Finish(); return; }
                var suited = Visit(0, Cell(1, 1)); var bare = Visit(1, Cell(6, 1));
                suit = Util.KInstantiate(Assets.GetPrefab(AtmoSuitConfig.ID), Grid.CellToPosCBC(Cell(1, 1), Grid.SceneLayer.Ore)); suit.SetActive(true);
                var tank = suit.GetComponent<SuitTank>();
                tank.storage.AddGasChunk(SimHashes.Oxygen, tank.capacity, 300, byte.MaxValue, 0, false, true);
                var equippable = suit.GetComponent<Equippable>();
                equippable.Assign(suited.assignableProxy.Get());
                suited.GetEquipment().Equip(equippable);
                foreach (var m in new[] { suited, bare }) Db.Get().Amounts.Breath.Lookup(m.gameObject).SetValue(100);
                recorded["tank"] = tank.storage.GetMassAvailable(SimHashes.Oxygen);
            }
            if (Once(48, "check"))
            {
                var suited = visitors[0].minion; var bare = visitors[1].minion;
                var breathSuited = Db.Get().Amounts.Breath.Lookup(suited.gameObject).value; var breathBare = Db.Get().Amounts.Breath.Lookup(bare.gameObject).value;
                var tank = suit != null ? suit.GetComponent<SuitTank>().storage.GetMassAvailable(SimHashes.Oxygen) : 0;
                Record("a suited duplicant breathes its suit's oxygen", breathSuited > 95 && tank < recorded["tank"],
                    $"breath {breathSuited:F1}, suit oxygen {recorded["tank"]:G4} -> {tank:G4} kg");
                var cell = Grid.PosToCell(bare);
                Record("carbon dioxide is not breathable", breathBare < 95 && !(ColonyOwner.TryReadBreathingCell(cell, out var offered, out var element) && element != SimHashes.Vacuum),
                    $"breath {breathBare:F1}; cell {Grid.Element[cell].id} {Grid.Mass[cell]:G4} kg");
                var why = ColonyBreathingStatus.Explain("", bare.gameObject);
                Record("the duplicant is told why the air fails", why.Length > 0, why.Trim());
                if (suit != null && suited != null) suited.GetEquipment().Unequip(suit.GetComponent<Equippable>());
                SendVisitorsHome();
                Finish();
            }
        }

        // A duplicant breathing polluted oxygen carrying slimelung is exposed.
        private static void GermExposure(int frame)
        {
            var slime = Db.Get().Diseases.SlimeGerms;
            if (Once(0, "room")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : y == Height - 1 ? (SimHashes.Unobtanium, 20000f, 300f) :
                (x % 2 == 0 ? SimHashes.Oxygen : SimHashes.ContaminatedOxygen, 1.4f, 300f));
            if (Once(6, "seed"))
                for (var y = 1; y < Height - 1; ++y) for (var x = 0; x < Width; ++x)
                    SimMessages.ModifyDiseaseOnCell(Cell(x, y), DiseaseIndex(slime), 50000);
            if (Once(10, "visit"))
            {
                if (Components.LiveMinionIdentities.Items.Count == 0) { Record("a duplicant to visit", false, "none alive"); Finish(); return; }
                Visit(0, Cell(3, 1));
            }
            if (Once(70, "check"))
            {
                var minion = visitors[0].minion;
                var monitor = minion.GetSMI<GermExposureMonitor.Instance>();
                var state = monitor?.GetExposureState(slime.Id).ToString() ?? "no monitor";
                var cell = Grid.PosToCell(minion);
                Record("breathing germy air exposes the duplicant", monitor != null && monitor.GetExposureState(slime.Id) != GermExposureMonitor.ExposureState.None,
                    $"exposure {state}; cell {Grid.Element[cell].id} germs {Grid.DiseaseCount[cell]} ({ColonySensors.Germs(cell)} all identities)");
                SendVisitorsHome();
                Finish();
            }
        }
    }
}
