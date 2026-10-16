using System;
using System.Collections.Generic;
using System.Buffers.Binary;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using MixedAtmospheres.Oni;

// Synthetic memory ownership tests compiled against the actual locally licensed
// declarations. No Unity singleton, native simulator, or game code is started.
internal static unsafe class Program
{
    private static int passed;
    private static int Main(string[] args)
    {
        if (args.Length is < 1 or > 2 || (args.Length == 2 && args[1] != "--frame-coordinator-benchmark"))
            throw new ArgumentException("Usage: OwnedSnapshotTests <licensed ONI root> [--frame-coordinator-benchmark]");
        var managed = Path.Combine(Path.GetFullPath(args[0]), "OxygenNotIncluded_Data", "Managed");
        AssemblyLoadContext.Default.Resolving += (_, name) =>
        {
            var candidate = Path.Combine(managed, name.Name + ".dll");
            return File.Exists(candidate) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(candidate) : null;
        };
        return Run(args.Length == 2);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Run(bool benchmark)
    {
        Test("all actual game channels are deep copied", () =>
        {
            using var source = new NativeFixture();
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            copy.AssertEquivalentTo(source.Pointer);
            Check(OwnedGameDataSnapshot.ChannelCount == 47 && OwnedGameDataSnapshot.EventChannelCount == 29);
            var report = SnapshotCopyValidation.CaptureAndValidate(source.Pointer, 3, 2);
            Check(report.ByteIdentical && report.CellCount == 6 && report.ChannelCount == 47 && report.EventChannelCount == 29);
        });
        Test("copy survives source overwrite and release", () =>
        {
            var source = new NativeFixture();
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            var mass = copy.Pointer->mass[2];
            var eventByte = ((byte*)copy.Pointer->massConsumedCallbacks)[0];
            var texture = ((byte*)copy.Pointer->propertyTextureLiquidData)[7];
            source.Overwrite(); source.Dispose();
            Check(copy.Pointer->mass[2] == mass && ((byte*)copy.Pointer->massConsumedCallbacks)[0] == eventByte &&
                ((byte*)copy.Pointer->propertyTextureLiquidData)[7] == texture);
        });
        Test("custom overlay appends callbacks while preserving native bytes", () =>
        {
            using var source = new NativeFixture();
            var originalMass = source.Pointer->mass[0];
            var originalCount = source.Pointer->numMassConsumedCallbacks;
            OwnedSnapshotOverlay? retained = null;
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2, writer =>
            {
                retained = writer;
                writer.Masses[0] = 12.5f;
                writer.Temperatures[0] = 300;
                writer.AppendEvents<Sim.MassConsumedCallback>(SnapshotEventChannel.MassConsumedCallbacks,
                    new[] { new Sim.MassConsumedCallback { callbackIdx = 101, elemIdx = 4, mass = 0.25f, temperature = 300, diseaseIdx = 255 } });
            });
            Check(copy.Pointer->mass[0] == 12.5f && source.Pointer->mass[0] == originalMass);
            Check(copy.Pointer->numMassConsumedCallbacks == originalCount + 1);
            Check(copy.Pointer->massConsumedCallbacks[originalCount].callbackIdx == 101);
            Check(new ReadOnlySpan<byte>(source.Pointer->massConsumedCallbacks, originalCount * sizeof(Sim.MassConsumedCallback))
                .SequenceEqual(new ReadOnlySpan<byte>(copy.Pointer->massConsumedCallbacks, originalCount * sizeof(Sim.MassConsumedCallback))));
            Reject(() => { _ = retained!.Masses.Length; });
        });
        Test("fault freeze retains physical buffers and status tables without replaying callbacks", () =>
        {
            using var source = new NativeFixture();
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            var mass = copy.Pointer->mass[0]; var texture = ((byte*)copy.Pointer->propertyTextureLiquidData)[7];
            var statusNames = new[] { nameof(Sim.GameDataUpdate.emittedMassEntries), nameof(Sim.GameDataUpdate.elementChunkInfos),
                nameof(Sim.GameDataUpdate.buildingTemperatures), nameof(Sim.GameDataUpdate.diseaseEmittedInfos), nameof(Sim.GameDataUpdate.diseaseConsumedInfos) };
            var statuses = statusNames.Select(field =>
            {
                var pointerOffset = (int)Marshal.OffsetOf<Sim.GameDataUpdate>(field);
                var countOffset = (int)Marshal.OffsetOf<Sim.GameDataUpdate>("num" + char.ToUpperInvariant(field[0]) + field.Substring(1));
                var count = *(int*)((byte*)copy.Pointer + countOffset);
                var address = *(IntPtr*)((byte*)copy.Pointer + pointerOffset);
                var stride = Marshal.SizeOf(typeof(Sim.GameDataUpdate).GetField(field)!.FieldType.GetElementType()!);
                return (pointerOffset, countOffset, count, address, bytes: new ReadOnlySpan<byte>((void*)address, count * stride).ToArray());
            }).ToArray();
            Check(copy.Pointer->numMassConsumedCallbacks > 0 && copy.Pointer->numFramesProcessed > 0);
            copy.ClearEventsForFault(); copy.ClearEventsForFault();
            Check(copy.Pointer->numFramesProcessed == 0 && copy.Pointer->numMassConsumedCallbacks == 0 &&
                copy.Pointer->massConsumedCallbacks == null && copy.Pointer->numMassEmittedCallbacks == 0 &&
                copy.Pointer->numCallbackInfo == 0 && copy.Pointer->mass[0] == mass &&
                ((byte*)copy.Pointer->propertyTextureLiquidData)[7] == texture && source.Pointer->numMassConsumedCallbacks > 0);
            Check(statuses.Length == 5 && statuses.All(status => status.count > 0));
            foreach (var status in statuses) Check(*(int*)((byte*)copy.Pointer + status.countOffset) == status.count &&
                *(IntPtr*)((byte*)copy.Pointer + status.pointerOffset) == status.address &&
                new ReadOnlySpan<byte>((void*)status.address, status.bytes.Length).SequenceEqual(status.bytes));
        });
        Test("wrong event record type rejects unpublished overlay", () =>
        {
            using var source = new NativeFixture();
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2,
                writer => writer.AppendEvents<int>(SnapshotEventChannel.MassConsumedCallbacks, new[] { 1 })));
        });
        Test("late malformed native event leaves previous publication valid", () =>
        {
            using var source = new NativeFixture();
            using var publisher = new OwnedSnapshotPublisher();
            var first = publisher.Publish(source.Pointer, 3, 2);
            var pointer = (IntPtr)first.Pointer;
            source.Pointer->numRadiationConsumedCallbacks = -1;
            Reject(() => publisher.Publish(source.Pointer, 3, 2));
            Check((IntPtr)publisher.CurrentPointer == pointer && first.Pointer->numFramesProcessed == 2);
        });
        Test("overlay failure leaves previous publication valid", () =>
        {
            using var source = new NativeFixture();
            using var publisher = new OwnedSnapshotPublisher();
            var first = publisher.Publish(source.Pointer, 3, 2);
            var pointer = (IntPtr)first.Pointer;
            Reject(() => publisher.Publish(source.Pointer, 3, 2, writer =>
            {
                writer.Masses[0] = 123;
                throw new InvalidDataException("Injected incomplete custom frame");
            }));
            Check((IntPtr)publisher.CurrentPointer == pointer && first.Pointer->mass[0] == source.Pointer->mass[0]);
        });
        Test("next successful publication retires old buffers", () =>
        {
            using var source = new NativeFixture();
            using var publisher = new OwnedSnapshotPublisher();
            var first = publisher.Publish(source.Pointer, 3, 2);
            source.Pointer->numFramesProcessed = 3;
            var second = publisher.Publish(source.Pointer, 3, 2);
            Reject(() => { _ = (IntPtr)first.Pointer; });
            Check(second.Pointer->numFramesProcessed == 3 && publisher.CurrentPointer == second.Pointer);
            publisher.Dispose();
            Reject(() => { _ = (IntPtr)second.Pointer; });
            Reject(() => publisher.Publish(source.Pointer, 3, 2));
        });
        Test("zero-count arrays relinquish native addresses", () =>
        {
            using var source = new NativeFixture();
            source.Pointer->numMassConsumedCallbacks = 0;
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            Check(source.Pointer->massConsumedCallbacks != null && copy.Pointer->massConsumedCallbacks == null);
            copy.AssertEquivalentTo(source.Pointer);
        });
        Test("required cell and texture pointers reject null", () =>
        {
            using var source = new NativeFixture();
            source.Pointer->mass = null;
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2));
            using var textures = new NativeFixture();
            textures.Pointer->propertyTextureLiquid = IntPtr.Zero;
            Reject(() => OwnedGameDataSnapshot.Capture(textures.Pointer, 3, 2));
            using var headless = OwnedGameDataSnapshot.Capture(textures.Pointer, 3, 2, allowMissingTextures: true);
            Check(headless.Pointer->propertyTextureLiquid == IntPtr.Zero);
            headless.AssertEquivalentTo(textures.Pointer, allowMissingTextures: true);
        });
        Test("invalid dimensions and frame metadata reject", () =>
        {
            using var source = new NativeFixture();
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, 0, 2));
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, int.MaxValue, 2));
            Reject(() => OwnedGameDataSnapshot.Capture(null, 3, 2));
            source.Pointer->numFramesProcessed = -1;
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2));
        });
        Test("colony envelope owns native and custom state in one frame", () =>
        {
            var native = NativePayload();
            var custom = new byte[] { 1, 2, 3, 4, 5 };
            var catalog = CatalogHash();
            var envelope = ColonySaveEnvelope.Create(744825, 1, 1234, catalog, native, custom);
            var encoded = envelope.CopyEncodedData();
            Check(ColonySaveEnvelope.HasMagic(encoded) && !encoded.AsSpan(0, 8).SequenceEqual(native.AsSpan(0, 8)));
            native[30] ^= 0xff; custom[0] ^= 0xff; catalog[0] ^= 0xff;
            var restored = ColonySaveEnvelope.Parse(encoded, 744825, 1, CatalogHash());
            encoded[0] = 0;
            Check(restored.OniBuild == 744825 && restored.MaterialModelVersion == 1 && restored.CompletedFrame == 1234);
            Check(restored.NativeData.SequenceEqual(NativePayload()) && restored.CustomData.SequenceEqual(new byte[] { 1, 2, 3, 4, 5 }));
            Check(restored.MaterialCatalogHash.SequenceEqual(CatalogHash()));
        });
        Test("length-prefixed envelope consumes exactly its own section", () =>
        {
            var envelope = ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), NativePayload(), new byte[] { 9 });
            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream, System.Text.Encoding.UTF8, true))
            {
                envelope.WriteLengthPrefixed(writer); writer.Write(1234567);
            }
            stream.Position = 0;
            using var reader = new BinaryReader(stream);
            var restored = ColonySaveEnvelope.ReadLengthPrefixed(reader, 744825, 1, CatalogHash());
            Check(restored.CompletedFrame == 0 && restored.CustomData[0] == 9 && reader.ReadInt32() == 1234567);
        });
        Test("corrupt header payload and checksum reject", () =>
        {
            var envelope = ColonySaveEnvelope.Create(744825, 1, 55, CatalogHash(), NativePayload(), new byte[] { 9 });
            foreach (var position in new[] { 0, 8, 20, 60, 68, 80, envelope.EncodedLength - 1 })
            {
                var damaged = envelope.CopyEncodedData(); damaged[position] ^= 0x80;
                Reject(() => ColonySaveEnvelope.Parse(damaged, 744825, 1, CatalogHash()));
            }
            var overflow = envelope.CopyEncodedData();
            BinaryPrimitives.WriteInt64LittleEndian(overflow.AsSpan(60), long.MaxValue);
            Reject(() => ColonySaveEnvelope.Parse(overflow, 744825, 1, CatalogHash()));
        });
        Test("incompatible build material model and catalogue reject", () =>
        {
            var envelope = ColonySaveEnvelope.Create(744825, 1, 55, CatalogHash(), NativePayload(), new byte[] { 9 });
            var encoded = envelope.CopyEncodedData();
            Reject(() => ColonySaveEnvelope.Parse(encoded, 744826, 1, CatalogHash()));
            Reject(() => ColonySaveEnvelope.Parse(encoded, 744825, 2, CatalogHash()));
            var other = CatalogHash(); other[0] ^= 0xff;
            Reject(() => ColonySaveEnvelope.Parse(encoded, 744825, 1, other));
            Reject(() => ColonySaveEnvelope.Create(744825, 1, 0, new byte[31], NativePayload(), new byte[] { 9 }));
        });
        Test("incomplete native framing or custom state reject", () =>
        {
            Reject(() => ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), NativePayload(), Array.Empty<byte>()));
            var native = NativePayload();
            Reject(() => ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), native.AsSpan(0, native.Length - 1), new byte[] { 9 }));
            native[0] = 0;
            Reject(() => ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), native, new byte[] { 9 }));
            var envelope = ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), NativePayload(), new byte[] { 9 });
            var encoded = envelope.CopyEncodedData();
            Reject(() => ColonySaveEnvelope.Parse(encoded.AsSpan(0, encoded.Length - 1), 744825, 1, CatalogHash()));
            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream, System.Text.Encoding.UTF8, true))
            {
                writer.Write(encoded.Length + 10); writer.Write(encoded);
            }
            stream.Position = 0;
            using var reader = new BinaryReader(stream);
            Reject(() => ColonySaveEnvelope.ReadLengthPrefixed(reader, 744825, 1, CatalogHash()));
        });
        FluidRouterTests.Run(Test, Check, Reject);
        FluidFrameCoordinatorTests.Run(Test, Check, Reject);
        MixtureRuntimeTests.Run(Test, Check, Reject);
        if (benchmark) FluidFrameCoordinatorTests.Benchmark();
        Console.WriteLine($"{passed}/{passed} snapshot, persistence and message boundary scenarios passed against actual ONI declarations.");
        return 0;
    }

    private static void Test(string name, System.Action action) { action(); ++passed; Console.WriteLine("PASS " + name); }
    private static void Check(bool condition) { if (!condition) throw new InvalidOperationException("Assertion failed"); }
    private static void Reject(System.Action action)
    {
        try { action(); } catch (ArgumentException) { return; } catch (InvalidDataException) { return; }
        catch (InvalidOperationException) { return; } catch (NotSupportedException) { return; }
        catch (EndOfStreamException) { return; }
        throw new InvalidOperationException("Expected a rejection");
    }

    private static byte[] CatalogHash()
    {
        var result = new byte[32]; for (var index = 0; index < result.Length; ++index) result[index] = (byte)(index + 1);
        return result;
    }

    private static byte[] NativePayload()
    {
        var native = new byte[29 + 36 * 3 * 4];
        System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(native, 0);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(8), 15);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(12), 3);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(16), 4);
        return native;
    }

    internal sealed class NativeFixture : IDisposable
    {
        private readonly List<(IntPtr pointer, int bytes)> buffers = new();
        private IntPtr snapshot;
        internal Sim.GameDataUpdate* Pointer => (Sim.GameDataUpdate*)snapshot;
        internal NativeFixture()
        {
            snapshot = Marshal.AllocHGlobal(sizeof(Sim.GameDataUpdate));
            *Pointer = default;
            Pointer->numFramesProcessed = 2;
            var type = typeof(Sim.GameDataUpdate);
            foreach (var field in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                if (!field.FieldType.IsPointer && field.FieldType != typeof(IntPtr)) continue;
                var countName = "num" + char.ToUpperInvariant(field.Name[0]) + field.Name.Substring(1);
                var countField = type.GetField(countName);
                var count = countField == null ? 6 : 2;
                if (countField != null) *(int*)((byte*)Pointer + (int)Marshal.OffsetOf<Sim.GameDataUpdate>(countName)) = count;
                var stride = field.FieldType.IsPointer ? Marshal.SizeOf(field.FieldType.GetElementType()!) : 1;
                if (field.Name == nameof(Sim.GameDataUpdate.propertyTextureFlow)) count *= 8;
                if (field.Name is nameof(Sim.GameDataUpdate.propertyTextureLiquid) or nameof(Sim.GameDataUpdate.propertyTextureLiquidData)
                    or nameof(Sim.GameDataUpdate.propertyTextureMaterialData)) count *= 4;
                var bytes = checked(stride * count);
                var buffer = Marshal.AllocHGlobal(bytes);
                for (var index = 0; index < bytes; ++index) ((byte*)buffer)[index] = (byte)(buffers.Count * 7 + index + 1);
                buffers.Add((buffer, bytes));
                *(IntPtr*)((byte*)Pointer + (int)Marshal.OffsetOf<Sim.GameDataUpdate>(field.Name)) = buffer;
            }
        }
        internal void Overwrite()
        {
            foreach (var buffer in buffers) new Span<byte>((void*)buffer.pointer, buffer.bytes).Fill(0xff);
        }
        public void Dispose()
        {
            foreach (var buffer in buffers) Marshal.FreeHGlobal(buffer.pointer);
            buffers.Clear();
            if (snapshot != IntPtr.Zero) Marshal.FreeHGlobal(snapshot);
            snapshot = IntPtr.Zero;
        }
    }
}
