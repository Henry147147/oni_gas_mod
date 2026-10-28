using System;
using System.Buffers.Binary;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // The physical profile a restored owner actually runs: its MACCLS01
    // version and, for game kernels (20-27), the saved gas heat mixing, liquid
    // law and solver caps. Read from the owner image itself, never from
    // generic config flags.
    internal readonly struct ColonyPhysicalProfile : IEquatable<ColonyPhysicalProfile>
    {
        internal readonly uint OwnerVersion, PhysicalVersion, LiquidLaw, PressureIterationCap, HeatIterationCap;
        internal readonly double GasHeatMixing;
        internal ColonyPhysicalProfile(uint owner, uint physical, uint law, uint pressure, uint heat, double mixing = 0)
        { OwnerVersion=owner;PhysicalVersion=physical;LiquidLaw=law;PressureIterationCap=pressure;HeatIterationCap=heat;GasHeatMixing=mixing; }
        public bool Equals(ColonyPhysicalProfile other) => OwnerVersion==other.OwnerVersion && PhysicalVersion==other.PhysicalVersion &&
            LiquidLaw==other.LiquidLaw && PressureIterationCap==other.PressureIterationCap && HeatIterationCap==other.HeatIterationCap &&
            BitConverter.DoubleToInt64Bits(GasHeatMixing)==BitConverter.DoubleToInt64Bits(other.GasHeatMixing);
        public override bool Equals(object? other) => other is ColonyPhysicalProfile value && Equals(value);
        public override int GetHashCode() => HashCode.Combine(OwnerVersion,PhysicalVersion,LiquidLaw,PressureIterationCap,HeatIterationCap,GasHeatMixing);
    }

    // Read the durable counter, never a potentially older publication status.
    // This is the existing owner framing (MACOLN01, native/src/oni_colony.cpp).
    internal static class ColonyCheckpointMetadata
    {
        private static readonly byte[] OwnerMagic={77,65,67,79,76,78,48,49}, PhysicalMagic={77,65,67,67,76,83,48,49};
        private const int TransferBytes=48, HeatBytes=24, TerrainBytes=8, FreezeBytes=16, DigestBytes=64;
        private const int GermClockBytes=18, GermEditBytes=16, GermAccountBytes=8+6*16;
        // Removed custom game element hashes; these are refusal markers only.
        internal static void RefuseRemovedNitrogen(ReadOnlySpan<byte> image)
        {
            LedgerOffset(image, out _, out var count, out var table);
            for (var k = 0U; k < count; ++k)
            {
                var hash = BinaryPrimitives.ReadInt32LittleEndian(image.Slice(table + (int)k * 168 + 8, 4));
                if (hash == -1109340048 || hash == 1078696428 || hash == 418600943)
                    throw new NotSupportedException("This save contains removed nitrogen content. Start a new colony with this R&D build.");
            }
        }

        internal static void RefuseUnboundedPressure(ReadOnlySpan<byte> image)
        {
            var version=Profile(image).PhysicalVersion;
            if(version>=20 && version<=23)
                throw new NotSupportedException("This R&D save uses the earlier pressure update, which can cause large temperature swings while digging. " +
                    "Start a new colony to use the corrected pressure update.");
        }

        internal static ulong CompletedFrames(ReadOnlySpan<byte> image)
        {
            var at=CountersOffset(image,out _);
            Skip(image,ref at,8);
            return BinaryPrimitives.ReadUInt64LittleEndian(image.Slice(at-8,8));
        }

        // Walks the complete owner framing to its physical image. Versions 20
        // (linear law 0, caps 200/200), 21–27 (saved law and caps at the
        // same offsets; 22/23/24 imply profile families 4/5/6, 25 is
        // profile 6 with boil-only phase change, 26/27 are profiles 7/8 with
        // either phase rule) report the game kernel;
        // older physical versions report zero law/caps.
        internal static uint OwnerVersion(ReadOnlySpan<byte> image) { CountersOffset(image,out var version); return version; }
        internal static uint CallbackRequestCount(ReadOnlySpan<byte> image)
        {
            LedgerOffset(image,out _,out _,out _);
            return BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(8,4))==13 ?
                BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(16,4)) : 0;
        }
        // The embedded MACCLS01 physical image.
        internal static ReadOnlySpan<byte> PhysicalImage(ReadOnlySpan<byte> image) => image.Slice(PhysicalOffset(image,out _,out var length),length);

        internal static ColonyPhysicalProfile Profile(ReadOnlySpan<byte> image)
        {
            var at=PhysicalOffset(image,out var version,out var length);
            var body=image.Slice(at,length);
            var p=8;var physicalVersion=U32(body,ref p);
            if(physicalVersion<20 || physicalVersion>27) return new ColonyPhysicalProfile(version,physicalVersion,0,0,0);
            var model=U32(body,ref p);Skip(body,ref p,model);
            // time, revision, width/height, spacing/depth, timestep, gravity,
            // conduction, trace threshold, stiffness, flow parts, tolerance;
            // then heat mixing; then sharpening, interface detail, phase relaxation.
            Skip(body,ref p,8+8+4+4+8+8+8+8+1+8+8+4+8);
            var mixingAt=p;Skip(body,ref p,8);
            var mixing=BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(body.Slice(mixingAt,8)));
            Skip(body,ref p,1+8+8);
            if(physicalVersion==20) return new ColonyPhysicalProfile(version,20,0,200,200,mixing);
            var law=U32(body,ref p);var pressure=U32(body,ref p);var heat=U32(body,ref p);
            return new ColonyPhysicalProfile(version,physicalVersion,law,pressure,heat,mixing);
        }

        private static int PhysicalOffset(ReadOnlySpan<byte> image,out uint version,out int length)
        {
            var at=CountersOffset(image,out version);
            Skip(image,ref at,7*8+7*8);
            foreach(var size in new[]{TransferBytes,HeatBytes,TerrainBytes,FreezeBytes})
            {var count=U32(image,ref at);Skip(image,ref at,(long)count*size);}
            if(version>=8)
            {
                Skip(image,ref at,8);
                var jobs=U32(image,ref at);
                for(var k=0U;k<jobs;++k)
                {
                    Skip(image,ref at,12);var passives=U32(image,ref at);Skip(image,ref at,(long)passives*16);
                    if(version>=9){var wire=U32(image,ref at);Skip(image,ref at,wire);}
                }
            }
            if(version>=10)
            {
                // World germs: mode, frames, clocks, queued edits, accounts, audit error.
                Skip(image,ref at,4+8);
                var clocks=U32(image,ref at);Skip(image,ref at,(long)clocks*GermClockBytes);
                var edits=U32(image,ref at);Skip(image,ref at,(long)edits*GermEditBytes);
                var accounts=U32(image,ref at);Skip(image,ref at,(long)accounts*GermAccountBytes);
                Skip(image,ref at,8);
            }
            if(version>=11)
            {
                var held=U32(image,ref at);var heldAt=at;Skip(image,ref at,held);
                if(held<12 || held>16*1024*1024 || !image.Slice(heldAt,8).SequenceEqual(new byte[]{77,65,80,82,69,67,48,49}))
                    throw new InvalidDataException("Invalid owner precision inventory framing.");
            }
            var lengthAt=at;Skip(image,ref at,8);
            var physical=BinaryPrimitives.ReadUInt64LittleEndian(image.Slice(lengthAt,8));
            if(physical<20 || physical!=(ulong)(image.Length-at-DigestBytes))
                throw new InvalidDataException("Colony checkpoint physical image length differs.");
            length=(int)physical;
            if(!image.Slice(at,8).SequenceEqual(PhysicalMagic)) throw new InvalidDataException("Missing physical checkpoint signature.");
            return at;
        }

        private static int LedgerOffset(ReadOnlySpan<byte> image,out uint version,out uint materials,out int materialTable)
        {
            if (image.Length < 12 || !image.Slice(0,8).SequenceEqual(OwnerMagic))
                throw new InvalidDataException("Missing colony checkpoint framing.");
            version=BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(8,4));
            var at=12;
            // Wrapper13 encloses a base wrapper 8..12 whose queue keeps
            // callback-owned requests: base version, request count, then the
            // base layout unchanged. Metadata reports the base framing.
            if(version==13)
            {
                if(image.Length<20) throw new InvalidDataException("Truncated colony checkpoint framing.");
                version=BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(12,4));
                var requests=BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(16,4));
                if(version<8 || version>12 || requests==0 || requests>1u<<20) throw new InvalidDataException("Invalid callback-owned request framing.");
                at=20;
            }
            // Wrapper12 selects the conservative owner boundary law and keeps
            // wrapper11's complete framing, including its precision block.
            else if(version<6 || version>12) throw new InvalidDataException("Unsupported colony checkpoint metadata version.");
            Skip(image,ref at,56); // MAOniColonyConfig
            materials=U32(image,ref at);materialTable=at;
            Skip(image,ref at,(long)materials*168);
            var cells=U32(image,ref at);Skip(image,ref at,cells);
            var blocking=U32(image,ref at);Skip(image,ref at,blocking);
            var reservoir=U32(image,ref at);Skip(image,ref at,(long)reservoir*32);
            return at;
        }
        private static int CountersOffset(ReadOnlySpan<byte> image,out uint version)
        {
            var at=LedgerOffset(image,out version,out var materials,out _);
            // Native long-double accounts are encoded as two binary64 terms.
            Skip(image,ref at,(version>=7?6L*materials+8:5L*materials+5)*16);
            if(version>=7) Skip(image,ref at,16); // trace counters
            return at;
        }
        // Actual admitted material, independent of unrelated source emissions.
        // Both binary64 terms retain the native canonical long-double account.
        internal static (double High,double Low) InjectedMass(ReadOnlySpan<byte> image,int elementHash)
        {
            var ledger=LedgerOffset(image,out _,out var materials,out var table);
            for(var k=0U;k<materials;++k)
                if(BinaryPrimitives.ReadInt32LittleEndian(image.Slice(checked(table+(int)k*168+8),4))==elementHash)
                {
                    // The runtime catalogue sorts by native index; saved input
                    // records retain their caller order.
                    var native=BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(checked(table+(int)k*168+12),4));var ordinal=0U;
                    for(var j=0U;j<materials;++j)
                        if(BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(checked(table+(int)j*168+12),4))<native)++ordinal;
                    var at=ledger;Skip(image,ref at,((long)materials+ordinal)*16);var value=at;Skip(image,ref at,16);
                    return (BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(image.Slice(value,8))),
                        BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(image.Slice(value+8,8))));
                }
            throw new InvalidDataException("The injected material is absent from the colony catalogue.");
        }
        private static uint U32(ReadOnlySpan<byte> image,ref int at)
        {var offset=at;Skip(image,ref at,4);return BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(offset,4));}
        private static void Skip(ReadOnlySpan<byte> image,ref int at,long bytes)
        {
            if(bytes<0 || bytes>image.Length-at) throw new InvalidDataException("Truncated colony checkpoint metadata.");
            at+=checked((int)bytes);
        }
    }
}
