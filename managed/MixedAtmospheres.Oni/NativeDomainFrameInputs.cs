using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static partial class NativeSealedDomainValidation
    {
        private static Configuration replayGeometry;
        private static readonly List<object> replayInputEvidence=new List<object>();
        private static readonly List<string> replayPhysicalInputs=new List<string>();
        private static readonly List<object> replayDiffusionBoundaries=new List<object>();
        private static string PhysicalInputTranscriptHash()=>System.Text.Encoding.ASCII.GetString(NativeFrameInputJournal.Digest(
            System.Text.Encoding.ASCII.GetBytes(string.Concat(replayPhysicalInputs))));
        private static int replayInputFrames,replayInputHeatBatches,replayInputTransferBatches,replayActorRefreshFrames;
        internal static void BeforeGasDiffusionChange()
        {
            // Verify any newly completed frame before configuration invalidates
            // that frame's immutable source/journal cache.
            if(colonyLoaded && !completed) VerifyRestoredPhysicalFrame();
        }
        internal static void AfterGasDiffusionChange(uint mode,double multiplier)
        {
            if(!colonyLoaded || completed) return;
            Require(latest.CompletedFrames==replayFrame && latest.TimeSeconds==replayTime,
                "Diffusion configuration changed outside the observed continuation boundary.");
            Require(TryCapturePhysicalCheckpoint(out var configured),"Diffusion configuration did not retain a drained continuation image.");
            // Explicit configuration begins a new reference segment at the
            // same physical time. Retain its source so the transition is auditable.
            var file="native-diffusion-boundary-"+latest.Session+"-"+latest.CompletedFrames+"-"+(replayDiffusionBoundaries.Count+1)+".bin";
            File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory,file),configured);
            replayDiffusionBoundaries.Add(new {latest.Session,CompletedFrame=latest.CompletedFrames,PhysicalTime=latest.TimeSeconds,
                Mode=mode,Multiplier=multiplier,SourceFile=file,SourceSha256=CheckpointHash(configured)});
            restoredExpected=configured;
        }
        private static byte[] ReadCompletedPhysicalFrame()
        {
            Check(CopyCompletedSnapshot(latest.Session,latest.CompletedFrames,null,0,out var size));
            Require(size>32 && size<=1024U*1024*1024,"Completed physical frame length is invalid.");
            var result=new byte[checked((int)size)];
            Check(CopyCompletedSnapshot(latest.Session,latest.CompletedFrames,result,size,out var copied));
            Require(copied==size,"Completed physical frame changed during its copy.");return result;
        }
        private static byte[] ReplayCompletedFrameInputs(byte[] source)
        {
            Check(CopyFrameInputs(latest.Session,latest.CompletedFrames,null,0,out var size));
            Require(size>=232 && size<=NativeFrameInputJournal.MaximumBytes,"Completed frame input journal length is invalid.");
            var bytes=new byte[checked((int)size)];
            Check(CopyFrameInputs(latest.Session,latest.CompletedFrames,bytes,size,out var copied));
            Require(copied==size,"Completed frame input journal changed inside one frame.");
            var journal=NativeFrameInputJournal.Parse(bytes,source,latest.Session,replayFrame,replayTime,latest.LastTimestepSeconds,replayGeometry);
            byte[] result;
            if(journal.ReplaySupported)
            {
                Check(AdvanceDomainSnapshotWithInputs(source,(uint)source.Length,bytes,size,null,0,out var length));
                Require(length>32 && length<=1024U*1024*1024,"Frame input replay produced an invalid snapshot length.");
                result=new byte[checked((int)length)];
                Check(AdvanceDomainSnapshotWithInputs(source,(uint)source.Length,bytes,size,result,length,out copied));
                Require(copied==length,"Frame input replay did not produce a complete snapshot.");++replayInputFrames;
            }
            else
            {
                // Existing actor fixtures may refresh an unchanged binding.
                // The old exact image comparison still checks its physics.
                Require((journal.Flags&~NativeFrameInputJournal.ProductionAdvance)==1 && journal.HeatCommands==0 && journal.TransferCommands==0 && restoredActorImage!=null,
                    "Saved continuation contains actor changes that cannot yet be replayed with external inputs.");
                result=ResumePhysicalCheckpoint(source,latest.LastTimestepSeconds);++replayActorRefreshFrames;
            }
            replayInputHeatBatches+=journal.HeatBatches;replayInputTransferBatches+=journal.TransferBatches;
            var name="native-frame-inputs-"+latest.Session+"-"+latest.CompletedFrames+".bin";
            if(replayInputEvidence.Count==0)
                File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory,"native-frame-inputs-source-"+latest.Session+".bin"),source);
            File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory,name),bytes);
            replayPhysicalInputs.Add(journal.PhysicalInputsSha256);
            replayInputEvidence.Add(new {File=name,Bytes=size,Sha256=CheckpointHash(bytes),SourceSha256=CheckpointHash(source),
                latest.Session,SourceFrame=replayFrame,CompletedFrame=latest.CompletedFrames,SourceTime=replayTime,Seconds=latest.LastTimestepSeconds,
                journal.Flags,journal.HeatBatches,journal.TransferBatches,journal.HeatCommands,journal.TransferCommands,journal.ReplaySupported,
                journal.PhysicalInputsSha256});
            return result;
        }
        private static object FrameInputReplayReport()=>new
        {
            AbiVersion=1,CompletedSnapshotAbiVersion=1,ReplayedFrames=replayInputFrames,ActorRefreshFramesCompared=replayActorRefreshFrames,
            HeatBatches=replayInputHeatBatches,TransferBatches=replayInputTransferBatches,Journals=replayInputEvidence.ToArray(),
            PhysicalInputTranscriptSha256=PhysicalInputTranscriptHash(),
            DiffusionConfigurationBoundaries=replayDiffusionBoundaries.ToArray(),
            PredictsFutureGameCommands=false,ActorReconfigurationReplayImplemented=false
        };
    }
}
