using System;
using System.Collections;
using System.Collections.Generic;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
namespace MixedAtmospheres.Oni
{
    internal static class ColonyCompactReadings
    {
        internal static readonly bool Enabled = MixturePresentationRuntime.CompactTexturesEnabled;
        internal const int MaximumCompactConstituents = 256;
        private static long hits, misses, rawSamples, cacheSlots, fitBatches, fallbackBatches;
        internal static (long Hits, long Misses, long Raw) Counts => (System.Threading.Interlocked.Read(ref hits), System.Threading.Interlocked.Read(ref misses), System.Threading.Interlocked.Read(ref rawSamples));
        private static int species;
        internal static object Report() => new
        {
            Enabled,
            RangeAvailable = MixtureWorldRendering.CompactRangeAvailable,
            Paired = MixtureWorldRendering.CompactProofReport(),
            TableSpecies = species,
            FitBatches = System.Threading.Interlocked.Read(ref fitBatches),
            FallbackBatches = System.Threading.Interlocked.Read(ref fallbackBatches),
            CacheHits = System.Threading.Interlocked.Read(ref hits),
            CacheMisses = System.Threading.Interlocked.Read(ref misses),
            RawSamples = System.Threading.Interlocked.Read(ref rawSamples),
            CumulativeAllocatedSlots = System.Threading.Interlocked.Read(ref cacheSlots),
            LivePooledSlots = ColonyReadingPool.CompactSlots,
            MaximumPooledSlots = 48 * 4093,
            MaximumScratchConstituents = MaximumCompactConstituents
        };

        internal static void ResetDiagnostics()
        {
            System.Threading.Interlocked.Exchange(ref hits, 0);
            System.Threading.Interlocked.Exchange(ref misses, 0);
            System.Threading.Interlocked.Exchange(ref rawSamples, 0);
            System.Threading.Interlocked.Exchange(ref fitBatches, 0);
            System.Threading.Interlocked.Exchange(ref fallbackBatches, 0);
            species = 0;
        }

        [ThreadStatic] private static Scratch? current;
        // Slots contain numeric values only:152 bytes ×4093×48 <29MiB.
        // Unpooled overflow buffers retain no scalar cache. Slots keep their
        // locks through a buffer lease change; a retired writer cannot race a
        // new writer or install a value accepted under the new lease.
        internal sealed class Cache
        {
            internal struct Slot
            {
                internal int Gate, Index;
                internal long Lease, X, Y, ModelEpoch;
                internal MixtureTextureScalars Value;
            }
            internal readonly Slot[] Slots;
            private readonly object modelGate = new object(), prepareGate = new object();
            private readonly bool[] occupied;
            private long preparedLease = long.MinValue;
            private int preparedWidth, left, bottom, right, top, preparedCount;
            private bool preparedResult;
            internal bool Prepare(ColonyReadingBuffer buffer, long lease, int width, int x0, int y0, int x1, int y1, int tableSpecies, out int count)
            {
                lock (prepareGate)
                {
                    if (preparedLease == lease && preparedWidth == width && left == x0 && bottom == y0 && right == x1 && top == y1)
                    {
                        count = preparedCount;
                        return preparedResult;
                    }
                    count = 0;
                    var valid = buffer.Sorted && width > 0 && Slots.Length > 0;
                    if (valid)
                    {
                        Array.Clear(occupied, 0, occupied.Length);
                        var low = 0;
                        var high = buffer.Count;
                        var first = (long)y0 * width + x0;
                        var last = (long)y1 * width + x1;
                        while (low < high)
                        {
                            var mid = (low + high) >> 1;
                            if (buffer.Readings[mid].PublicCell < first)
                                low = mid + 1;
                            else
                                high = mid;
                        }
                        for (var i = low; i < buffer.Count; ++i)
                        {
                            var cell = buffer.Readings[i].PublicCell;
                            if (cell > last)
                                break;
                            var x = cell % width;
                            if (x < x0 || x > x1)
                                continue;
                            ++count;
                            if (valid)
                            {
                                var slot = i % Slots.Length;
                                if (occupied[slot] || buffer.Readings[i].Constituents > Math.Min(tableSpecies, MaximumCompactConstituents))
                                    valid = false;
                                else
                                    occupied[slot] = true;
                            }
                        }
                    }
                    preparedLease = lease;
                    preparedWidth = width;
                    left = x0;
                    bottom = y0;
                    right = x1;
                    top = y1;
                    preparedCount = count;
                    preparedResult = valid && count > 0;
                    return preparedResult;
                }
            }

