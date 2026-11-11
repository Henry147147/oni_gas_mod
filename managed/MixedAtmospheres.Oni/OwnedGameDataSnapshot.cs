using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // This owner deliberately has no Harmony hooks. A caller may first compare
    // copies without publishing them, then supply a completed custom-frame
    // overlay when the full ownership boundary is ready.
    internal sealed unsafe class OwnedGameDataSnapshot : IDisposable
    {
        private const long MaximumOwnedBytes = 1024L * 1024 * 1024;
        private const int MaximumCells = 16_777_216;
        private static readonly Channel[] Channels = CreateChannels();
        private static readonly string[] EventPointerFields = CreateEventPointerFields();
        // Each channel owns one native block: `capacities` bytes allocated, the
        // first `lengths` records of it in use. A snapshot captured again into
        // the same object (a publisher's spare) keeps its blocks when they are
        // large enough, so a steady publication allocates nothing.
        private readonly IntPtr[] allocations = new IntPtr[Channels.Length];
        private readonly int[] lengths = new int[Channels.Length];
        private readonly long[] capacities = new long[Channels.Length];
        // Texture planes only: equal identities (OwnedSnapshotPublisher) mean
        // equal bytes; 0 is unknown.
        private readonly long[] textureContent = new long[Channels.Length];
        private IntPtr snapshot;
        private bool disposed;

        internal int Width { get; }
        internal int Height { get; }
        internal int CellCount { get; }
        internal long CapturedBytes { get; private set; }
        internal bool IsDisposed => disposed;
        internal static int ChannelCount => Channels.Length;
        internal static int EventChannelCount => 29;
        internal Sim.GameDataUpdate* Pointer
        {
            get { RequireOpen(); return (Sim.GameDataUpdate*)snapshot; }
        }

        private OwnedGameDataSnapshot(int width, int height)
        {
            Width = width; Height = height; CellCount = checked(width * height);
            snapshot = Marshal.AllocHGlobal(sizeof(Sim.GameDataUpdate));
            *(Sim.GameDataUpdate*)snapshot = default;
            CapturedBytes = sizeof(Sim.GameDataUpdate);
        }

        // source must be the native completed-frame snapshot at a synchronization
        // boundary, with no concurrent writer or outstanding native mutation.
        // Pointer/count validation cannot make an arbitrary address safe to read.
        // `reuse`, a snapshot nobody reads any more, is captured into when it
        // has the same grid (and disposed otherwise, or if the capture fails
        // after it was chosen); an invalid source leaves it untouched.
        internal static OwnedGameDataSnapshot Capture(Sim.GameDataUpdate* source, int width, int height,
            Action<OwnedSnapshotOverlay>? overlay = null, bool allowMissingTextures = false, OwnedGameDataSnapshot? reuse = null)
        {
            if (source == null) throw new ArgumentNullException(nameof(source));
            if (width <= 0 || height <= 0 || (long)width * height > MaximumCells)
                throw new ArgumentOutOfRangeException(nameof(width), "Invalid snapshot dimensions.");
            if (source->numFramesProcessed < 0) throw new InvalidDataException("Negative completed native frame count.");
            var cellCount = checked(width * height);
            Span<int> counts = stackalloc int[Channels.Length];
            long totalBytes = sizeof(Sim.GameDataUpdate);
            // Validate EVERY descriptor before copying any native bytes. A late
            // malformed event cannot expose a partly copied published snapshot.
            for (var index = 0; index < Channels.Length; ++index)
            {
                var channel = Channels[index];
                var count = channel.ReadCount(source, cellCount);
                var pointer = channel.ReadPointer(source);
                if (channel.Texture && allowMissingTextures && pointer == IntPtr.Zero) count = 0;
                if (count < 0 || count > MaximumCells * 8L || (count != 0 && pointer == IntPtr.Zero))
                    throw new InvalidDataException("Invalid native snapshot channel: " + channel.PointerField);
                totalBytes = checked(totalBytes + (long)count * channel.Stride);
                if (totalBytes > MaximumOwnedBytes) throw new InvalidDataException("Snapshot exceeds the owned-buffer limit.");
                counts[index] = count;
            }

            OwnedGameDataSnapshot result;
            if (reuse != null && !reuse.disposed && reuse.Width == width && reuse.Height == height) result = reuse;
            else { reuse?.Dispose(); result = new OwnedGameDataSnapshot(width, height); }
            try
            {
                // Copy scalar metadata, then replace ALL 47 native addresses.
                *result.Pointer = *source;
                for (var index = 0; index < Channels.Length; ++index)
                    result.ReplaceBuffer(index, Channels[index].ReadPointer(source), counts[index]);
                if (overlay != null)
                {
                    var writer = new OwnedSnapshotOverlay(result);
                    try { overlay(writer); }
                    finally { writer.Close(); }
                }
                result.ValidateOwnership();
                return result;
            }
            catch { result.Dispose(); throw; }
        }

        // Only meaningful for a read-only copy, before applying a custom overlay
        // and before native publication changes. Checks every pointer channel.
        internal void AssertEquivalentTo(Sim.GameDataUpdate* source, bool allowMissingTextures = false)
        {
            RequireOpen();
            if (source == null) throw new ArgumentNullException(nameof(source));
            if (source->numFramesProcessed != Pointer->numFramesProcessed)
                throw new InvalidDataException("Copied native frame count differs.");
            for (var index = 0; index < Channels.Length; ++index)
            {
                var channel = Channels[index];
                var count = channel.ReadCount(source, CellCount);
                var address = channel.ReadPointer(source);
                if (channel.Texture && allowMissingTextures && address == IntPtr.Zero) count = 0;
                if (count != lengths[index]) throw new InvalidDataException("Copied channel length differs: " + channel.PointerField);
                var bytes = checked(count * channel.Stride);
                if (bytes == 0) continue;
                if (address == allocations[index] || address == IntPtr.Zero ||
                    !new ReadOnlySpan<byte>((void*)address, bytes).SequenceEqual(new ReadOnlySpan<byte>((void*)allocations[index], bytes)))
                    throw new InvalidDataException("Copied channel is aliased or differs: " + channel.PointerField);
            }
        }

        private void RequireOpen()
        {
            if (disposed) throw new ObjectDisposedException(nameof(OwnedGameDataSnapshot));
        }

        private void ReplaceBuffer(int index, IntPtr source, int count) =>
            ReplaceCombinedBuffer(index, IntPtr.Zero, 0, source, count);

        // Copy into the channel's block when it is large enough and neither
        // source overlaps it (an append's prefix is the block's own records,
        // already in place); otherwise into a new block, freeing the old one
        // only after both copies succeeded, since either source may be it.
        // A channel with no records publishes a null address, as ONI does.
        private void ReplaceCombinedBuffer(int index, IntPtr prefix, int prefixCount, IntPtr suffix, int suffixCount)
        {
            RequireOpen();
            var channel = Channels[index];
            if (prefixCount < 0 || suffixCount < 0 || (prefixCount != 0 && prefix == IntPtr.Zero) ||
                (suffixCount != 0 && suffix == IntPtr.Zero)) throw new InvalidDataException("Invalid replacement channel.");
            var count = checked(prefixCount + suffixCount);
            var bytes = checked(count * channel.Stride);
            var prefixBytes = checked(prefixCount * channel.Stride);
            var suffixBytes = checked(suffixCount * channel.Stride);
            var previousBytes = (long)lengths[index] * channel.Stride;
            var total = checked(CapturedBytes - previousBytes + bytes);
            if (total > MaximumOwnedBytes) throw new InvalidDataException("Snapshot exceeds the owned-buffer limit.");
            var block = allocations[index]; var capacity = capacities[index];
            var appended = prefix == block && prefixCount <= lengths[index];
            if (bytes != 0 && block != IntPtr.Zero && bytes <= capacity &&
                (appended || prefixBytes == 0 || !Overlaps(prefix, prefixBytes, block, capacity)) &&
                (suffixBytes == 0 || !Overlaps(suffix, suffixBytes, block, capacity) || appended && !Overlaps(suffix, suffixBytes, block + prefixBytes, capacity - prefixBytes)))
            {
                if (!appended && prefixBytes != 0) Buffer.MemoryCopy((void*)prefix, (void*)block, capacity, prefixBytes);
                if (suffixBytes != 0) Buffer.MemoryCopy((void*)suffix, (byte*)block + prefixBytes, capacity - prefixBytes, suffixBytes);
                Publish(index, block, count, total);
                return;
            }
            if (bytes == 0)
            {
                // The block stays for a later capture; nothing reads it now.
                Publish(index, block, 0, total);
                return;
            }
            // Event tables grow geometrically; cell planes are exactly their size.
            var size = channel.CountOffset >= 0 ? Math.Max(bytes, Math.Min(Math.Max(capacity * 2, 1024), MaximumOwnedBytes)) : bytes;
            var replacement = IntPtr.Zero;
            try
            {
                replacement = Marshal.AllocHGlobal((IntPtr)size);
                if (prefixBytes != 0) Buffer.MemoryCopy((void*)prefix, (void*)replacement, size, prefixBytes);
                if (suffixBytes != 0) Buffer.MemoryCopy((void*)suffix, (byte*)replacement + prefixBytes, size - prefixBytes, suffixBytes);
            }
            catch
            {
                if (replacement != IntPtr.Zero) Marshal.FreeHGlobal(replacement);
                throw;
            }
            allocations[index] = replacement; capacities[index] = size;
            Publish(index, replacement, count, total);
            if (block != IntPtr.Zero) Marshal.FreeHGlobal(block);
        }
        private void Publish(int index, IntPtr block, int count, long total)
        {
            var channel = Channels[index];
            lengths[index] = count;
            CapturedBytes = total;
            channel.WritePointer(Pointer, count == 0 ? IntPtr.Zero : block);
            if (channel.CountOffset >= 0) *(int*)((byte*)Pointer + channel.CountOffset) = count;
        }
        private static bool Overlaps(IntPtr first, long firstBytes, IntPtr second, long secondBytes) =>
            (byte*)first < (byte*)second + secondBytes && (byte*)second < (byte*)first + firstBytes;
        // The published address of a channel (null when it holds no records).
        private IntPtr Published(int index) => lengths[index] == 0 ? IntPtr.Zero : allocations[index];

        // Gives each texture plane the identity of the same plane in `previous`
        // when its bytes are equal, a new one otherwise. Only a copy of a
        // zero-time data request (no native frame processed: every paused
        // Unity frame) is compared; a processed frame gets new identities
        // without a comparison, as its planes were rewritten. Returns the last
        // identity issued.
        internal long IdentifyTextures(OwnedGameDataSnapshot? previous, long issued)
        {
            RequireOpen();
            var compare = Pointer->numFramesProcessed == 0;
            for (var index = 0; index < Channels.Length; ++index)
            {
                if (!Channels[index].Texture) continue;
                var bytes = (long)lengths[index] * Channels[index].Stride;
                textureContent[index] = compare && previous != null && !previous.disposed && previous.textureContent[index] != 0 &&
                    (long)previous.lengths[index] * Channels[index].Stride == bytes &&
                    SameBytes(allocations[index], previous.allocations[index], bytes) ? previous.textureContent[index] : ++issued;
            }
            return issued;
        }
        private static bool SameBytes(IntPtr first, IntPtr second, long bytes)
        {
            if (bytes == 0 || first == second) return true;
            for (long offset = 0; offset < bytes; offset += int.MaxValue)
            {
                var length = (int)Math.Min(int.MaxValue, bytes - offset);
                if (!new ReadOnlySpan<byte>((byte*)first + offset, length).SequenceEqual(new ReadOnlySpan<byte>((byte*)second + offset, length))) return false;
            }
            return true;
        }
        internal bool TryGetTextureContent(IntPtr data, long bytes, out long content)
        {
            content = 0;
            if (disposed || data == IntPtr.Zero) return false;
            for (var index = 0; index < Channels.Length; ++index)
                if (Channels[index].Texture && Published(index) == data && (long)lengths[index] * Channels[index].Stride == bytes && textureContent[index] != 0)
                { content = textureContent[index]; return true; }
            return false;
        }

        internal Span<T> CellBuffer<T>(string pointerField) where T : unmanaged
        {
            RequireOpen();
            var index = FindChannel(pointerField);
            var channel = Channels[index];
            if (channel.CountOffset >= 0 || channel.ElementType != typeof(T))
                throw new ArgumentException("Snapshot buffer type does not match " + pointerField);
            return new Span<T>((void*)allocations[index], lengths[index]);
        }

        internal void AppendEvents<T>(SnapshotEventChannel eventChannel, ReadOnlySpan<T> events) where T : unmanaged
        {
            RequireOpen();
            var index = FindChannel(EventPointerField(eventChannel));
            var channel = Channels[index];
            if (channel.ElementType != typeof(T)) throw new ArgumentException("Event record type does not match " + eventChannel);
            if (events.Length == 0) return;
            var newCount = checked(lengths[index] + events.Length);
            if (newCount > MaximumCells * 8L) throw new InvalidDataException("Too many snapshot event records.");
            if (CapturedBytes + (long)events.Length * channel.Stride > MaximumOwnedBytes)
                throw new InvalidDataException("Snapshot exceeds the owned-buffer limit.");
            fixed (T* pointer = events)
                ReplaceCombinedBuffer(index, allocations[index], lengths[index], (IntPtr)pointer, events.Length);
        }

        internal static bool HasTransientEvents(Sim.GameDataUpdate* source)
        {
            if(source==null) throw new ArgumentNullException(nameof(source));
            return Channels.Any(channel=>channel.CountOffset>=0 && !RetainedStatusTable(channel.PointerField) && channel.ReadCount(source,0)!=0);
        }
        // Use only on an unpublished candidate. If any allocation fails, the
        // caller discards the entire candidate and retains both sources.
        internal void PrependNativeEventsFrom(OwnedGameDataSnapshot earlier)
        {
            RequireOpen();earlier.RequireOpen();
            if(ReferenceEquals(this,earlier) || Width!=earlier.Width || Height!=earlier.Height)
                throw new ArgumentException("Native event replay requires distinct snapshots of the same grid.");
            for(var index=0;index<Channels.Length;++index)
            {
                var channel=Channels[index];
                if(channel.CountOffset<0 || RetainedStatusTable(channel.PointerField) || earlier.lengths[index]==0) continue;
                var count=checked(earlier.lengths[index]+lengths[index]);
                if(count>MaximumCells*8L) throw new InvalidDataException("Too many replayed native events.");
                ReplaceCombinedBuffer(index,earlier.allocations[index],earlier.lengths[index],allocations[index],lengths[index]);
            }
            Pointer->numFramesProcessed=checked(Pointer->numFramesProcessed+earlier.Pointer->numFramesProcessed);
        }

        // A frozen fault publication must not replay one-shot callbacks from
        // the last successful frame. Keep handle-indexed status tables: game
        // UI/components retain them even when no simulation frame is processed.
        internal void ClearEventsForFault()
        {
            RequireOpen();
            for (var index = 0; index < Channels.Length; ++index)
                if (Channels[index].CountOffset >= 0 && !RetainedStatusTable(Channels[index].PointerField))
                    ReplaceBuffer(index, IntPtr.Zero, 0);
            Pointer->numFramesProcessed = 0;
        }

        private static bool RetainedStatusTable(string field) => field == nameof(Sim.GameDataUpdate.emittedMassEntries) ||
            field == nameof(Sim.GameDataUpdate.elementChunkInfos) || field == nameof(Sim.GameDataUpdate.buildingTemperatures) ||
            field == nameof(Sim.GameDataUpdate.diseaseEmittedInfos) || field == nameof(Sim.GameDataUpdate.diseaseConsumedInfos);

        private void ValidateOwnership()
        {
            for (var index = 0; index < Channels.Length; ++index)
                if (Channels[index].ReadPointer(Pointer) != Published(index) ||
                    (Channels[index].CountOffset >= 0 && Channels[index].ReadCount(Pointer, CellCount) != lengths[index]))
                    throw new InvalidDataException("Snapshot overlay changed a buffer's ownership or length.");
        }

        private static int FindChannel(string field)
        {
            for (var index = 0; index < Channels.Length; ++index)
                if (Channels[index].PointerField == field) return index;
            throw new ArgumentException("Unrecognized snapshot channel: " + field);
        }

        private static string EventPointerField(SnapshotEventChannel channel)
        {
            if ((uint)channel >= EventPointerFields.Length) throw new ArgumentOutOfRangeException(nameof(channel));
            return EventPointerFields[(int)channel];
        }

        private static string[] CreateEventPointerFields()
        {
            var names = Enum.GetNames(typeof(SnapshotEventChannel));
            var fields = new string[names.Length];
            for (var i = 0; i < names.Length; ++i)
            {
                if ((int)Enum.Parse(typeof(SnapshotEventChannel), names[i]) != i)
                    throw new InvalidOperationException("Snapshot event channel indices must be consecutive.");
                fields[i] = char.ToLowerInvariant(names[i][0]) + names[i].Substring(1);
            }
            return fields;
        }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            for (var index = 0; index < allocations.Length; ++index)
                if (allocations[index] != IntPtr.Zero) Marshal.FreeHGlobal(allocations[index]);
            if (snapshot != IntPtr.Zero) Marshal.FreeHGlobal(snapshot);
            snapshot = IntPtr.Zero;
            GC.SuppressFinalize(this);
        }

        ~OwnedGameDataSnapshot() { Dispose(); }

        private sealed class Channel
        {
            internal readonly string PointerField;
            internal readonly int PointerOffset, CountOffset, Stride, PerCell;
            internal readonly bool Texture;
            internal readonly Type ElementType;
            internal Channel(string pointerField, Type elementType, int stride, string? countField = null, int perCell = 1, bool texture = false)
            {
                PointerField = pointerField; ElementType = elementType; Stride = stride; PerCell = perCell; Texture = texture;
                PointerOffset = checked((int)Marshal.OffsetOf<Sim.GameDataUpdate>(pointerField));
                CountOffset = countField == null ? -1 : checked((int)Marshal.OffsetOf<Sim.GameDataUpdate>(countField));
            }
            internal int ReadCount(Sim.GameDataUpdate* value, int cells) =>
                CountOffset < 0 ? checked(cells * PerCell) : *(int*)((byte*)value + CountOffset);
            internal IntPtr ReadPointer(Sim.GameDataUpdate* value) => *(IntPtr*)((byte*)value + PointerOffset);
            internal void WritePointer(Sim.GameDataUpdate* value, IntPtr pointer) => *(IntPtr*)((byte*)value + PointerOffset) = pointer;
        }

        private static Channel[] CreateChannels()
        {
            var channels = new[]
            {
                new Channel(nameof(Sim.GameDataUpdate.elementIdx), typeof(ushort), sizeof(ushort)),
                new Channel(nameof(Sim.GameDataUpdate.temperature), typeof(float), sizeof(float)),
                new Channel(nameof(Sim.GameDataUpdate.mass), typeof(float), sizeof(float)),
                new Channel(nameof(Sim.GameDataUpdate.properties), typeof(byte), sizeof(byte)),
                new Channel(nameof(Sim.GameDataUpdate.insulation), typeof(byte), sizeof(byte)),
                new Channel(nameof(Sim.GameDataUpdate.strengthInfo), typeof(byte), sizeof(byte)),
                new Channel(nameof(Sim.GameDataUpdate.radiation), typeof(float), sizeof(float)),
                new Channel(nameof(Sim.GameDataUpdate.diseaseIdx), typeof(byte), sizeof(byte)),
                new Channel(nameof(Sim.GameDataUpdate.diseaseCount), typeof(int), sizeof(int)),
                new Channel(nameof(Sim.GameDataUpdate.backwallElement), typeof(ushort), sizeof(ushort)),
                new Channel(nameof(Sim.GameDataUpdate.backwallMass), typeof(float), sizeof(float)),
                new Channel(nameof(Sim.GameDataUpdate.backwallTemperature), typeof(float), sizeof(float)),
                new Channel(nameof(Sim.GameDataUpdate.solidInfo), typeof(Sim.SolidInfo), sizeof(Sim.SolidInfo), nameof(Sim.GameDataUpdate.numSolidInfo)),
                new Channel(nameof(Sim.GameDataUpdate.liquidChangeInfo), typeof(Sim.LiquidChangeInfo), sizeof(Sim.LiquidChangeInfo), nameof(Sim.GameDataUpdate.numLiquidChangeInfo)),
                new Channel(nameof(Sim.GameDataUpdate.solidSubstanceChangeInfo), typeof(Sim.SolidSubstanceChangeInfo), sizeof(Sim.SolidSubstanceChangeInfo), nameof(Sim.GameDataUpdate.numSolidSubstanceChangeInfo)),
                new Channel(nameof(Sim.GameDataUpdate.substanceChangeInfo), typeof(Sim.SubstanceChangeInfo), sizeof(Sim.SubstanceChangeInfo), nameof(Sim.GameDataUpdate.numSubstanceChangeInfo)),
                new Channel(nameof(Sim.GameDataUpdate.callbackInfo), typeof(Sim.CallbackInfo), sizeof(Sim.CallbackInfo), nameof(Sim.GameDataUpdate.numCallbackInfo)),
                new Channel(nameof(Sim.GameDataUpdate.spawnFallingLiquidInfo), typeof(Sim.SpawnFallingLiquidInfo), sizeof(Sim.SpawnFallingLiquidInfo), nameof(Sim.GameDataUpdate.numSpawnFallingLiquidInfo)),
                new Channel(nameof(Sim.GameDataUpdate.digInfo), typeof(Sim.SpawnOreInfo), sizeof(Sim.SpawnOreInfo), nameof(Sim.GameDataUpdate.numDigInfo)),
                new Channel(nameof(Sim.GameDataUpdate.spawnOreInfo), typeof(Sim.SpawnOreInfo), sizeof(Sim.SpawnOreInfo), nameof(Sim.GameDataUpdate.numSpawnOreInfo)),
                new Channel(nameof(Sim.GameDataUpdate.spawnFXInfo), typeof(Sim.SpawnFXInfo), sizeof(Sim.SpawnFXInfo), nameof(Sim.GameDataUpdate.numSpawnFXInfo)),
                new Channel(nameof(Sim.GameDataUpdate.unstableCellInfo), typeof(Sim.UnstableCellInfo), sizeof(Sim.UnstableCellInfo), nameof(Sim.GameDataUpdate.numUnstableCellInfo)),
                new Channel(nameof(Sim.GameDataUpdate.worldDamageInfo), typeof(Sim.WorldDamageInfo), sizeof(Sim.WorldDamageInfo), nameof(Sim.GameDataUpdate.numWorldDamageInfo)),
                new Channel(nameof(Sim.GameDataUpdate.buildingTemperatures), typeof(Sim.BuildingTemperatureInfo), sizeof(Sim.BuildingTemperatureInfo), nameof(Sim.GameDataUpdate.numBuildingTemperatures)),
                new Channel(nameof(Sim.GameDataUpdate.massConsumedCallbacks), typeof(Sim.MassConsumedCallback), sizeof(Sim.MassConsumedCallback), nameof(Sim.GameDataUpdate.numMassConsumedCallbacks)),
                new Channel(nameof(Sim.GameDataUpdate.massEmittedCallbacks), typeof(Sim.MassEmittedCallback), sizeof(Sim.MassEmittedCallback), nameof(Sim.GameDataUpdate.numMassEmittedCallbacks)),
                new Channel(nameof(Sim.GameDataUpdate.diseaseConsumptionCallbacks), typeof(Sim.DiseaseConsumptionCallback), sizeof(Sim.DiseaseConsumptionCallback), nameof(Sim.GameDataUpdate.numDiseaseConsumptionCallbacks)),
                new Channel(nameof(Sim.GameDataUpdate.componentStateChangedMessages), typeof(Sim.ComponentStateChangedMessage), sizeof(Sim.ComponentStateChangedMessage), nameof(Sim.GameDataUpdate.numComponentStateChangedMessages)),
                new Channel(nameof(Sim.GameDataUpdate.removedMassEntries), typeof(Sim.ConsumedMassInfo), sizeof(Sim.ConsumedMassInfo), nameof(Sim.GameDataUpdate.numRemovedMassEntries)),
                new Channel(nameof(Sim.GameDataUpdate.emittedMassEntries), typeof(Sim.EmittedMassInfo), sizeof(Sim.EmittedMassInfo), nameof(Sim.GameDataUpdate.numEmittedMassEntries)),
                new Channel(nameof(Sim.GameDataUpdate.elementChunkInfos), typeof(Sim.ElementChunkInfo), sizeof(Sim.ElementChunkInfo), nameof(Sim.GameDataUpdate.numElementChunkInfos)),
                new Channel(nameof(Sim.GameDataUpdate.elementChunkMeltedInfos), typeof(Sim.MeltedInfo), sizeof(Sim.MeltedInfo), nameof(Sim.GameDataUpdate.numElementChunkMeltedInfos)),
                new Channel(nameof(Sim.GameDataUpdate.buildingOverheatInfos), typeof(Sim.MeltedInfo), sizeof(Sim.MeltedInfo), nameof(Sim.GameDataUpdate.numBuildingOverheatInfos)),
                new Channel(nameof(Sim.GameDataUpdate.buildingNoLongerOverheatedInfos), typeof(Sim.MeltedInfo), sizeof(Sim.MeltedInfo), nameof(Sim.GameDataUpdate.numBuildingNoLongerOverheatedInfos)),
                new Channel(nameof(Sim.GameDataUpdate.buildingMeltedInfos), typeof(Sim.MeltedInfo), sizeof(Sim.MeltedInfo), nameof(Sim.GameDataUpdate.numBuildingMeltedInfos)),
                new Channel(nameof(Sim.GameDataUpdate.cellMeltedInfos), typeof(Sim.CellMeltedInfo), sizeof(Sim.CellMeltedInfo), nameof(Sim.GameDataUpdate.numCellMeltedInfos)),
                new Channel(nameof(Sim.GameDataUpdate.backwallElementChangedInfos), typeof(Sim.BackwallElementChangedInfo), sizeof(Sim.BackwallElementChangedInfo), nameof(Sim.GameDataUpdate.numBackwallElementChangedInfos)),
                new Channel(nameof(Sim.GameDataUpdate.backwallShouldTransitionInfos), typeof(Sim.BackwallShouldTransitionInfo), sizeof(Sim.BackwallShouldTransitionInfo), nameof(Sim.GameDataUpdate.numBackwallShouldTransitionInfos)),
                new Channel(nameof(Sim.GameDataUpdate.diseaseEmittedInfos), typeof(Sim.DiseaseEmittedInfo), sizeof(Sim.DiseaseEmittedInfo), nameof(Sim.GameDataUpdate.numDiseaseEmittedInfos)),
                new Channel(nameof(Sim.GameDataUpdate.diseaseConsumedInfos), typeof(Sim.DiseaseConsumedInfo), sizeof(Sim.DiseaseConsumedInfo), nameof(Sim.GameDataUpdate.numDiseaseConsumedInfos)),
                new Channel(nameof(Sim.GameDataUpdate.radiationConsumedCallbacks), typeof(Sim.ConsumedRadiationCallback), sizeof(Sim.ConsumedRadiationCallback), nameof(Sim.GameDataUpdate.numRadiationConsumedCallbacks)),
                new Channel(nameof(Sim.GameDataUpdate.accumulatedFlow), typeof(float), sizeof(float)),
                new Channel(nameof(Sim.GameDataUpdate.propertyTextureFlow), typeof(float), sizeof(float), perCell: 2, texture: true),
                new Channel(nameof(Sim.GameDataUpdate.propertyTextureLiquid), typeof(byte), sizeof(byte), perCell: 4, texture: true),
                new Channel(nameof(Sim.GameDataUpdate.propertyTextureLiquidData), typeof(byte), sizeof(byte), perCell: 4, texture: true),
                new Channel(nameof(Sim.GameDataUpdate.propertyTextureMaterialData), typeof(byte), sizeof(byte), perCell: 4, texture: true),
                new Channel(nameof(Sim.GameDataUpdate.propertyTextureExposedToSunlight), typeof(byte), sizeof(byte), texture: true)
            };
            var fields = typeof(Sim.GameDataUpdate).GetFields(BindingFlags.Public | BindingFlags.Instance);
            var classified = new HashSet<int> { checked((int)Marshal.OffsetOf<Sim.GameDataUpdate>(nameof(Sim.GameDataUpdate.numFramesProcessed))) };
            foreach (var channel in channels)
            {
                if (!classified.Add(channel.PointerOffset) || (channel.CountOffset >= 0 && !classified.Add(channel.CountOffset)))
                    throw new NotSupportedException("Duplicate snapshot channel description.");
                var fieldType = fields.Single(field => field.Name == channel.PointerField).FieldType;
                if (fieldType != typeof(IntPtr) && (!fieldType.IsPointer || fieldType.GetElementType() != channel.ElementType))
                    throw new NotSupportedException("Snapshot pointer type changed: " + channel.PointerField);
                if (channel.CountOffset >= 0 && fields.Single(field => checked((int)Marshal.OffsetOf<Sim.GameDataUpdate>(field.Name)) == channel.CountOffset).FieldType != typeof(int))
                    throw new NotSupportedException("Snapshot count type changed: " + channel.PointerField);
            }
            if (IntPtr.Size != 8 || sizeof(Sim.GameDataUpdate) != 496 || fields.Length != 77 ||
                channels.Length != 47 || classified.Count != fields.Length ||
                fields.Any(field => !classified.Contains(checked((int)Marshal.OffsetOf<Sim.GameDataUpdate>(field.Name)))))
                throw new NotSupportedException("Unrecognized snapshot layout or unowned snapshot channel.");
            return channels;
        }
    }

    internal enum SnapshotEventChannel
    {
        SolidInfo, LiquidChangeInfo, SolidSubstanceChangeInfo, SubstanceChangeInfo, CallbackInfo,
        SpawnFallingLiquidInfo, DigInfo, SpawnOreInfo, SpawnFXInfo, UnstableCellInfo, WorldDamageInfo,
        BuildingTemperatures, MassConsumedCallbacks, MassEmittedCallbacks, DiseaseConsumptionCallbacks,
        ComponentStateChangedMessages, RemovedMassEntries, EmittedMassEntries, ElementChunkInfos,
        ElementChunkMeltedInfos, BuildingOverheatInfos, BuildingNoLongerOverheatedInfos, BuildingMeltedInfos,
        CellMeltedInfos, BackwallElementChangedInfos, BackwallShouldTransitionInfos, DiseaseEmittedInfos,
        DiseaseConsumedInfos, RadiationConsumedCallbacks
    }

    // This extension point is valid only during Capture's synchronous callback.
    // Array/event records are display and game-compatibility projections; the
    // caller must obtain them from the same completed authoritative custom frame.
    internal sealed class OwnedSnapshotOverlay
    {
        private readonly OwnedGameDataSnapshot owner;
        private bool active = true;
        internal OwnedSnapshotOverlay(OwnedGameDataSnapshot owner) { this.owner = owner; }
        internal void Close() { active = false; }
        private Span<T> Buffer<T>(string name) where T : unmanaged
        {
            if (!active) throw new InvalidOperationException("Snapshot overlay is closed.");
            return owner.CellBuffer<T>(name);
        }
        internal Span<ushort> Elements => Buffer<ushort>(nameof(Sim.GameDataUpdate.elementIdx));
        internal Span<float> Temperatures => Buffer<float>(nameof(Sim.GameDataUpdate.temperature));
        internal Span<float> Masses => Buffer<float>(nameof(Sim.GameDataUpdate.mass));
        internal Span<byte> Properties => Buffer<byte>(nameof(Sim.GameDataUpdate.properties));
        internal Span<byte> Insulation => Buffer<byte>(nameof(Sim.GameDataUpdate.insulation));
        internal Span<byte> Strength => Buffer<byte>(nameof(Sim.GameDataUpdate.strengthInfo));
        internal Span<float> Radiation => Buffer<float>(nameof(Sim.GameDataUpdate.radiation));
        internal Span<byte> DiseaseIndices => Buffer<byte>(nameof(Sim.GameDataUpdate.diseaseIdx));
        internal Span<int> DiseaseCounts => Buffer<int>(nameof(Sim.GameDataUpdate.diseaseCount));
        internal Span<ushort> BackwallElements => Buffer<ushort>(nameof(Sim.GameDataUpdate.backwallElement));
        internal Span<float> BackwallMasses => Buffer<float>(nameof(Sim.GameDataUpdate.backwallMass));
        internal Span<float> BackwallTemperatures => Buffer<float>(nameof(Sim.GameDataUpdate.backwallTemperature));
        internal Span<float> AccumulatedFlow => Buffer<float>(nameof(Sim.GameDataUpdate.accumulatedFlow));
        internal Span<float> FlowTexture => Buffer<float>(nameof(Sim.GameDataUpdate.propertyTextureFlow));
        internal Span<byte> LiquidTexture => Buffer<byte>(nameof(Sim.GameDataUpdate.propertyTextureLiquid));
        internal Span<byte> LiquidDataTexture => Buffer<byte>(nameof(Sim.GameDataUpdate.propertyTextureLiquidData));
        internal Span<byte> MaterialDataTexture => Buffer<byte>(nameof(Sim.GameDataUpdate.propertyTextureMaterialData));
        internal Span<byte> SunlightTexture => Buffer<byte>(nameof(Sim.GameDataUpdate.propertyTextureExposedToSunlight));
        internal void AppendEvents<T>(SnapshotEventChannel channel, ReadOnlySpan<T> events) where T : unmanaged
        {
            if (!active) throw new InvalidOperationException("Snapshot overlay is closed.");
            owner.AppendEvents(channel, events);
        }
    }

    // Publish only at a boundary where readers of the previous publication have
    // completed. Game.simData keeps some event pointers until the NEXT snapshot;
    // disposing a copy immediately after Game.StepTheSim would be unsafe.
    //
    // The game reads the snapshot it was last handed (Grid's cell pointers,
    // its event tables) until it is handed another one. A publication is
    // therefore built as a candidate the game cannot see (Begin) and becomes
    // current only when nothing else can fail (Commit, the last step before
    // the pointer is returned). An abandoned candidate is discarded; the
    // current snapshot is never freed or rewritten until its successor has
    // been committed.
    internal sealed unsafe class OwnedSnapshotPublisher : IDisposable
    {
        // reuseBuffers: the snapshot retired by a hand-over (which the game no
        // longer reads) is kept as the spare the next publication is copied
        // into, so a steady publication allocates no native memory. The game
        // then alternates between two sets of addresses.
        private readonly bool reuseBuffers;
        private OwnedGameDataSnapshot? current, candidate, spare;
        private long textureContents;
        private bool disposed;
        internal OwnedSnapshotPublisher(bool reuseBuffers = false) { this.reuseBuffers = reuseBuffers; }
        internal Sim.GameDataUpdate* CurrentPointer => disposed
            ? throw new ObjectDisposedException(nameof(OwnedSnapshotPublisher)) : current == null ? null : current.Pointer;
        internal bool HasCandidate => candidate != null;
        internal bool HasSpare => spare != null;
        internal OwnedGameDataSnapshot Publish(Sim.GameDataUpdate* source, int width, int height,
            Action<OwnedSnapshotOverlay>? overlay = null, bool allowMissingTextures = false)
        {
            var completed = Begin(source, width, height, overlay, allowMissingTextures);
            Commit(completed);
            return completed;
        }
        // A copy of source the caller completes before Commit or Abandon.
        internal OwnedGameDataSnapshot Begin(Sim.GameDataUpdate* source, int width, int height,
            Action<OwnedSnapshotOverlay>? overlay = null, bool allowMissingTextures = false)
        {
            if (disposed) throw new ObjectDisposedException(nameof(OwnedSnapshotPublisher));
            if (candidate != null) throw new InvalidOperationException("A snapshot publication is already being built.");
            var reuse = spare; spare = null;
            try { candidate = OwnedGameDataSnapshot.Capture(source, width, height, overlay, allowMissingTextures, reuse); }
            catch
            {
                // A source refused before any copy leaves the spare as it was.
                if (reuse != null && !reuse.IsDisposed) spare = reuse;
                throw;
            }
            return candidate;
        }
        // Hands the candidate over; only then is the previous snapshot retired
        // (kept as the spare, or freed).
        internal IntPtr Commit(OwnedGameDataSnapshot completed)
        {
            if (disposed || !ReferenceEquals(completed, candidate))
            {
                // A reset while it was built already took the candidate.
                if (!ReferenceEquals(completed, current)) completed.Dispose();
                throw new InvalidOperationException("The snapshot publication is no longer being built.");
            }
            var pointer = (IntPtr)completed.Pointer;
            if (reuseBuffers) textureContents = completed.IdentifyTextures(current, textureContents);
            candidate = null;
            var previous = current;
            current = completed;
            if (reuseBuffers && previous != null) { spare?.Dispose(); spare = previous; }
            else previous?.Dispose();
            return pointer;
        }
        // A failed publication: the current snapshot stays as it was.
        internal void Abandon(OwnedGameDataSnapshot? failed)
        {
            if (failed == null) return;
            if (ReferenceEquals(failed, candidate)) candidate = null;
            if (!ReferenceEquals(failed, current) && !ReferenceEquals(failed, spare)) failed.Dispose();
        }
        // The content identity of a texture plane the game was handed with
        // the current snapshot (reuseBuffers only): equal identities, equal
        // bytes. False for any other address or size.
        internal bool TryGetTextureContent(IntPtr data, long bytes, out long content)
        {
            content = 0;
            return reuseBuffers && !disposed && current != null && current.TryGetTextureContent(data, bytes, out content);
        }
        // Session teardown: the game no longer reads any snapshot.
        internal void Clear()
        {
            candidate?.Dispose(); candidate = null;
            current?.Dispose(); current = null;
            spare?.Dispose(); spare = null;
        }
        public void Dispose()
        {
            if (disposed) return;
            disposed = true; Clear();
        }
    }

    internal sealed class SnapshotCopyValidationReport
    {
        public int CellCount, ChannelCount, EventChannelCount, NativeFramesProcessed;
        public long CopiedBytes;
        public bool ByteIdentical;
    }

    internal static unsafe class SnapshotCopyValidation
    {
        // A read-only opt-in hook: validate and dispose an independent copy,
        // leaving every game/native pointer and every callback untouched.
        internal static SnapshotCopyValidationReport CaptureAndValidate(Sim.GameDataUpdate* source, int width, int height,
            bool allowMissingTextures = false)
        {
            using var owned = OwnedGameDataSnapshot.Capture(source, width, height, allowMissingTextures: allowMissingTextures);
            owned.AssertEquivalentTo(source, allowMissingTextures);
            return new SnapshotCopyValidationReport
            {
                CellCount = owned.CellCount, ChannelCount = OwnedGameDataSnapshot.ChannelCount,
                EventChannelCount = OwnedGameDataSnapshot.EventChannelCount,
                NativeFramesProcessed = source->numFramesProcessed, CopiedBytes = owned.CapturedBytes, ByteIdentical = true
            };
        }
    }
}
