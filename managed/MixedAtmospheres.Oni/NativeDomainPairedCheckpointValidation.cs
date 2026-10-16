using System;
using System.Buffers.Binary;
using System.IO;
using System.Linq;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static partial class NativeSealedDomainValidation
    {
        private static bool PairedCheckpointMode => ColonyRuntimeValidation.Settings?.Mode == "native-paired-checkpoint-domain";
        private static PairStatus pairedStatus;
        private static ulong pairedToken, pairedRequestedFrame;
        private static bool pairedVerified, pairedNativeCellsVerified, pairedImmutableVerified, pairedReleaseVerified;
        private static int pairedEncodedBytes;
        private static string pairedNativeHash="",pairedPhysicalHash="",pairedEnvelopeHash="";

        private static void TickPairedCheckpointFixture()
        {
            if (pairedVerified) return;
            if (pairedToken==0)
            {
                var request=PairStatus.Empty;
                var code=RequestPair(latest.Session,latest.CompletedFrames,0,0,ref request);
                if (code==3) return;
                Check(code); Require(request.State==1 && request.Token!=0,"Paired checkpoint did not reserve a worker capture.");
                pairedToken=request.Token; pairedRequestedFrame=latest.CompletedFrames; pairedStatus=request; return;
            }
            var ready=PairStatus.Empty; Check(PollPair(pairedToken,ref ready));
            if (ready.State==1) return;
            if (ready.CompletedFrame>latest.CompletedFrames) return;
            Require(ready.State==2 && ready.Session==latest.Session && ready.CompletedFrame>=pairedRequestedFrame &&
                ready.CompletedFrame<=latest.CompletedFrames && ready.NativeEpoch>0 && ready.CaptureThread!=0 &&
                ready.Configuration.GridWidth==(uint)Grid.WidthInCells && ready.Configuration.X==(uint)x && ready.Configuration.Y==(uint)y &&
                ready.Configuration.Width==Width && ready.Configuration.Height==Height,"Paired source epoch/geometry differs from the game owner.");
            pairedStatus=ready;
            try
            {
                var native=new byte[checked((int)ready.NativeBytes)]; var physical=new byte[checked((int)ready.PhysicalBytes)];
                Check(CopyPair(pairedToken,native,ready.NativeBytes,physical,ready.PhysicalBytes));
                Require(ResumePhysicalCheckpoint(physical,0).SequenceEqual(physical),"Paired physical image did not validate/restore exactly.");
                var nativeWidth=BinaryPrimitives.ReadInt32LittleEndian(native.AsSpan(12));
                var nativeHeight=BinaryPrimitives.ReadInt32LittleEndian(native.AsSpan(16));
                Require(nativeWidth==Grid.WidthInCells+2 && nativeHeight==Grid.HeightInCells+2 && native.Length==29L+36L*nativeWidth*nativeHeight,
                    "Paired native image dimensions differ from the actual padded grid.");
                pairedNativeCellsVerified=Enumerable.Range(0,cells.Length).All(index=>
                {
                    var column=index%Width; var nativeIndex=x+column+1+(y+index/Width+1)*nativeWidth;
                    var offset=29+16*nativeIndex;
                    return BinaryPrimitives.ReadInt32LittleEndian(native.AsSpan(offset))==(int)SimHashes.Oxygen &&
                        BinaryPrimitives.ReadInt32LittleEndian(native.AsSpan(offset+4))==BitConverter.SingleToInt32Bits(ProjectionTemperature(column)) &&
                        BinaryPrimitives.ReadInt32LittleEndian(native.AsSpan(offset+8))==BitConverter.SingleToInt32Bits(ProjectionMass(column));
                });
                Require(pairedNativeCellsVerified,"Paired native image did not retain the exact challenged compatibility cells.");
                pairedNativeHash=CheckpointHash(native); pairedPhysicalHash=CheckpointHash(physical);
                native[0]^=1; physical[0]^=1;
                Check(CopyPair(pairedToken,native,ready.NativeBytes,physical,ready.PhysicalBytes));
                pairedImmutableVerified=CheckpointHash(native)==pairedNativeHash && CheckpointHash(physical)==pairedPhysicalHash;
                Require(pairedImmutableVerified,"Caller changes replaced the retained paired native/physical images.");
                var hash=Enumerable.Range(0,32).Select(i=>Convert.ToByte(catalog!.Fingerprint.Substring(2*i,2),16)).ToArray();
                byte[] payload;
                using (var stream=new MemoryStream())
                {
                    using (var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true))
                    {
                        writer.Write(System.Text.Encoding.ASCII.GetBytes("MAONID01")); writer.Write(1);
                        writer.Write(ready.Session); writer.Write(ready.CompletedFrame); writer.Write(ready.NativeEpoch); writer.Write(ready.PhysicalTime);
                        writer.Write(ready.Configuration.Size); writer.Write(ready.Configuration.Version); writer.Write(ready.Configuration.Flags);
                        writer.Write(ready.Configuration.GridWidth); writer.Write(ready.Configuration.X); writer.Write(ready.Configuration.Y);
                        writer.Write(ready.Configuration.Width); writer.Write(ready.Configuration.Height); writer.Write(ready.Configuration.Scenario); writer.Write(ready.Configuration.Reserved);
                        writer.Write(physical.Length); writer.Write(physical);
                    }
                    payload=stream.ToArray();
                }
                var envelope=ColonySaveEnvelope.Create(744825,2,ready.CompletedFrame,hash,native,payload);
                var encoded=envelope.CopyEncodedData(); var parsed=ColonySaveEnvelope.Parse(encoded,744825,2,hash);
                Require(parsed.CompletedFrame==ready.CompletedFrame && parsed.NativeData.SequenceEqual(native) && parsed.CustomData.SequenceEqual(payload),
                    "Paired checkpoint envelope did not retain both complete sections and their source frame.");
                pairedEncodedBytes=encoded.Length; pairedEnvelopeHash=CheckpointHash(encoded);
                File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory,"native-paired-checkpoint.bin"),encoded);
                Check(ReleasePair(pairedToken)); Check(ReleasePair(pairedToken)); pairedReleaseVerified=true; pairedToken=0;
                pairedVerified=true;
            }
            finally { if (pairedToken!=0) { Check(ReleasePair(pairedToken)); pairedToken=0; } }
        }
        private static object PairedCheckpointReport()=>new {
            Verified=pairedVerified, Native=pairedStatus, RequestedFrame=pairedRequestedFrame,
            NativeCompatibilityCellsVerified=pairedNativeCellsVerified, ImmutableCopiesVerified=pairedImmutableVerified, ReleaseVerified=pairedReleaseVerified,
            EncodedBytes=pairedEncodedBytes, NativeSha256=pairedNativeHash, PhysicalSha256=pairedPhysicalHash, EnvelopeSha256=pairedEnvelopeHash,
            OrdinaryGameSaveIntegrated=false, WholeColonyCheckpoint=false
        };
        private static void ReleasePairedCheckpointFixture()
        {
            if (pairedToken!=0) { Check(ReleasePair(pairedToken)); pairedToken=0; }
        }
    }
}