            private ModelKey? currentModel;
            private long modelEpoch;
            private long activeLease;
            internal sealed class ModelKey
            {
                internal readonly MixturePresentationModel Model; internal readonly long Epoch; internal ModelKey(MixturePresentationModel model, long epoch)
                {
                    Model = model;
                    Epoch = epoch;
                }
            }
            internal ModelKey? Key(MixturePresentationModel model, long lease)
            {
                if (System.Threading.Volatile.Read(ref activeLease) != lease)
                    return null;
                var existing = System.Threading.Volatile.Read(ref currentModel);
                if (existing != null && ReferenceEquals(existing.Model, model))
                    return existing;
                lock (modelGate)
                {
                    if (activeLease != lease)
                        return null;
                    existing = currentModel;
                    if (existing != null && ReferenceEquals(existing.Model, model))
                        return existing;
                    var created = new ModelKey(model, ++modelEpoch);
                    System.Threading.Volatile.Write(ref currentModel, created);
                    return created;
                }
            }
            internal void Activate(long lease)
            {
                lock (modelGate)
                {
                    System.Threading.Volatile.Write(ref activeLease, lease);
                }
            }
            internal void RetireModel()
            {
                lock (modelGate)
                {
                    System.Threading.Volatile.Write(ref activeLease, long.MinValue);
                    ++modelEpoch;
                    System.Threading.Volatile.Write(ref currentModel, null);
                }
            }
            internal Cache(int count, long lease = long.MinValue)
            {
                activeLease = lease;
                Slots = new Slot[Math.Min(count, 4093)];
                occupied = new bool[Slots.Length];
                System.Threading.Interlocked.Add(ref cacheSlots, Slots.Length);
            }
        }
        internal static bool Prepare(ColonyReadingBuffer buffer, long lease, int width, int x0, int y0, int x1, int y1, int tableSpecies, out int count)
        {
            count = 0;
            var result = buffer.CompactCache != null && buffer.CompactCache.Prepare(buffer, lease, width, x0, y0, x1, y1, tableSpecies, out count);
            if (result)
                System.Threading.Interlocked.Increment(ref fitBatches);
            else
                System.Threading.Interlocked.Increment(ref fallbackBatches);
            return result;
        }
        internal static MixtureTextureScalars SampleCached(ColonyReadingBuffer buffer, ColonyPresentation.Table table, int index, long lease, long revision, MixturePresentationModel model, double x, double y, out bool constructorRejected)
        {
            constructorRejected = false;
            var cache = buffer.CompactCache;
            if (cache == null || cache.Slots.Length == 0)
                return Sample(table, buffer.Readings[index], revision, model, x, y, out constructorRejected);
            var modelKey = cache.Key(model, lease);
            if (modelKey == null)
                return Sample(table, buffer.Readings[index], revision, model, x, y, out constructorRejected);
            ref var slot = ref cache.Slots[index % cache.Slots.Length];
            if (System.Threading.Interlocked.CompareExchange(ref slot.Gate, 1, 0) != 0)
                return Sample(table, buffer.Readings[index], revision, model, x, y, out constructorRejected);
            try
            {
                var xb = BitConverter.DoubleToInt64Bits(x);
                var yb = BitConverter.DoubleToInt64Bits(y);
                if (slot.Lease == lease && slot.Index == index && slot.ModelEpoch == modelKey.Epoch && slot.X == xb && slot.Y == yb)
                {
                    System.Threading.Interlocked.Increment(ref hits);
                    return slot.Value;
                }
                System.Threading.Interlocked.Increment(ref misses);
                var result = Sample(table, buffer.Readings[index], revision, model, x, y, out constructorRejected);
                if (System.Threading.Volatile.Read(ref buffer.Lease) == lease)
                {
                    slot.Value = result;
                    slot.Index = index;
                    slot.X = xb;
                    slot.Y = yb;
                    slot.ModelEpoch = modelKey.Epoch;
                    slot.Lease = lease;
                }
                return result;
            }
            finally { System.Threading.Volatile.Write(ref slot.Gate, 0); }
        }

