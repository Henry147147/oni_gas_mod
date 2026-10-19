using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool SolidHeatMode=>ColonyRuntimeValidation.Settings?.Mode=="native-solid-heat-domain";
        private static readonly List<SolidHeatCell> solidHeatBindings=new List<SolidHeatCell>();
        private static readonly Dictionary<int,float> solidHeatInitialTemperatures=new Dictionary<int,float>();
        private static ChunkHeatStatus solidHeatStatus;
        private static double solidHeatObservedLoss,solidHeatObservedResidual,solidHeatObservedBound;
        private static bool solidHeatEndpointsVerified;
        private static ulong solidHeatObservedFrame;
        private static object[] solidHeatEndpointEvidence=Array.Empty<object>();
        private static int solidHeatPublishedNativeFrames;
        private static OniNativePublication.Info solidHeatPublication;
        private static ChunkHeatStatus solidHeatPublishedStatus;
        private static Status solidHeatPublishedDomain;
        private static SolidHeatEndpoint solidHeatPublishedEndpoint;
        private static SolidCellState solidHeatPublishedCell;
        private static Cell[] solidHeatPublishedCells=Array.Empty<Cell>();
        private static bool solidHeatPublicationReleased;
        private static ulong solidHeatUnboundPublications;
        private static bool NativeSolidHeatReadyToComplete=>solidHeatBindings.Count==0 ||
            solidHeatEndpointsVerified && solidHeatPublicationReleased && solidHeatObservedFrame==latest.CompletedFrames;
        private static bool IsSolidHeatBoundary(int dx,int dy)=>dx== -1 && dy==0 &&
            (SolidHeatMode || (ColonyRestoreMode && Grid.Element[Grid.XYToCell(x-1,y)].id==SimHashes.Granite));
        private static SolidHeatCell? NativeSolidHeatInput(Sim.GameDataUpdate* source)
        {
            Require(solidHeatBindings.Count==0,"Native solid contacts were registered twice.");
            var position=Grid.XYToCell(x-1,y);
            var element=ElementLoader.elements[source->elementIdx[position]];
            if(element.id!=SimHashes.Granite) {Require(!SolidHeatMode,"Solid heat scene has no native granite contact.");return null;}
            Require(SolidHeatVersion()==1 && Marshal.SizeOf<SolidHeatCell>()==48 && Marshal.SizeOf<SolidHeatEndpoint>()==64 &&
                Marshal.SizeOf<SolidCellState>()==64,"Unsupported native solid contact ABI.");
            Require(OniNativePublication.Version()==1 && Marshal.SizeOf<OniNativePublication.Info>()==64,
                "Native solid contacts require completed publication leases.");
            Require(element.IsSolid && source->mass[position]==10f && source->temperature[position]>=1 &&
                source->temperature[position]<=1e6 && element.specificHeatCapacity>0 && element.thermalConductivity>0,
                "Native solid contact material, mass or thermal metadata differs from the admitted scene.");
            return new SolidHeatCell {Size=48,Version=1,PublicCell=checked((uint)position),NativeElementIndex=source->elementIdx[position],
                Session=latest.Session,ExpectedFrame=latest.CompletedFrames,MassKg=source->mass[position],
                SpecificHeatKJPerKgK=element.specificHeatCapacity,ConductivityWPerMK=element.thermalConductivity,
                NativeInsulation=source->insulation[position]};
        }
        private static void BeginWithNativeSolidHeat(Sim.GameDataUpdate* source,ref Configuration configuration)
        {
            Require(SolidHeatStartupVersion()==1,"Native solid heat requires atomic boundary activation.");
            var input=NativeSolidHeatInput(source) ?? throw new InvalidOperationException("Missing native solid heat startup contact.");
            input.Session=0;input.ExpectedFrame=0;
            var result=ChunkHeatStatus.Empty;
            Check(BeginWithSolidHeat(ref configuration,new[]{input},1,ref latest,ref result));
            input.Session=latest.Session;input.ExpectedFrame=latest.CompletedFrames;
            AcceptNativeSolidHeat(source,input,result);
        }
        private static void RegisterNativeSolidHeat(Sim.GameDataUpdate* source)
        {
            var value=NativeSolidHeatInput(source);if(!value.HasValue) return;
            var input=value.Value;var result=ChunkHeatStatus.Empty;Check(ConfigureSolidHeat(new[]{input},1,ref result));
            AcceptNativeSolidHeat(source,input,result);
        }
        private static void AcceptNativeSolidHeat(Sim.GameDataUpdate* source,SolidHeatCell input,ChunkHeatStatus result)
        {
            Require(result.Session==latest.Session && result.CompletedFrame==latest.CompletedFrames && result.Contacts==0,
                "Native solid registration advanced or changed its completed source.");
            solidHeatBindings.Add(input);solidHeatInitialTemperatures.Add((int)input.PublicCell,source->temperature[input.PublicCell]);solidHeatStatus=result;
            solidHeatPublishedCells=new Cell[Width*Height];
        }
        private static void CaptureNativeSolidHeat()
        {
            if(solidHeatBindings.Count==0) return;
            var result=ChunkHeatStatus.Empty;Check(CopySolidHeat(ref result));
            Require(result.Session==latest.Session && result.CompletedFrame==latest.CompletedFrames && result.State==latest.State &&
                result.Reserved==0 && result.Transfers<=result.Contacts && result.Unresolved<=result.Contacts,
                "Native solid heat account differs from its completed fluid frame.");
            foreach(var value in new[]{result.BodyHeatLossJoules,result.StoredFluidHeatJoules,result.MaximumBalanceResidualJoules,
                result.MaximumBalanceBoundJoules,result.EntropyLowerBound,result.RequestedAbsoluteHeatJoules})
                Require(!double.IsNaN(value) && !double.IsInfinity(value),"Native solid heat account is nonfinite.");
            Require(result.MaximumBalanceResidualJoules>=0 && result.MaximumBalanceResidualJoules<=result.MaximumBalanceBoundJoules &&
                result.EntropyLowerBound>=0,"Native solid contact failed its closed heat account.");solidHeatStatus=result;
            var endpoint=SolidHeatEndpoint.Empty;Check(CopySolidHeatEndpoint(ref endpoint));
            Require(endpoint.Session==latest.Session && endpoint.CompletedFrame==latest.CompletedFrames && endpoint.Reserved==0 &&
                endpoint.State==latest.State && endpoint.ContactFrame<=latest.CompletedFrames,"Native solid endpoint observation differs from its owner frame.");
            var cell=SolidCellState.Empty;Check(CopySolidCellState(ref cell));
            Require(cell.Session==latest.Session && cell.State==latest.State && cell.Reserved==0 &&
                (latest.CompletedFrames==0 || cell.CompletedFrame==latest.CompletedFrames && cell.NativeTicket==latest.NativeTicket),
                "Completed native solid cell observation differs from its fluid frame.");
        }
        private static void ObserveNativeSolidEndpoints(Sim.GameDataUpdate* source)
        {
            if(solidHeatBindings.Count==0) return;
            solidHeatPublishedNativeFrames=source->numFramesProcessed;
            var publication=OniNativePublication.Info.Empty;
            var code=OniNativePublication.Acquire((IntPtr)source,ref publication);
            if(code==3) {
                // The update used to activate ownership predates the owner.
                // It has no owned heat journal; require a tagged publication
                // before accepting an endpoint or completing the control.
                ++solidHeatUnboundPublications;solidHeatEndpointsVerified=false;solidHeatObservedFrame=0;
                solidHeatPublicationReleased=false;solidHeatPublication=default;return;
            }
            Check(code);solidHeatPublication=publication;solidHeatPublicationReleased=false;
            try {
                var frame=Status.Empty;var heat=ChunkHeatStatus.Empty;
                var endpoint=SolidHeatEndpoint.Empty;var nativeCell=SolidCellState.Empty;
                Check(OniNativePublication.Copy(publication.Token,ref frame,solidHeatPublishedCells,(uint)solidHeatPublishedCells.Length,
                    ref heat,ref endpoint,ref nativeCell));
                Require(publication.State==1 && publication.Reserved==0 && publication.Reserved2==0 && publication.NativeEpoch>0 &&
                    publication.Session==latest.Session && publication.CompletedFrame<=latest.CompletedFrames &&
                    publication.Cells==solidHeatPublishedCells.Length && frame.Session==publication.Session &&
                    frame.CompletedFrames==publication.CompletedFrame && frame.TimeSeconds==publication.TimeSeconds &&
                    heat.Session==publication.Session && heat.CompletedFrame==publication.CompletedFrame && heat.State==1 &&
                    endpoint.Session==publication.Session && endpoint.CompletedFrame==publication.CompletedFrame &&
                    nativeCell.Session==publication.Session && nativeCell.CompletedFrame==publication.CompletedFrame,
                    "Native solid publication lease contains mismatched completed frames.");
                solidHeatPublishedDomain=frame;solidHeatPublishedStatus=heat;
                solidHeatPublishedEndpoint=endpoint;solidHeatPublishedCell=nativeCell;
                ObserveLeasedNativeSolidEndpoints(source,heat,publication.CompletedFrame);
            } finally {
                Check(OniNativePublication.Release(publication.Token));solidHeatPublicationReleased=true;
            }
        }
        private static void ObserveLeasedNativeSolidEndpoints(Sim.GameDataUpdate* source,ChunkHeatStatus heat,ulong frame)
        {
            double loss=0,scale=0;var endpoints=new List<object>();
            foreach(var input in solidHeatBindings) {
                var cell=checked((int)input.PublicCell);var before=NativeBinary32.ToDouble(solidHeatInitialTemperatures[cell]);
                var after=NativeBinary32.ToDouble(source->temperature[cell]);
                Require(source->elementIdx[cell]==input.NativeElementIndex &&
                    BitConverter.SingleToInt32Bits(source->mass[cell])==BitConverter.SingleToInt32Bits(input.MassKg) &&
                    source->insulation[cell]==input.NativeInsulation && after>=1 && after<=1e6,
                    "Native solid publication changed an admitted material, mass, insulation or temperature range.");
                var capacity=NativeBinary32.ToDouble(input.MassKg)*NativeBinary32.ToDouble(input.SpecificHeatKJPerKgK)*1000;
                loss+=capacity*(before-after);scale+=capacity*(Math.Abs(before)+Math.Abs(after));
                endpoints.Add(new {PublicCell=cell,BeforeTemperatureK=before,PublishedTemperatureK=after,
                    HeatCapacityJoulesPerKelvin=capacity,MassKg=NativeBinary32.ToDouble(input.MassKg),
                    SpecificHeatKJPerKgK=NativeBinary32.ToDouble(input.SpecificHeatKJPerKgK)});
            }
            solidHeatObservedLoss=loss;solidHeatObservedResidual=loss-heat.BodyHeatLossJoules;
            solidHeatObservedBound=128*2.2204460492503131e-16*(scale+Math.Abs(loss)+Math.Abs(heat.BodyHeatLossJoules));
            solidHeatObservedFrame=frame;
            solidHeatEndpointEvidence=endpoints.ToArray();
            solidHeatEndpointsVerified=Math.Abs(solidHeatObservedResidual)<=solidHeatObservedBound;
            Require(solidHeatEndpointsVerified,"Published native solid temperature does not close its fluid boundary heat account.");
        }
        private static void VerifyNativeSolidHeatCompletion()
        {
            if(solidHeatBindings.Count==0) {Require(!SolidHeatMode,"Solid heat validation has no admitted native contacts.");return;}
            Require(solidHeatStatus.Contacts>0 && solidHeatStatus.Transfers>0 && solidHeatStatus.BodyHeatLossJoules>0 &&
                NativeSolidHeatReadyToComplete && solidHeatPublishedDomain.NativeTicket==latest.NativeTicket &&
                solidHeatPublishedCells.SequenceEqual(cells),
                "Native solid did not cool into the completed fluid owner through actual temperature-pair contacts.");
        }
        private static void ResetNativeSolidHeat()
        {
            solidHeatBindings.Clear();solidHeatInitialTemperatures.Clear();solidHeatStatus=default;
            solidHeatObservedLoss=solidHeatObservedResidual=solidHeatObservedBound=0;solidHeatObservedFrame=0;solidHeatEndpointsVerified=false;
            solidHeatEndpointEvidence=Array.Empty<object>();
            solidHeatPublishedNativeFrames=0;
            solidHeatPublication=default;solidHeatPublishedStatus=default;solidHeatPublishedDomain=default;
            solidHeatPublishedEndpoint=default;solidHeatPublishedCell=default;
            solidHeatPublishedCells=Array.Empty<Cell>();solidHeatPublicationReleased=false;solidHeatUnboundPublications=0;
        }
        private static object NativeSolidHeatReport()=>new {
            Required=SolidHeatMode || solidHeatBindings.Count>0,AbiVersion=1,Source="actual-native-UpdateTemperature-cell-pair",
            ConfiguredCells=solidHeatBindings.ToArray(),Status=solidHeatPublishedStatus,ObservedFrame=solidHeatObservedFrame,
            LatestOwnerStatus=solidHeatStatus,
            Publication=new {solidHeatPublication.Size,solidHeatPublication.Version,solidHeatPublication.State,
                solidHeatPublication.Reserved,solidHeatPublication.Reserved2,solidHeatPublication.Token,solidHeatPublication.Session,
                solidHeatPublication.CompletedFrame,solidHeatPublication.NativeEpoch,solidHeatPublication.Cells,solidHeatPublication.TimeSeconds,
                LeaseReleased=solidHeatPublicationReleased,UnboundUpdatesObserved=solidHeatUnboundPublications,
                Source="inspected-native-GameData-copy-and-export",CellsMatchLatest=solidHeatObservedFrame==latest.CompletedFrames &&
                    solidHeatPublishedCells.Length>0 && solidHeatPublishedCells.SequenceEqual(cells)},
            Endpoints=solidHeatEndpointEvidence,
            PublishedNativeFramesProcessed=solidHeatPublishedNativeFrames,
            CompletedNativeCell=new {solidHeatPublishedCell.Size,solidHeatPublishedCell.Version,solidHeatPublishedCell.State,
                solidHeatPublishedCell.SolidPublicCell,solidHeatPublishedCell.Session,solidHeatPublishedCell.CompletedFrame,
                OldTemperatureK=NativeBinary32.ToDouble(solidHeatPublishedCell.OldTemperatureK),
                UpdatedTemperatureK=NativeBinary32.ToDouble(solidHeatPublishedCell.UpdatedTemperatureK),
                solidHeatPublishedCell.NativeElementIndex,MassKg=NativeBinary32.ToDouble(solidHeatPublishedCell.MassKg),
                solidHeatPublishedCell.NativeInsulation,solidHeatPublishedCell.NativeTicket},
            LastContact=new {solidHeatPublishedEndpoint.Size,solidHeatPublishedEndpoint.Version,solidHeatPublishedEndpoint.State,
                solidHeatPublishedEndpoint.Session,solidHeatPublishedEndpoint.CompletedFrame,solidHeatPublishedEndpoint.ContactFrame,
                solidHeatPublishedEndpoint.SolidPublicCell,solidHeatPublishedEndpoint.FluidPublicCell,
                NativeOldTemperatureK=NativeBinary32.ToDouble(solidHeatPublishedEndpoint.NativeOldTemperatureK),
                BeforeTemperatureK=NativeBinary32.ToDouble(solidHeatPublishedEndpoint.BeforeTemperatureK),
                AfterTemperatureK=NativeBinary32.ToDouble(solidHeatPublishedEndpoint.AfterTemperatureK),
                Seconds=NativeBinary32.ToDouble(solidHeatPublishedEndpoint.Seconds)},
            ObservedSolidHeatLossJoules=solidHeatObservedLoss,ObservedBalanceResidualJoules=solidHeatObservedResidual,
            ObservedBalanceBoundJoules=solidHeatObservedBound,EndpointsVerified=solidHeatEndpointsVerified,
            CapacityModel="native-mass-times-native-specific-heat-without-binary32-product-rounding",
            ConductivityModel="harmonic-conductivity-with-squared-native-insulation-fractions",
            NativeOldTemperatureGridPreserved=true,WholeColonySolidHeat=false};
    }
}
