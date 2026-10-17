using System.Runtime.InteropServices;
using MixedAtmospheres.Oni;

internal static unsafe partial class Program
{
    private const int DomainX = 20, DomainY = 35, DomainWidth = 3, DomainHeight = 2;
    private static bool IsSealedArenaCell(int x, int y) => x >= DomainX - 1 && x <= DomainX + DomainWidth &&
        y >= DomainY - 1 && y <= DomainY + DomainHeight;
    private static float DomainProjectionMass(int column, bool liquid) => liquid ? column == 0 ? 1000 : column == 1 ? 1200 : 1100 :
        column == 0 ? .6f : column == 1 ? 1.2f : 2.4f;
    private static int DomainCell(int index) => DomainX + index % DomainWidth + (DomainY + index / DomainWidth) * Width;
    private static float DomainProjectionTemperature(int column) => 280 + 20 * column;

    private static void VerifySealedDomain(NativeSim sim, Contract contract, Snapshot initial, Dictionary<string, object?> report, bool liquid, bool heating, bool gravity, bool checkpoint, bool paired, OniNativeObserver observer, bool smallLiquidHeat=false)
    {
        Require(OniNativeDomain.Version() == 2 && Marshal.SizeOf<OniNativeDomain.Status>() == 440, "Domain ABI mismatch.");
        var initialCells = Enumerable.Range(0, 6).Select(i => initial.CellData[DomainCell(i)]).ToArray();
        var snapshot = initial;
        Stage("native-unbalanced-flow-control");
        for (var i = 0; i < 12; ++i)
        {
            sim.Send(-775326397, Frame(.2f)); snapshot = sim.PrepareAfterFrame(contract);
        }
        Require(Enumerable.Range(0, 6).Any(i => snapshot.CellData[DomainCell(i)].Mass != initialCells[i].Mass),
            "Unowned sealed projection did not exercise native flow.");
        report["nativeFlowControlPassed"] = true;
        for (var i = 0; i < 6; ++i)
        {
            var cell = DomainCell(i);
            sim.Send(-469311643, WritePayload(writer =>
            { writer.Write(cell); writer.Write(-1); writer.Write((byte)3); writer.Write((byte)1); writer.Write((ushort)0); }));
        }
        sim.Send(-775326397, Frame(.2f)); snapshot = sim.PrepareAfterFrame(contract);
        Require(Enumerable.Range(0, 6).All(i => (snapshot.CellData[DomainCell(i)].Properties & 3) == 3), "Native flow flags were not installed.");
        var config = new OniNativeDomain.Configuration { Size = 40, Version = 2, Flags = 1, GridWidth = Width,
            X = DomainX, Y = DomainY, Width = DomainWidth, Height = DomainHeight, Scenario = liquid ? 1U : gravity ? 2U : 0U };
        var status = OniNativeDomain.Status.Empty;
        var wrongPitch = config; wrongPitch.GridWidth -= 1;
        Require(OniNativeDomain.Begin(ref wrongPitch, ref status) == 2 && status.State == 0 && status.Session == 0,
            "A public grid with the wrong native pitch was accepted or changed its output.");
        report["wrongNativePitchRejectedBeforeActivation"] = true;
        Require(OniNativeDomain.Begin(ref config, ref status) == 0, "Domain start failed: " + Marshal.PtrToStringAnsi(OniNativeDomain.LastError()));
        var premature=OniNativeDomain.PairStatus.Empty;
        Require(OniNativeDomain.RequestPair(status.Session,0,0,0,ref premature)==3 && premature.Token==0,
            "A never-advanced initial physical owner was admitted as a completed paired source.");
        var initialPhysical = new OniNativeDomain.Cell[6];
        Require(OniNativeDomain.Copy(ref status, initialPhysical, 6) == 0, "Initial physical domain did not copy.");
        var initialGravity = OniNativeDomain.GravityStatus.Empty;
        Require(OniNativeDomain.GravityVersion() == 1 && Marshal.SizeOf<OniNativeDomain.GravityStatus>() == 96 &&
            OniNativeDomain.CopyGravityStatus(ref initialGravity) == 0, "Gravity diagnostic ABI mismatch.");
        var initialEnergy = initialPhysical.Sum(cell => cell.InternalEnergy + cell.ResolvedKineticEnergy) + initialGravity.Potential;
        for (var i = 0; i < 6; ++i)
        {
            var cell = DomainCell(i); var mass = DomainProjectionMass(i % DomainWidth, liquid);
            sim.Send(-1252920804, WritePayload(writer =>
            {
                writer.Write(cell); writer.Write(-1); writer.Write(DomainProjectionTemperature(i % DomainWidth)); writer.Write(mass); writer.Write(0);
                writer.Write((ushort)(liquid ? contract.Water : contract.Oxygen)); writer.Write((byte)1); writer.Write((byte)255);
                writer.Write((byte)1); writer.Write(new byte[3]);
            }));
        }
        var values = new OniNativeDomain.Cell[6]; CellState[]? baseline = null; var primedAt = 0UL;
        var heatQueued = false; var heatBeforeEnergy = 0.0; var heatBeforeScale = 0.0;
        var heatFactor=liquid && !smallLiquidHeat ? 1000.0 : 1.0;
        byte[]? checkpointExpected = null; ulong checkpointFrame = 0; var checkpointVerified = false;
        var pairVerified=false;
        var trace = new List<object>(); report["nativeDomainFrames"] = trace;
        Stage("advance-sealed-native-domain");
        for (var frame = 0; frame < 75; ++frame)
        {
            sim.Send(-775326397, Frame(.2f)); snapshot = sim.PrepareAfterFrame(contract);
            status = OniNativeDomain.Status.Empty;
            Require(OniNativeDomain.Copy(ref status, values, 6) == 0 && status.State == 1, "Domain failed: " + status.Error);
            var source = Enumerable.Range(0, 6).Select(i => snapshot.CellData[DomainCell(i)]).ToArray();
            trace.Add(new { status.CompletedFrames, status.NativeTicket, status.TimeSeconds, status.SuppressedGas, status.SuppressedLiquid,
                status.SuppressedTemperature, status.SuppressedPostProcess, status.SolverThread, status.StartThread,
                status.CurrentOxygenKg, status.PressurePa, NativeMasses = source.Select(cell => cell.Mass).ToArray() });
            if (baseline == null && Enumerable.Range(0, 6).All(i => source[i].Mass == DomainProjectionMass(i % DomainWidth, liquid) &&
                source[i].Temperature == DomainProjectionTemperature(i % DomainWidth) &&
                source[i].Element == (liquid ? contract.Water : contract.Oxygen)))
            { baseline = source; primedAt = status.CompletedFrames; }
            Require(baseline != null || status.CompletedFrames <= 10, "Native projection bootstrap did not complete.");
            if (baseline == null) continue;
            for (var i = 0; i < 6; ++i) Require(source[i] == baseline[i], "Uncovered native writer changed cell " + DomainCell(i) +
                " at physical frame " + status.CompletedFrames + ": " + baseline[i] + " -> " + source[i]);
            Require(status.MaximumRelativeMassError <= 1e-8 && status.MaximumRelativeEnergyError <= 1e-6, "Custom conservation failed.");
            if (paired && !pairVerified && status.CompletedFrames-primedAt>=4)
            {
                VerifyPairedCheckpoint(sim,contract,observer,status,report); pairVerified=true;
            }
            if (checkpoint && !checkpointVerified && status.CompletedFrames-primedAt>=4)
            {
                if (checkpointExpected == null)
                {
                    Require(OniNativeDomain.CheckpointVersion()==1,"Physical checkpoint ABI mismatch.");
                    var image=ReadPhysicalCheckpoint(status.Session,status.CompletedFrames);
                    Require(ResumePhysicalCheckpoint(image,0).SequenceEqual(image),"Physical checkpoint validation changed its original bytes.");
                    checkpointFrame=status.CompletedFrames; checkpointExpected=ResumePhysicalCheckpoint(image,status.LastTimestepSeconds);
                    report["physicalCheckpointSourceFrame"]=checkpointFrame; report["physicalCheckpointBytes"]=image.Length;
                    report["physicalCheckpointSourceSha256"]=Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(image));
                }
                else
                {
                    var actual=ReadPhysicalCheckpoint(status.Session,status.CompletedFrames);
                    Require(status.CompletedFrames==checkpointFrame+1 && actual.SequenceEqual(checkpointExpected),
                        "Restored physical image differs from the actual next native worker frame.");
                    checkpointVerified=true; report["physicalCheckpointVerified"]=true;
                    report["physicalCheckpointResumedFrame"]=status.CompletedFrames;
                    report["physicalCheckpointExpectedSha256"]=Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(checkpointExpected));
                    report["physicalCheckpointActualSha256"]=Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(actual));
                }
            }
            if (heating && !heatQueued && status.CompletedFrames - primedAt >= 4)
            {
                heatBeforeEnergy = values.Sum(cell => cell.InternalEnergy + cell.ResolvedKineticEnergy);
                heatBeforeScale = values.Sum(cell => Math.Abs(cell.InternalEnergy) + cell.ResolvedKineticEnergy);
                var commands = new[] {
                    new OniNativeDomain.HeatCommand { Size = 48, Version = 1, Session = status.Session, ExpectedFrame = status.CompletedFrames,
                        PublicCell = (uint)DomainCell(0), Source = 1000, Joules = 2000*heatFactor, MaximumTemperature = 1000 },
                    new OniNativeDomain.HeatCommand { Size = 48, Version = 1, Session = status.Session, ExpectedFrame = status.CompletedFrames,
                        PublicCell = (uint)DomainCell(0), Source = 1001, Joules = -500*heatFactor, MaximumTemperature = 1000 } };
                var invalidCommands = (OniNativeDomain.HeatCommand[])commands.Clone(); invalidCommands[1].MaximumTemperature = 0;
                var result = OniNativeDomain.HeatStatus.Empty;
                Require(OniNativeDomain.QueueHeat(invalidCommands, 2, ref result) == 1 && result.AcceptedBatches == 0 && result.State == 0,
                    "Invalid heat batch partially queued or changed output.");
                Require(OniNativeDomain.QueueHeat(commands, 2, ref result) == 0 && result.PendingBatches == 1 && result.PendingHeatJoules == 1500*heatFactor,
                    "Valid heat batch was not reserved.");
                heatQueued = true;
            }
            if (status.CompletedFrames - primedAt < 60) continue;
            Require(!checkpoint || checkpointVerified,"Physical checkpoint continuation was not observed.");
            Require(status.SolverThread != status.StartThread && status.SuppressedTemperature > 0 && status.SuppressedPostProcess > 0,
                "Native worker/filter controls did not execute.");
            Require(snapshot.ColdTemperature > initial.ColdTemperature && snapshot.HotTemperature < initial.HotTemperature,
                "Native solid heat stopped.");
            Require(liquid ? values[0].Water == 1100 && Math.Abs(values[0].Pressure - 2.2e8) < 1 :
                status.CurrentOxygenKg != status.InitialOxygenKg, "Intended physical model did not execute.");
            if (heating)
            {
                var account = OniNativeDomain.HeatStatus.Empty;
                Require(OniNativeDomain.CopyHeat(ref account) == 0 && account.AcceptedBatches == 1 && account.CommittedBatches == 1 &&
                    account.PendingBatches == 0 && account.CommittedHeatJoules == 1500*heatFactor, "Native heat account did not commit exactly once.");
                var residual = values.Sum(cell => cell.InternalEnergy + cell.ResolvedKineticEnergy) - heatBeforeEnergy - 1500*heatFactor;
                var heatBound = liquid ? 128*2.2204460492503131e-16*(heatBeforeScale + values.Sum(cell => Math.Abs(cell.InternalEnergy) + cell.ResolvedKineticEnergy)) : 1e-6;
                report["heatCommandsQueued"] = heatQueued; report["heatBeforeEnergyJoules"] = heatBeforeEnergy;
                report["heatAfterEnergyJoules"] = values.Sum(cell => cell.InternalEnergy + cell.ResolvedKineticEnergy);
                report["heatEnergyResidualJoules"] = residual; report["heatEnergyRoundoffBoundJoules"] = heatBound;
                Require(heatQueued && Math.Abs(residual) <= heatBound, "Native physical energy did not retain the declared heat exchange.");
                report["heatCommandsPassed"] = true; report["heatEnergyResidualJoules"] = residual;
                report["heatEnergyRoundoffBoundJoules"] = heatBound;
                var thermal = OniNativeDomain.ThermalStatus.Empty;
                Require(OniNativeDomain.CopyThermalStatus(ref thermal) == 0 && thermal.Enabled == 1 && thermal.State == 1 &&
                    thermal.Session == status.Session && thermal.CompletedFrame == status.CompletedFrames &&
                    thermal.Stages == 2 * status.Substeps && thermal.Contacts > 0 &&
                    (smallLiquidHeat ? thermal.UnresolvedUpdates>0 && thermal.RedistributedEnergy==0 && thermal.EntropyChange==0 :
                        thermal.RedistributedEnergy>0 && thermal.EntropyChange>0),
                    "Worker-clock internal heat conduction did not produce coherent physical diagnostics.");
                report["internalConductionPassed"] = !smallLiquidHeat;
                report["smallLiquidHeatAccountingPassed"] = smallLiquidHeat;
                if(liquid && !smallLiquidHeat) Require(values[0].Temperature>300 && values.Skip(1).Any(cell=>cell.Temperature>300+1e-9),
                    "Liquid heat did not warm the source and a conductive neighbor.");
                report["thermalConduction"] = new { thermal.Size, thermal.Version, thermal.State, thermal.Enabled,
                    thermal.Session, thermal.CompletedFrame, thermal.Stages, thermal.Contacts, thermal.Iterations,
                    thermal.UnresolvedUpdates, thermal.RedistributedEnergy, thermal.EntropyChange };
            }
            report["sealedNativeDomainPassed"] = true; report["projectionPrimedAtFrame"] = primedAt;
            if (gravity)
            {
                var field = OniNativeDomain.GravityStatus.Empty;
                Require(OniNativeDomain.CopyGravityStatus(ref field) == 0 && field.Enabled == 1 && field.State == 1 && field.Gravity == 9.81 &&
                    field.Session == status.Session && field.CompletedFrame == status.CompletedFrames && field.BodyImpulseY < 0 &&
                    field.PendingBoundaryPotential == 0 && field.CommittedBoundaryPotential == 0 && field.MaximumEnergyResidual <= 1e-6 &&
                    field.Potential != field.InitialPotential && values.Any(cell => cell.MomentumY != 0), "Gravity did not advance on the actual native clock.");
                var projected = values.Select((cell, index) => cell.TotalMass * 9.81 * (index / DomainWidth + .5)).Sum();
                var residual = values.Sum(cell => cell.InternalEnergy + cell.ResolvedKineticEnergy) + projected - initialEnergy;
                Require(Math.Abs(projected-field.Potential)<1e-9 && Math.Abs(residual)<1e-6, "Physical U+K+G disagrees with its independent projection.");
                report["gravityPassed"] = true; report["gravityEnergyResidualJoules"] = residual;
                report["gravity"] = new { field.Size, field.Version, field.State, field.Enabled, field.Session, field.CompletedFrame, field.Gravity,
                    field.InitialPotential, field.Potential, field.CommittedBoundaryPotential, field.PendingBoundaryPotential,
                    field.BodyImpulseY, field.InternalWork, field.MaximumEnergyResidual };
            }
            report["nativeCompatibilityCellsUnchanged"] = true;
            report["maximumRelativeMassError"] = status.MaximumRelativeMassError;
            report["maximumRelativeEnergyError"] = status.MaximumRelativeEnergyError;
            sim.VerifyNativeSaveRejected(); report["nativeScalarSaveRejected"] = true;
            // A declined BeginSave must leave the simulation runnable.
            sim.Send(-775326397, Frame(.2f)); var afterSave = sim.PrepareAfterFrame(contract);
            var afterStatus = OniNativeDomain.Status.Empty;
            Require(afterSave.FramesProcessed == 1 && OniNativeDomain.Copy(ref afterStatus, values, 6) == 0 &&
                afterStatus.State == 1 && afterStatus.CompletedFrames == status.CompletedFrames + 1,
                "Rejected native saving changed or suspended the simulator.");
            return;
        }
        throw new TimeoutException("Sealed domain did not complete sixty post-bootstrap frames.");
    }
    private static byte[] ReadPhysicalCheckpoint(ulong session,ulong frame)
    {
        Require(OniNativeDomain.CopyCheckpoint(session,frame,null,0,out var size)==0 && size>32,"Physical checkpoint size query failed.");
        var image=new byte[checked((int)size)];
        Require(OniNativeDomain.CopyCheckpoint(session,frame,image,size,out var copied)==0 && copied==size,"Physical checkpoint copy failed.");
        return image;
    }
    private static byte[] ResumePhysicalCheckpoint(byte[] image,double seconds)
    {
        Require(OniNativeDomain.AdvanceCheckpoint(image,(uint)image.Length,seconds,null,0,out var size)==0 && size>32,
            "Physical checkpoint restore/advance query failed.");
        var result=new byte[checked((int)size)];
        Require(OniNativeDomain.AdvanceCheckpoint(image,(uint)image.Length,seconds,result,size,out var copied)==0 && copied==size,
            "Physical checkpoint restore/advance copy failed.");
        return result;
    }
}