        internal static MixtureTextureScalars Sample(ColonyPresentation.Table table, OniNativeColony.Reading cell, long revision, MixturePresentationModel model, double x, double y)
            => Sample(table, cell, revision, model, x, y, out _);
        private static MixtureTextureScalars Sample(ColonyPresentation.Table table, OniNativeColony.Reading cell, long revision, MixturePresentationModel model, double x, double y, out bool constructorRejected)
        {
            constructorRejected = false;
            if (cell.Constituents > Math.Min(table.Ids.Length, MaximumCompactConstituents))
                throw new NotSupportedException("Compact texture capacity uses the unchanged full provider path.");
            System.Threading.Interlocked.Increment(ref rawSamples);
            System.Threading.Volatile.Write(ref species, table.Ids.Length);
            var storage = current ?? (current = new Scratch());
            if (storage.Busy)
                throw new InvalidOperationException("Compact texture sampling cannot reenter its thread scratch.");
            storage.Busy = true;
            try
            {
                storage.Ensure(checked((int)cell.Constituents));
                MixtureReadingAnalysis analyzed;
                try
                {
                    analyzed = Analyze(table, cell, revision, storage.Source);
                }
                catch (ArgumentException) { constructorRejected = true; throw; }
                return model.SampleTexture(analyzed, storage.Source.Values.AsSpan(0, storage.Source.Count), storage.Values, x, y);
            }
            finally { Array.Clear(storage.Source.Values, 0, storage.Source.Count); Array.Clear(storage.Values, 0, storage.Source.Count); storage.Source.Count = 0; storage.Busy = false; }
        }
        private sealed class Scratch
        {
            internal bool Busy;
            internal readonly Source Source = new Source();
            internal MixtureConstituentValue[] Values = Array.Empty<MixtureConstituentValue>();
            internal void Ensure(int count)
            {
                if (Source.Values.Length < count)
                    Source.Values = new MixtureThermodynamicConstituent[Math.Max(8, count)];
                if (Values.Length < count)
                    Values = new MixtureConstituentValue[Math.Max(8, count)];
            }
        }
        // Reused only within one non-reentrant thread-local sampling call.
        private sealed class Source : ICollection<MixtureThermodynamicConstituent>, IEnumerator<MixtureThermodynamicConstituent>
        {
            internal MixtureThermodynamicConstituent[] Values = Array.Empty<MixtureThermodynamicConstituent>();
            public int Count
            {
                get; set;
            }
            public bool IsReadOnly => true;
            private int position;
            public MixtureThermodynamicConstituent Current => Values[position]; object IEnumerator.Current => Current;
            public IEnumerator<MixtureThermodynamicConstituent> GetEnumerator()
            {
                position = -1;
                return this;
            }
            IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
            public bool MoveNext() => ++position < Count;
            public void Reset() => position = -1;
            public void Dispose()
            {
            }
            public void Add(MixtureThermodynamicConstituent item) => throw new NotSupportedException();
            public bool Remove(MixtureThermodynamicConstituent item) => throw new NotSupportedException();
            public void Clear() => throw new NotSupportedException();
            public bool Contains(MixtureThermodynamicConstituent item) => throw new NotSupportedException();
            public void CopyTo(MixtureThermodynamicConstituent[] array, int index) => Array.Copy(Values, 0, array, index, Count);
        }
        private static MixtureReadingAnalysis Analyze(ColonyPresentation.Table table, OniNativeColony.Reading cell, long revision, Source source)
        {
            var parts = table.Parts;
            var phases = table.Phases;
            var molarMasses = table.MolarMasses;
            var ids = table.Ids;
            var catalog = table.Catalog;
            var gasMoles = 0.0;
            for (var c = 0; c < cell.Constituents; ++c)
            {
                var part = parts[cell.FirstConstituent + c];
                if (phases[part.Species] == NativeMatterPhase.Gas)
                    gasMoles += part.MassKg / molarMasses[part.Species];
            }
            var headspace = cell.OccupiedVolumeCubicMetres - cell.LiquidVolumeCubicMetres;
            var idealVolume = gasMoles > 0 && cell.GasPressurePa > 0 ? gasMoles * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * cell.TemperatureKelvin / cell.GasPressurePa : 0;
            var geometric = gasMoles > 0 && headspace > 0 && Math.Abs(headspace - idealVolume) <=
                MixtureThermodynamicReading.MaximumVolumeClosureRelativeTolerance * Math.Max(headspace, idealVolume);
            source.Count = checked((int)cell.Constituents);
            var constituents = source.Values;
            var gasVolume = 0.0;
            for (var c = 0; c < cell.Constituents; ++c)
            {
                var part = parts[cell.FirstConstituent + c];
                var k = (int)part.Species;
                double volume;
                if (phases[k] == NativeMatterPhase.Liquid)
                    volume = part.PartialVolumeCubicMetres;
                else
                {
                    var moles = part.MassKg / molarMasses[k];
                    volume = geometric ? headspace * (moles / gasMoles) : idealVolume * (moles / gasMoles);
                    gasVolume += volume;
                }
                constituents[c] = new MixtureThermodynamicConstituent(ids[k], phases[k], part.MassKg, molarMasses[k], volume);
            }
            if (geometric)
                gasVolume = headspace;
            var total = 0.0;
            for (var i = 0; i < source.Count; ++i)
                total += constituents[i].MassKg;
            var vacuum = Math.Max(0, 1 - cell.OccupiedVolumeCubicMetres);
            // A liquid surface under vacuum carries no pressure in the reading model.
            var mechanical = gasVolume == 0 && vacuum > 0 ? 0 : cell.MechanicalPressurePa;
            return new MixtureReadingAnalysis(source, total, cell.InternalEnergyJoules, 0,
                cell.TemperatureKelvin, gasVolume > 0 ? cell.GasPressurePa : 0, mechanical, 1, gasVolume, cell.LiquidVolumeCubicMetres,
                vacuum, catalog.Version, catalog.Fingerprint, revision, "colony:compact-texture",
                resolvedVacuumInterface: true, scratch: constituents, retain: false);
        }
    }
}
