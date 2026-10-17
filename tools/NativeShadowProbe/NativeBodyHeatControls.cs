using MixedAtmospheres.Oni;

internal static unsafe partial class Program
{
    private readonly record struct ChunkHeatSample(float Temperature,float DeltaKilojoules);
    private sealed class NativeBodyHeatControl
    {
        private const int Registration=103;
        private const float InitialTemperature=350,Mass=10;
        private int handle=-1;
        private bool registered;
        private double capacity,bodyLoss;
        private float lastTemperature=InitialTemperature;
        private int samples;
        private readonly List<object> evidence=new();
        internal NativeBodyHeatControl(NativeSim sim,Contract contract)
        {
            Require(OniNativeDomain.ChunkHeatVersion()==1,"Native body heat capability differs.");
            // The inspected element table expresses specific heat in kJ/kg/K.
            capacity=1000.0*(float)(Mass*BitConverter.ToSingle(contract.Elements,4+contract.Granite*164+12));
            Require(double.IsFinite(capacity) && capacity>0,"Native body heat capacity is invalid.");
            sim.Send(1445724082,WritePayload(writer=> {
                writer.Write(DomainCell(0));writer.Write(Registration);writer.Write(Mass);writer.Write(InitialTemperature);
                writer.Write(10f);writer.Write(.01f);writer.Write(0f);writer.Write((ushort)contract.Granite);writer.Write((ushort)0);
            }));
        }
        internal object PrimeRegistration(NativeSim sim,Contract contract,OniNativeDomain.Status expected)
        {
            var before=ReadPhysicalCheckpoint(expected.Session,expected.CompletedFrames);
            sim.Send(-775326397,DomainFrame(0));var registration=sim.PrepareAfterFrame(contract);
            foreach(var value in registration.ComponentEvents.Where(item=>item.Callback==Registration)) {
                Require(!registered && value.Handle!=-1,"Zero-time native body registration is invalid.");
                handle=value.Handle;registered=true;
            }
            Require(registered,"Zero-time native body registration did not publish its callback.");
            var index=handle & 0x00ffffff;
            Require(index<registration.ChunkHeatSamples.Length,"Registration output slot is missing.");
            var first=registration.ChunkHeatSamples[index];
            sim.Send(-775326397,DomainFrame(0));var initialized=sim.PrepareAfterFrame(contract);
            var state=OniNativeDomain.Status.Empty;var cells=new OniNativeDomain.Cell[6];
            Require(OniNativeDomain.Copy(ref state,cells,6)==0 && state.State==1 && state.Session==expected.Session &&
                state.CompletedFrames==expected.CompletedFrames && state.TimeSeconds==expected.TimeSeconds &&
                ReadPhysicalCheckpoint(state.Session,state.CompletedFrames).SequenceEqual(before),
                "Body registration lifecycle changed the physical frame or source image.");
            Require(index<initialized.ChunkHeatSamples.Length && initialized.ChunkHeatSamples[index].Temperature==InitialTemperature &&
                initialized.ChunkHeatSamples[index].DeltaKilojoules==0,
                "Zero-time lifecycle did not initialize the native body heat output.");
            return new {ZeroTimeUpdates=2,PhysicalSourceUnchanged=true,CompletedFrame=state.CompletedFrames,
                FirstTemperatureBits=BitConverter.SingleToInt32Bits(first.Temperature),
                FirstDeltaBits=BitConverter.SingleToInt32Bits(first.DeltaKilojoules),
                InitializedTemperatureBits=BitConverter.SingleToInt32Bits(initialized.ChunkHeatSamples[index].Temperature),
                InitializedDeltaBits=BitConverter.SingleToInt32Bits(initialized.ChunkHeatSamples[index].DeltaKilojoules)};
        }
        internal void Observe(Snapshot snapshot,OniNativeDomain.Status status)
        {
            foreach(var value in snapshot.ComponentEvents.Where(item=>item.Callback==Registration)) {
                Require(!registered && value.Handle!=-1,"Native body registered twice or returned an invalid handle.");
                handle=value.Handle;registered=true;
            }
            if(!registered) return;
            var index=handle & 0x00ffffff;
            Require(index<snapshot.ChunkHeatSamples.Length,"Native body temperature publication is missing.");
            var body=snapshot.ChunkHeatSamples[index];
            Require(float.IsFinite(body.Temperature) && body.Temperature<=lastTemperature && body.Temperature>250 &&
                float.IsFinite(body.DeltaKilojoules) && body.DeltaKilojoules<=0,"Native body heat publication is invalid.");
            var loss=capacity*((double)lastTemperature-body.Temperature);bodyLoss+=loss;lastTemperature=body.Temperature;++samples;
            var heat=OniNativeDomain.ChunkHeatStatus.Empty;
            Require(OniNativeDomain.CopyChunkHeatStatus(ref heat)==0 && heat.State==1 && heat.Session==status.Session &&
                heat.CompletedFrame==status.CompletedFrames && heat.MaximumBalanceResidualJoules<=heat.MaximumBalanceBoundJoules &&
                heat.BodyHeatLossJoules==bodyLoss,"Native body publication and completed fluid-boundary energy disagree.");
            var deltaBound=8*1.1920928955078125e-7*Math.Max(1,loss/1000);
            Require(Math.Abs(body.DeltaKilojoules+loss/1000)<=deltaBound,"Original native body deltaKJ publication differs from its represented heat loss.");
            evidence.Add(new {status.CompletedFrames,NativeTemperature=body.Temperature,body.DeltaKilojoules,BodyHeatLossJoules=bodyLoss,
                Heat=new {heat.Size,heat.Version,heat.State,heat.Reserved,heat.Session,heat.CompletedFrame,heat.Contacts,heat.Transfers,
                    heat.Unresolved,heat.Iterations,heat.BodyHeatLossJoules,heat.StoredFluidHeatJoules,heat.MaximumBalanceResidualJoules,
                    heat.MaximumBalanceBoundJoules,heat.EntropyLowerBound,heat.RequestedAbsoluteHeatJoules}});
        }
        internal object Finish(double initialEnergy,double initialScale,OniNativeDomain.Cell[] fluid)
        {
            Require(registered && samples>=50 && bodyLoss>0 && lastTemperature<InitialTemperature,"Actual native body/fluid contact did not execute.");
            var finalEnergy=fluid.Sum(cell=>cell.InternalEnergy+cell.ResolvedKineticEnergy);
            var residual=finalEnergy-initialEnergy-bodyLoss;
            var bound=128*2.2204460492503131e-16*(initialScale+fluid.Sum(cell=>Math.Abs(cell.InternalEnergy)+cell.ResolvedKineticEnergy)+Math.Abs(bodyLoss));
            Require(Math.Abs(residual)<=bound,"Actual native body/fluid closed U+K account failed.");
            return new {Verified=true,ActualLicensedNativeWorker=true,UnityGameTest=false,ActualWindowsOsEstablishedByThisProbe=false,
                Samples=samples,InitialTemperature,FinalTemperature=lastTemperature,HeatCapacityJoulesPerKelvin=capacity,
                BodyHeatLossJoules=bodyLoss,InitialFluidEnergyJoules=initialEnergy,FinalFluidEnergyJoules=finalEnergy,
                ClosedEnergyResidualJoules=residual,ClosedEnergyBoundJoules=bound,CompatibilityCellTemperatureUnchanged=true,Evidence=evidence};
        }
    }
}
