using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static readonly List<ClusterPortProbe> warpPorts = new List<ClusterPortProbe>();

        private static void SpacedOutWarpPorts(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "spawn generated warp pairs"))
            {
                if (!DlcManager.IsExpansion1Active())
                    throw new InvalidOperationException("Warp control requires actual Spaced Out content.");
                var spawner = SaveGame.Instance.GetComponent<WorldGenSpawner>();
                var generated = spawner.GetSpawnables().Where(s => s.spawnInfo.id == "WarpConduitSender" ||
                    s.spawnInfo.id == "WarpConduitReceiver").ToArray();
                Debug.Log("[Mixed Atmospheres] Generated warp hardware: " + string.Join("; ", generated.Select(s =>
                    $"{s.spawnInfo.id} cell {s.cell}, world {Grid.WorldIdx[s.cell]}, spawned {s.isSpawned}")));
                // Reveal the actual generated objects, just as discovering a
                // linked endpoint does. No synthetic partner or transfer tick.
                spawner.SpawnTag("WarpConduitSender");
                spawner.SpawnTag("WarpConduitReceiver");
                warpPorts.Clear();
            }
            if (frame >= 2 && !recorded.ContainsKey("connect generated warp pairs"))
            {
                var senders = Resources.FindObjectsOfTypeAll<WarpConduitSender>()
                    .Where(s => s.isSpawned).ToArray();
                if (senders.Length < 2 || senders.Any(s => ClusterField(s, "receiver") == null))
                {
                    if (frame < 30) return;
                    var all = Resources.FindObjectsOfTypeAll<WarpConduitSender>().Where(s => s.gameObject.activeInHierarchy).ToArray();
                    var receivers = Resources.FindObjectsOfTypeAll<WarpConduitReceiver>().Where(s => s.gameObject.activeInHierarchy).ToArray();
                    Record("generated warp pairs became available", false, $"spawned senders {senders.Length}; active senders " +
                        string.Join("; ", all.Select(s => $"cell {Grid.PosToCell(s.gameObject)}, spawned {s.isSpawned}, paired {ClusterField(s, "receiver") != null}")) +
                        "; active receivers " + string.Join("; ", receivers.Select(s => $"cell {Grid.PosToCell(s.gameObject)}, spawned {s.isSpawned}")));
                    Finish(); return;
                }
                var endpoints = senders.Cast<Component>().Concat(senders.Select(s =>
                    (Component)ClusterField(s, "receiver"))).Distinct().ToArray();
                foreach (var endpoint in endpoints)
                {
                    var activation = endpoint.GetComponent<Activatable>();
                    if (activation == null)
                        throw new InvalidOperationException("Generated warp endpoint has no activation work.");
                    if (!activation.IsActivated)
                        AccessTools.Method(typeof(Workable), "CompleteWork", new[] { typeof(WorkerBase) })
                            .Invoke(activation, new object?[] { null });
                }
                foreach (var sender in senders)
                {
                    var receiver = (WarpConduitReceiver)ClusterField(sender, "receiver");
                    foreach (var gas in new[] { true, false })
                    {
                        var portName = gas ? "gasPort" : "liquidPort";
                        var input = (int)ClusterField(ClusterField(sender, portName), "inputCell");
                        var output = (int)ClusterField(ClusterField(receiver, portName), "outputCell");
                        var storage = (Storage)ClusterField(sender, gas ? "gasStorage" : "liquidStorage");
                        if (!Grid.IsValidCell(input) || !Grid.IsValidCell(output) ||
                            Grid.WorldIdx[input] == Grid.WorldIdx[output] ||
                            !ReferenceEquals(storage, ClusterField(receiver, gas ? "senderGasStorage" : "senderLiquidStorage")))
                            throw new InvalidOperationException("Generated warp storage does not link two distinct worlds.");
                        var flow = gas ? Game.Instance.gasConduitFlow : Game.Instance.liquidConduitFlow;
                        var network = gas ? Game.Instance.gasConduitSystem : Game.Instance.liquidConduitSystem;
                        Pipe(network, gas ? GasConduitConfig.ID : LiquidConduitConfig.ID,
                            new List<int> { input }, new List<int> { output });
                        warpPorts.Add(new ClusterPortProbe($"warp {(gas ? "gas" : "liquid")} {Grid.WorldIdx[input]}->{Grid.WorldIdx[output]}",
                            flow, input, output, storage));
                    }
                }
                Record("generated warp pairs cover both directions", senders.Select(s => Grid.WorldIdx[Grid.PosToCell(s.gameObject)])
                    .Distinct().Count() >= 2, $"{senders.Length} senders, {warpPorts.Count} gas/liquid routes");
                recorded["connect generated warp pairs"] = frame;
            }
            if (recorded.TryGetValue("connect generated warp pairs", out var connected) &&
                frame >= connected + 4 && !recorded.ContainsKey("seed generated warp pairs"))
            {
                if (warpPorts.Any(p => !p.Flow.HasConduit(p.Input) || !p.Flow.HasConduit(p.Output)))
                {
                    if (frame < connected + 30) return;
                    Record("warp endpoint pipes became ready", false, "actual endpoint conduit missing");
                    Finish(); return;
                }
                foreach (var probe in warpPorts)
                {
                    if (ColonyConduits.ReadSynced(probe.Flow, probe.Input).Mass != 0 ||
                        ColonyConduits.ReadSynced(probe.Flow, probe.Output).Mass != 0 || probe.Storage.MassStored() != 0)
                        throw new InvalidOperationException("Warp fixture route is not empty: " + probe.Name);
                    var gas = probe.Flow.conduitType == ConduitType.Gas;
                    var a = ColonyConduits.Seed(probe.Flow, probe.Input,
                        gas ? SimHashes.Oxygen : SimHashes.Water, .75f, 300, byte.MaxValue, 0);
                    var b = ColonyConduits.Seed(probe.Flow, probe.Input,
                        gas ? SimHashes.CarbonDioxide : SimHashes.DirtyWater, .25f, 300, byte.MaxValue, 0);
                    if (a != .75f || b != .25f)
                        throw new InvalidOperationException("Warp fixture source was refused: " + probe.Name);
                }
                recorded["seed generated warp pairs"] = frame;
            }
            if (recorded.TryGetValue("seed generated warp pairs", out var seeded) && frame >= seeded + 16)
            {
                foreach (var probe in warpPorts)
                {
                    var gas = probe.Flow.conduitType == ConduitType.Gas;
                    var output = ColonyConduits.ReadSynced(probe.Flow, probe.Output);
                    var first = ClusterPipeMass(output, gas ? SimHashes.Oxygen : SimHashes.Water);
                    var second = ClusterPipeMass(output, gas ? SimHashes.CarbonDioxide : SimHashes.DirtyWater);
                    var input = ColonyConduits.ReadSynced(probe.Flow, probe.Input).Mass;
                    Record(probe.Name + " moves both constituents", Math.Abs(first - .75) < 1e-6 && Math.Abs(second - .25) < 1e-6,
                        $"first {first:R} kg, second {second:R} kg");
                    Record(probe.Name + " leaves no duplicate inventory", input < 1e-6 && probe.Storage.MassStored() < 1e-6,
                        $"input {input:R} kg, shared storage {probe.Storage.MassStored():R} kg");
                }
                Record("warp transfer keeps closed world ledgers", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 &&
                    status.MaximumRelativeEnergyError <= 1e-12, $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
                Record("warp pipe and storage boundary accounts close", ColonyConduits.Audit(out var detail), detail);
                Finish();
            }
        }
    }
}
