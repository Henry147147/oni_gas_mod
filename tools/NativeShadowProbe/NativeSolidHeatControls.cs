using MixedAtmospheres.Oni;

internal static unsafe partial class Program
{
    private sealed class NativeSolidHeatControl
    {
        private const float Mass=10;
        private readonly float InitialTemperature;
        private readonly int cell=DomainX-1+DomainY*Width;
        private readonly double capacity;
        private float temperature;
        private double bodyLoss;
        private int samples;
        private readonly List<object> evidence=new();
        internal NativeSolidHeatControl(NativeSim sim,Contract contract,OniNativeDomain.Status status)
        {
            var bits=Environment.GetEnvironmentVariable("MA_NATIVE_SOLID_TEMPERATURE_BITS");
            InitialTemperature=bits==null ? 400 : BitConverter.Int32BitsToSingle(int.Parse(bits,
                System.Globalization.NumberStyles.HexNumber,System.Globalization.CultureInfo.InvariantCulture));
            Require(float.IsFinite(InitialTemperature) && InitialTemperature>300 && InitialTemperature<=1e6,
                "Native solid control needs a finite initial temperature above its 300 K fluid.");
            temperature=InitialTemperature;
            Require(OniNativeDomain.SolidHeatVersion()==1,"Native solid heat capability differs.");
            void Replace(int position,int element,float mass,float kelvin)=>sim.Send(-1252920804,WritePayload(writer=> {
                writer.Write(position);writer.Write(-1);writer.Write(kelvin);writer.Write(mass);writer.Write(0);
                writer.Write((ushort)element);writer.Write((byte)1);writer.Write((byte)255);writer.Write((byte)1);writer.Write(new byte[3]);
            }));
            Replace(cell-1,contract.Unobtanium,20000,300);Replace(cell,contract.Granite,Mass,InitialTemperature);
            sim.Send(-775326397,DomainFrame(0));var snapshot=sim.PrepareAfterFrame(contract);
            var source=snapshot.CellData[cell];Require(source.Element==contract.Granite && source.Mass==Mass && source.Temperature==InitialTemperature,
                "Native solid source did not complete its zero-time material preparation.");
            var input=new OniNativeDomain.SolidHeatCell {Size=48,Version=1,PublicCell=(uint)cell,NativeElementIndex=(uint)contract.Granite,
                Session=status.Session,ExpectedFrame=status.CompletedFrames,MassKg=Mass,
                SpecificHeatKJPerKgK=BitConverter.ToSingle(contract.Elements,4+contract.Granite*164+12),
                ConductivityWPerMK=BitConverter.ToSingle(contract.Elements,4+contract.Granite*164+16),NativeInsulation=source.Insulation};
            capacity=(double)Mass*input.SpecificHeatKJPerKgK*1000;
            var result=OniNativeDomain.ChunkHeatStatus.Empty;
            Require(OniNativeDomain.ConfigureSolidHeat(new[]{input},1,ref result)==0 && result.Session==status.Session &&
                result.CompletedFrame==status.CompletedFrames && result.Transfers==0,"Native solid registration changed or rejected its zero-time source.");
        }
        internal void Observe(Snapshot snapshot,OniNativeDomain.Status status)
        {
            var current=snapshot.CellData[cell];
            Require(current.Mass==Mass && float.IsFinite(current.Temperature) && current.Temperature<=temperature && current.Temperature>300,
                "Actual native solid publication changed its mass or passive temperature ordering.");
            temperature=current.Temperature;bodyLoss=capacity*((double)InitialTemperature-temperature);++samples;
            var heat=OniNativeDomain.ChunkHeatStatus.Empty;
            Require(OniNativeDomain.CopySolidHeat(ref heat)==0 && heat.State==1 && heat.Session==status.Session &&
                heat.CompletedFrame==status.CompletedFrames && heat.MaximumBalanceResidualJoules<=heat.MaximumBalanceBoundJoules,
                "Native solid completed heat status differs from its fluid frame.");
            var bound=128*2.2204460492503131e-16*(capacity*(InitialTemperature+temperature)+Math.Abs(bodyLoss)+Math.Abs(heat.BodyHeatLossJoules));
            var native=OniNativeDomain.SolidCellState.Empty;
            Require(OniNativeDomain.CopySolidCellState(ref native)==0 && native.State==1 && native.Session==status.Session &&
                native.CompletedFrame==status.CompletedFrames && native.NativeTicket==status.NativeTicket && native.SolidPublicCell==cell &&
                native.NativeElementIndex==current.Element && native.MassKg==Mass && native.NativeInsulation==current.Insulation &&
                float.IsFinite(native.OldTemperatureK) && float.IsFinite(native.UpdatedTemperatureK),
                "Completed native solid array observation differs from its worker frame or material.");
            Require(Math.Abs(bodyLoss-heat.BodyHeatLossJoules)<=bound,"Actual native solid temperature lost a native contribution or its represented fluid heat: published="+
                (double)temperature+", nativeOld="+(double)native.OldTemperatureK+", nativeUpdated="+(double)native.UpdatedTemperatureK+
                ", observedLoss="+bodyLoss+", journalLoss="+heat.BodyHeatLossJoules+", frame="+status.CompletedFrames+".");
            evidence.Add(new {status.CompletedFrames,NativeTemperature=temperature,NativeMass=current.Mass,NativeInsulation=current.Insulation,
                ObservedBodyHeatLossJoules=bodyLoss,ObservedBalanceBoundJoules=bound,
                CompletedNativeCell=new {native.CompletedFrame,native.NativeTicket,
                    OldTemperatureK=(double)native.OldTemperatureK,UpdatedTemperatureK=(double)native.UpdatedTemperatureK},
                Heat=new {heat.Size,heat.Version,heat.State,heat.Reserved,heat.Session,heat.CompletedFrame,heat.Contacts,heat.Transfers,
                    heat.Unresolved,heat.Iterations,heat.BodyHeatLossJoules,heat.StoredFluidHeatJoules,heat.MaximumBalanceResidualJoules,
                    heat.MaximumBalanceBoundJoules,heat.EntropyLowerBound,heat.RequestedAbsoluteHeatJoules}});
        }
        internal object Finish(double initialEnergy,double initialScale,OniNativeDomain.Cell[] fluid)
        {
            var heat=OniNativeDomain.ChunkHeatStatus.Empty;Require(OniNativeDomain.CopySolidHeat(ref heat)==0,"Native solid final heat is unavailable.");
            Require(samples>=50 && heat.Contacts>=50 && heat.Transfers>=50 && bodyLoss>0 && temperature<InitialTemperature,
                "Actual licensed native temperature-pair contacts did not exchange represented solid heat.");
            var finalEnergy=fluid.Sum(c=>c.InternalEnergy+c.ResolvedKineticEnergy);var residual=finalEnergy-initialEnergy-heat.BodyHeatLossJoules;
            var bound=128*2.2204460492503131e-16*(initialScale+fluid.Sum(c=>Math.Abs(c.InternalEnergy)+c.ResolvedKineticEnergy)+Math.Abs(heat.BodyHeatLossJoules));
            Require(Math.Abs(residual)<=bound,"Actual native solid/fluid U+K boundary did not close.");
            return new {Verified=true,ActualLicensedNativeWorker=true,UnityGameTest=false,ActualWindowsOsEstablishedByThisProbe=false,
                Samples=samples,InitialTemperature,FinalTemperature=temperature,HeatCapacityJoulesPerKelvin=capacity,
                BodyHeatLossJoules=bodyLoss,ClosedEnergyResidualJoules=residual,ClosedEnergyBoundJoules=bound,
                NativeOldTemperatureGridPreserved=true,CompatibilityCellTemperatureUnchanged=true,Evidence=evidence};
        }
    }
}
