using System.Security.Cryptography;
using MixedAtmospheres.Oni;

internal static unsafe partial class Program
{
    private sealed class FrameInputControl
    {
        private readonly OniNativeDomain.Configuration geometry;
        private byte[] source;
        private ulong frame;
        private double time;
        private int frames,heatBatches,heatCommands;
        private readonly List<object> evidence=new();
        private readonly bool pendingProbe=Environment.GetEnvironmentVariable("MA_VERIFY_COMPLETED_SNAPSHOT")=="1";
        private bool pendingProbeVerified;
        internal FrameInputControl(OniNativeDomain.Configuration config,OniNativeDomain.Status status)
        {
            Require(OniNativeDomain.FrameInputsVersion()==1,"Frame input ABI differs.");
            geometry=config;source=ReadPhysicalCheckpoint(status.Session,status.CompletedFrames);frame=status.CompletedFrames;time=status.TimeSeconds;
            var output=Enumerable.Repeat((byte)0x5a,256).ToArray();var held=(byte[])output.Clone();
            Require(OniNativeDomain.CopyFrameInputs(status.Session,0,output,256,out _)==3 && output.SequenceEqual(held),
                "Frame-zero journal read was accepted or changed output.");
        }
        internal void Observe(OniNativeDomain.Status status)
        {
            Require(status.CompletedFrames==frame+1,"Native frame-input control skipped an actual worker interval.");
            Require(OniNativeDomain.CopyFrameInputs(status.Session,status.CompletedFrames,null,0,out var bytes)==0 && bytes>=232 && bytes<=NativeFrameInputJournal.MaximumBytes,
                "Actual native worker journal size is invalid.");
            var journal=new byte[bytes];
            Require(OniNativeDomain.CopyFrameInputs(status.Session,status.CompletedFrames,journal,bytes,out var copied)==0 && copied==bytes,
                "Actual completed native journal did not copy coherently.");
            var parsed=NativeFrameInputJournal.Parse(journal,source,status.Session,frame,time,status.LastTimestepSeconds,geometry);
            Require(parsed.ReplaySupported && parsed.TransferCommands==0,"Headless heat control has unsupported actor or mass inputs.");
            Require(OniNativeDomain.AdvanceDomainSnapshotWithInputs(source,(uint)source.Length,journal,bytes,null,0,out var length)==0 && length>32 && length<=1<<20,
                "Actual native journal could not resume its source image.");
            var predicted=new byte[length];
            Require(OniNativeDomain.AdvanceDomainSnapshotWithInputs(source,(uint)source.Length,journal,bytes,predicted,length,out copied)==0 && copied==length,
                "Actual native input replay output is incomplete.");
            Require(OniNativeDomain.CompletedSnapshotVersion()==1,"Completed frame snapshot capability differs.");
            byte[] CompletedImage()
            {
                Require(OniNativeDomain.CopyCompletedSnapshot(status.Session,status.CompletedFrames,null,0,out var size)==0 && size>32,
                    "Completed native frame image size is invalid.");
                var result=new byte[size];
                Require(OniNativeDomain.CopyCompletedSnapshot(status.Session,status.CompletedFrames,result,size,out var copiedImage)==0 && copiedImage==size,
                    "Completed native frame image changed while copying.");return result;
            }
            var actual=CompletedImage();
            Require(actual.SequenceEqual(predicted),"Accepted heat commands differ from the actual next licensed native worker frame.");
            evidence.Add(new {SourceFrame=frame,CompletedFrame=status.CompletedFrames,Bytes=bytes,
                Sha256=Convert.ToHexStringLower(SHA256.HashData(journal)),SourceSha256=Convert.ToHexStringLower(SHA256.HashData(source)),
                ActualSha256=Convert.ToHexStringLower(SHA256.HashData(actual)),parsed.HeatBatches,parsed.HeatCommands});
            ++frames;heatBatches+=parsed.HeatBatches;heatCommands+=parsed.HeatCommands;
            source=actual;frame=status.CompletedFrames;time=status.TimeSeconds;
            if(pendingProbe && !pendingProbeVerified && frames==10)
            {
                var command=new OniNativeDomain.HeatCommand {Size=48,Version=1,Session=status.Session,ExpectedFrame=status.CompletedFrames,
                    PublicCell=(uint)DomainCell(0),Source=901,Joules=0,MaximumTemperature=1e6};
                var heat=OniNativeDomain.HeatStatus.Empty;
                Require(OniNativeDomain.QueueHeat(new[]{command},1,ref heat)==0 && heat.PendingBatches==1,
                    "Future command was not reserved for the completed-frame control.");
                var buffer=Enumerable.Repeat((byte)0x5a,64).ToArray();
                Require(OniNativeDomain.CopyDomainSnapshot(status.Session,status.CompletedFrames,buffer,64,out _)==3 && buffer.All(x=>x==0x5a) &&
                    CompletedImage().SequenceEqual(actual),"Future inputs changed the completed image or bypassed the drained save guard.");
                Require(OniNativeDomain.CopyCompletedSnapshot(status.Session,status.CompletedFrames,buffer,1,out _)==5 && buffer.All(x=>x==0x5a),
                    "Insufficient completed-frame buffer changed output.");
                Require(OniNativeDomain.CopyCompletedSnapshot(status.Session+1,status.CompletedFrames,buffer,64,out _)==3 && buffer.All(x=>x==0x5a),
                    "Stale completed-frame epoch changed output.");pendingProbeVerified=true;
            }
        }
        internal object Finish(bool heating)
        {
            Require(frames>=60 && (!pendingProbe || pendingProbeVerified) && heatBatches==((heating ? 1 : 0)+(pendingProbe ? 1 : 0)) &&
                heatCommands==((heating ? 2 : 0)+(pendingProbe ? 1 : 0)),
                "Headless input replay did not observe the exact accepted heat batch and all worker frames.");
            return new {Verified=true,Frames=frames,HeatBatches=heatBatches,HeatCommands=heatCommands,PendingCompletedFrameControl=pendingProbeVerified,Evidence=evidence,
                ActualLicensedNativeWorker=true,UnityGameTest=false,ActualWindowsOsEstablishedByThisProbe=false};
        }
        internal object FinishNativeBodyHeat()
        {
            Require(frames>=60 && heatBatches>0 && heatCommands==heatBatches,"Native body heat inputs were not captured in every worker replay.");
            return new {Verified=true,Frames=frames,HeatBatches=heatBatches,HeatCommands=heatCommands,Evidence=evidence,
                ActualLicensedNativeWorker=true,UnityGameTest=false,ActualWindowsOsEstablishedByThisProbe=false};
        }
    }
}
