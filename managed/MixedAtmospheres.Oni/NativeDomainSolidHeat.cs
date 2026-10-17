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
        private static SolidHeatEndpoint solidHeatLastContact;
        private static SolidCellState solidHeatCompletedCell;
        private static int solidHeatPublishedNativeFrames;
        private static bool IsSolidHeatBoundary(int dx,int dy)=>dx== -1 && dy==0 &&
            (SolidHeatMode || (ColonyRestoreMode && Grid.Element[Grid.XYToCell(x-1,y)].id==SimHashes.Granite));
        private static void RegisterNativeSolidHeat(Sim.GameDataUpdate* source)
        {
            Require(solidHeatBindings.Count==0,"Native solid contacts were registered twice.");
            var position=Grid.XYToCell(x-1,y);
            var element=ElementLoader.elements[source->elementIdx[position]];
            if(element.id!=SimHashes.Granite) {Require(!SolidHeatMode,"Solid heat scene has no native granite contact.");return;}
            Require(SolidHeatVersion()==1 && Marshal.SizeOf<SolidHeatCell>()==48 && Marshal.SizeOf<SolidHeatEndpoint>()==64 &&
                Marshal.SizeOf<SolidCellState>()==64,"Unsupported native solid contact ABI.");
            Require(element.IsSolid && source->mass[position]==10f && source->temperature[position]>=1 &&
                source->temperature[position]<=1e6 && element.specificHeatCapacity>0 && element.thermalConductivity>0,
                "Native solid contact material, mass or thermal metadata differs from the admitted scene.");
            var input=new SolidHeatCell {Size=48,Version=1,PublicCell=checked((uint)position),NativeElementIndex=source->elementIdx[position],
                Session=latest.Session,ExpectedFrame=latest.CompletedFrames,MassKg=source->mass[position],
                SpecificHeatKJPerKgK=element.specificHeatCapacity,ConductivityWPerMK=element.thermalConductivity,
                NativeInsulation=source->insulation[position]};
            var result=ChunkHeatStatus.Empty;Check(ConfigureSolidHeat(new[]{input},1,ref result));
            Require(result.Session==latest.Session && result.CompletedFrame==latest.CompletedFrames && result.Contacts==0,
                "Native solid registration advanced or changed its completed source.");
            solidHeatBindings.Add(input);solidHeatInitialTemperatures.Add(position,source->temperature[position]);solidHeatStatus=result;
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
            solidHeatLastContact=endpoint;
            var cell=SolidCellState.Empty;Check(CopySolidCellState(ref cell));
            Require(cell.Session==latest.Session && cell.State==latest.State && cell.Reserved==0 &&
                (latest.CompletedFrames==0 || cell.CompletedFrame==latest.CompletedFrames && cell.NativeTicket==latest.NativeTicket),
                "Completed native solid cell observation differs from its fluid frame.");
            solidHeatCompletedCell=cell;
        }
        private static void ObserveNativeSolidEndpoints(Sim.GameDataUpdate* source)
        {
            if(solidHeatBindings.Count==0 || source->numFramesProcessed<=0) return;
            solidHeatPublishedNativeFrames=source->numFramesProcessed;
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
            solidHeatObservedLoss=loss;solidHeatObservedResidual=loss-solidHeatStatus.BodyHeatLossJoules;
            solidHeatObservedBound=128*2.2204460492503131e-16*(scale+Math.Abs(loss)+Math.Abs(solidHeatStatus.BodyHeatLossJoules));
            solidHeatObservedFrame=latest.CompletedFrames;
            solidHeatEndpointEvidence=endpoints.ToArray();
            solidHeatEndpointsVerified=Math.Abs(solidHeatObservedResidual)<=solidHeatObservedBound;
            Require(solidHeatEndpointsVerified,"Published native solid temperature does not close its fluid boundary heat account.");
        }
        private static void VerifyNativeSolidHeatCompletion()
        {
            if(solidHeatBindings.Count==0) {Require(!SolidHeatMode,"Solid heat validation has no admitted native contacts.");return;}
            Require(solidHeatStatus.Contacts>0 && solidHeatStatus.Transfers>0 && solidHeatStatus.BodyHeatLossJoules>0 &&
                solidHeatEndpointsVerified && solidHeatObservedFrame==latest.CompletedFrames,
                "Native solid did not cool into the completed fluid owner through actual temperature-pair contacts.");
        }
        private static void ResetNativeSolidHeat()
        {
            solidHeatBindings.Clear();solidHeatInitialTemperatures.Clear();solidHeatStatus=default;
            solidHeatObservedLoss=solidHeatObservedResidual=solidHeatObservedBound=0;solidHeatObservedFrame=0;solidHeatEndpointsVerified=false;
            solidHeatEndpointEvidence=Array.Empty<object>();
            solidHeatLastContact=default;solidHeatCompletedCell=default;solidHeatPublishedNativeFrames=0;
        }
        private static object NativeSolidHeatReport()=>new {
            Required=SolidHeatMode || solidHeatBindings.Count>0,AbiVersion=1,Source="actual-native-UpdateTemperature-cell-pair",
            ConfiguredCells=solidHeatBindings.ToArray(),Status=solidHeatStatus,ObservedFrame=solidHeatObservedFrame,
            Endpoints=solidHeatEndpointEvidence,
            PublishedNativeFramesProcessed=solidHeatPublishedNativeFrames,
            CompletedNativeCell=new {solidHeatCompletedCell.Size,solidHeatCompletedCell.Version,solidHeatCompletedCell.State,
                solidHeatCompletedCell.SolidPublicCell,solidHeatCompletedCell.Session,solidHeatCompletedCell.CompletedFrame,
                OldTemperatureK=NativeBinary32.ToDouble(solidHeatCompletedCell.OldTemperatureK),
                UpdatedTemperatureK=NativeBinary32.ToDouble(solidHeatCompletedCell.UpdatedTemperatureK),
                solidHeatCompletedCell.NativeElementIndex,MassKg=NativeBinary32.ToDouble(solidHeatCompletedCell.MassKg),
                solidHeatCompletedCell.NativeInsulation,solidHeatCompletedCell.NativeTicket},
            LastContact=new {solidHeatLastContact.Size,solidHeatLastContact.Version,solidHeatLastContact.State,
                solidHeatLastContact.Session,solidHeatLastContact.CompletedFrame,solidHeatLastContact.ContactFrame,
                solidHeatLastContact.SolidPublicCell,solidHeatLastContact.FluidPublicCell,
                NativeOldTemperatureK=NativeBinary32.ToDouble(solidHeatLastContact.NativeOldTemperatureK),
                BeforeTemperatureK=NativeBinary32.ToDouble(solidHeatLastContact.BeforeTemperatureK),
                AfterTemperatureK=NativeBinary32.ToDouble(solidHeatLastContact.AfterTemperatureK),
                Seconds=NativeBinary32.ToDouble(solidHeatLastContact.Seconds)},
            ObservedSolidHeatLossJoules=solidHeatObservedLoss,ObservedBalanceResidualJoules=solidHeatObservedResidual,
            ObservedBalanceBoundJoules=solidHeatObservedBound,EndpointsVerified=solidHeatEndpointsVerified,
            CapacityModel="native-mass-times-native-specific-heat-without-binary32-product-rounding",
            ConductivityModel="harmonic-conductivity-with-squared-native-insulation-fractions",
            NativeOldTemperatureGridPreserved=true,WholeColonySolidHeat=false};
    }
}
