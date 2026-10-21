using System;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Element consumers on the owned asteroid (docs/colony-owner.md#element-consumers).
    // ONI's native consumer sees one element per cell (the owner's projection),
    // so a consumer of carbon dioxide in a mixed room would find none. The
    // owner runs every consumer instead: ONI still registers it natively (for
    // its handle) but its rate messages stop here, leaving the native consumer
    // at rate zero. Each positive frame queues the consumption as an owner
    // extraction by element, or of the whole gas or liquid mixture for an
    // any-gas or any-liquid consumer (pumps), and the receipts return as ONI's
    // consumed-mass events for the consumer's handle.
    internal static unsafe class ColonyConsumers
    {
        private const int SetElementConsumerData = 1575539738, RemoveElementConsumer = 894417742;
        private sealed class Consumer { public int Cell; public float RateKgPerSecond; public uint Kind, NativeIndex, Radius; }
        private static readonly Dictionary<int, Consumer> consumers = new Dictionary<int, Consumer>();
        private static readonly System.Reflection.FieldInfo? handles = AccessTools.Field(typeof(ElementConsumer), "handleInstanceMap");
        internal static int Count => consumers.Count;
        internal static long Queued { get; private set; }
        internal static long Delivered { get; private set; }
        internal static void Reset() { consumers.Clear(); Queued = Delivered = 0; }

        // SetElementConsumerData (12 bytes: handle, cell, kg/s) and
        // RemoveElementConsumer (8 bytes: handle, ...). Returns true when the
        // whole batch is the owner's.
        internal static bool TryHandle(int message, int length, int count, byte* payload, Func<int, bool> inRect)
        {
            if (message == RemoveElementConsumer && length == 8)
            {
                for (var k = 0; k < count; ++k) consumers.Remove(BinaryPrimitives.ReadInt32LittleEndian(new ReadOnlySpan<byte>(payload + k * length, 4)));
                return false; // ONI removes its inactive native registration.
            }
            if (message != SetElementConsumerData || length != 12) return false;
            var map = handles?.GetValue(null) as IDictionary;
            var updates = new List<(int handle, Consumer consumer)>();
            for (var k = 0; k < count; ++k)
            {
                var d = new ReadOnlySpan<byte>(payload + k * length, length);
                var handle = BinaryPrimitives.ReadInt32LittleEndian(d);
                var cell = BinaryPrimitives.ReadInt32LittleEndian(d.Slice(4));
                if (!inRect(cell) || map == null || !map.Contains(handle) || !(map[handle] is ElementConsumer source)) return false;
                var kind = source.configuration switch
                {
                    ElementConsumer.Configuration.AllGas => OniNativeColony.ExtractGas,
                    ElementConsumer.Configuration.AllLiquid => OniNativeColony.ExtractLiquid,
                    _ => OniNativeColony.Extract
                };
                updates.Add((handle, new Consumer { Cell = cell, RateKgPerSecond = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(d.Slice(8))),
                    Kind = kind | OniNativeColony.ConsumerFlag, Radius = source.consumptionRadius,
                    NativeIndex = kind == OniNativeColony.Extract ? (uint)ElementLoader.FindElementByHash(source.elementToConsume).idx : 0 }));
            }
            foreach (var (handle, consumer) in updates) consumers[handle] = consumer;
            return true;
        }

        // At each positive native frame: this frame's consumption, committed at
        // the owner's next step.
        internal static void QueueFrame(float seconds)
        {
            if (!(seconds > 0) || consumers.Count == 0) return;
            var list = new List<OniNativeColony.Transfer>(consumers.Count);
            foreach (var entry in consumers)
            {
                var c = entry.Value; if (!(c.RateKgPerSecond > 0)) continue;
                list.Add(new OniNativeColony.Transfer { PublicCell = (uint)c.Cell, NativeIndex = c.NativeIndex, Kind = c.Kind, Radius = c.Radius,
                    Callback = entry.Key, MassKg = c.RateKgPerSecond * seconds });
            }
            if (list.Count == 0) return;
            OniNativeColony.Check(OniNativeColony.QueueTransfers(list.ToArray(), (uint)list.Count));
            Queued += list.Count;
        }

        internal static bool IsConsumerReceipt(in OniNativeColony.Receipt receipt) => (receipt.Kind & OniNativeColony.ConsumerFlag) != 0;
        internal static Sim.ConsumedMassInfo Event(in OniNativeColony.Receipt receipt)
        {
            ++Delivered;
            return new Sim.ConsumedMassInfo { simHandle = receipt.Callback, removedElemIdx = (ushort)receipt.NativeIndex, mass = receipt.MassKg,
                temperature = receipt.MassKg > 0 ? receipt.TemperatureK : 0, diseaseIdx = OniNativeColony.DiseaseIdx(receipt.Disease), diseaseCount = receipt.DiseaseCount };
        }
    }
}
