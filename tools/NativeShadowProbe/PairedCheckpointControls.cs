using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using MixedAtmospheres.Oni;

internal static unsafe partial class Program
{
    private static void VerifyPairedCheckpoint(NativeSim sim,Contract contract,OniNativeObserver observer,OniNativeDomain.Status source,Dictionary<string,object?> report)
    {
        Require(OniNativeDomain.PairVersion()==1 && Marshal.SizeOf<OniNativeDomain.PairStatus>()==112,"Paired checkpoint ABI differs.");
        var before=observer.Capture().Hooks.Single(hook=>hook.Name=="begin-save").Entries;
        var physical=ReadPhysicalCheckpoint(source.Session,source.CompletedFrames);
        var pending=OniNativeDomain.PairStatus.Empty;
        Require(OniNativeDomain.RequestPair(source.Session,source.CompletedFrames-1,0,0,ref pending)==3 && pending.Token==0,
            "Stale pair request changed output or was admitted.");
        Require(OniNativeDomain.RequestPair(source.Session,source.CompletedFrames,0,0,ref pending)==0 && pending.Token!=0 && pending.State==1,
            "Paired checkpoint request failed.");
        var token=pending.Token;
        try
        {
            var duplicate=OniNativeDomain.PairStatus.Empty;
            Require(OniNativeDomain.RequestPair(source.Session,source.CompletedFrames,0,0,ref duplicate)==3 && duplicate.Token==0,
                "Duplicate pair request replaced a retained token.");
            // Explicit lifecycle synchronization; physical time must stay fixed.
            sim.Send(-775326397,Frame(0));
            var timer=Stopwatch.StartNew(); var ready=OniNativeDomain.PairStatus.Empty;
            do
            {
                sim.PrepareWithoutFrame(contract);
                Require(OniNativeDomain.PollPair(token,ref ready)==0,"Worker paired capture failed.");
            } while (ready.State==1 && timer.ElapsedMilliseconds<3000);
            Require(ready.State==2 && ready.CompletedFrame==source.CompletedFrames && ready.Session==source.Session &&
                ready.PhysicalTime==source.TimeSeconds && ready.NativeEpoch>0 && ready.CaptureThread!=0 &&
                ready.Configuration.Size==40 && ready.Configuration.Version==2 && ready.Configuration.X==DomainX && ready.Configuration.Y==DomainY,
                "Paired capture did not finish at a coherent zero-time native worker barrier.");
            var native=new byte[checked((int)ready.NativeBytes)]; var restored=new byte[checked((int)ready.PhysicalBytes)];
            var small=Enumerable.Repeat((byte)0xa5,16).ToArray();
            Require(OniNativeDomain.CopyPair(token,small,8,small,8)==5 && small.All(value=>value==0xa5),"Short paired buffers changed output.");
            var alias=Enumerable.Repeat((byte)0xa5,Math.Max(native.Length,restored.Length)).ToArray();
            fixed(byte* pointer=alias)
                Require(OniNativeDomain.CopyPairPointers(token,(IntPtr)pointer,(uint)alias.Length,(IntPtr)pointer,(uint)alias.Length)==1 && alias.All(value=>value==0xa5),
                    "Overlapping paired output buffers were accepted or changed.");
            Require(OniNativeDomain.CopyPair(token,native,(uint)native.Length,restored,(uint)restored.Length)==0 && restored.SequenceEqual(physical),
                "Paired physical image differs from its unchanged completed source.");
            Require(native.Length==29+36*66*66 && native.AsSpan(0,8).SequenceEqual("SIMSAVE\0"u8),"Paired native image framing differs.");
            var nativeHash=Convert.ToHexStringLower(SHA256.HashData(native)); var physicalHash=Convert.ToHexStringLower(SHA256.HashData(restored));
            native[0]^=1; restored[0]^=1;
            Require(OniNativeDomain.CopyPair(token,native,(uint)native.Length,restored,(uint)restored.Length)==0 &&
                Convert.ToHexStringLower(SHA256.HashData(native))==nativeHash && Convert.ToHexStringLower(SHA256.HashData(restored))==physicalHash,
                "Caller mutation replaced the retained immutable paired images.");
            Require(observer.Capture().Hooks.Single(hook=>hook.Name=="begin-save").Entries==before+1,
                "Polling/copying a paired token repeated native saving.");
            var current=OniNativeDomain.Status.Empty;
            Require(OniNativeDomain.Copy(ref current,new OniNativeDomain.Cell[6],6)==0 && current.CompletedFrames==source.CompletedFrames &&
                current.TimeSeconds==source.TimeSeconds,"Paired lifecycle capture advanced custom physical time.");
            report["pairedCheckpointVerified"]=true;
            report["pairedCheckpoint"]=new {ready.Token,ready.Session,ready.CompletedFrame,ready.NativeEpoch,ready.CaptureThread,
                ready.PhysicalTime,ready.NativeBytes,ready.PhysicalBytes,NativeSha256=nativeHash,PhysicalSha256=physicalHash,OrdinaryGameSaveIntegrated=false};
        }
        finally { Require(OniNativeDomain.ReleasePair(token)==0 && OniNativeDomain.ReleasePair(token)==0,"Paired token release was not idempotent."); }
        var retired=OniNativeDomain.PairStatus.Empty;
        Require(OniNativeDomain.PollPair(token,ref retired)==1 && retired.Token==0,"Released paired token remained readable.");
    }
}
