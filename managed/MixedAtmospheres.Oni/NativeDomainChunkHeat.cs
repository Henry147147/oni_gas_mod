using System;
using System.Runtime.InteropServices;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static ChunkHeatStatus chunkHeatStatus;
        private static void RequireChunkHeatCapability() => Require(ChunkHeatVersion()==1 && CompletedSnapshotVersion()==1 && Marshal.SizeOf<ChunkHeatStatus>()==112,
            "Unsupported native body/fluid heat contact ABI.");
        private static void CaptureChunkHeat()
        {
            var next=ChunkHeatStatus.Empty;Check(CopyChunkHeatStatus(ref next));
            Require(next.Session==latest.Session && next.CompletedFrame==latest.CompletedFrames && next.State==latest.State &&
                next.Transfers<=next.Contacts && next.Unresolved<=next.Contacts && next.Reserved==0,
                "Native body heat diagnostics differ from the completed physical frame.");
            foreach(var value in new[]{next.BodyHeatLossJoules,next.StoredFluidHeatJoules,next.MaximumBalanceResidualJoules,
                next.MaximumBalanceBoundJoules,next.EntropyLowerBound,next.RequestedAbsoluteHeatJoules})
                Require(!double.IsNaN(value) && !double.IsInfinity(value),"Native body heat diagnostic is nonfinite.");
            Require(next.MaximumBalanceResidualJoules>=0 && next.MaximumBalanceResidualJoules<=next.MaximumBalanceBoundJoules &&
                next.EntropyLowerBound>=0 && next.RequestedAbsoluteHeatJoules>=0,"Native body heat account failed.");
            chunkHeatStatus=next;
        }
        private static object ChunkHeatReport()=>new {
            AbiVersion=1, Source="native-element-chunk-contact", Status=chunkHeatStatus,
            Model="implicit-passive-native-binary32-body-to-full-EOS-cell",
            Conductivity="harmonic-body-and-phase-volume-weighted-fluid-times-native-contact-scales",
            UnresolvedContactsLeaveBothEndpointsUnchanged=true, CompleteMetabolicBodyModel=false
        };
    }
}
