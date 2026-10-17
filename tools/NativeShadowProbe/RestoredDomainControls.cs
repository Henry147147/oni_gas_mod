using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using MixedAtmospheres.Oni;

internal static unsafe partial class Program
{
    private static void VerifyRestoredDomain(NativeSim sim,Contract contract,OniNativeObserver observer,Dictionary<string,object?> report,string reportPath)
    {
        Stage("write-and-admit-paired-domain-save");
        var saved=savedDomainPair ?? throw new InvalidOperationException("No completed paired source.");
        var payload=NativeDomainCheckpointPayload.Create(saved.Source,saved.Physical);
        payload.ValidateNativeSection(saved.Native);
        var catalogHash=SHA256.HashData(contract.Elements);
        var envelope=ColonySaveEnvelope.Create(744825,2,saved.Source.CompletedFrame,catalogHash,saved.Native,payload.CopyEncodedData());
        var destination=Path.ChangeExtension(Path.GetFullPath(reportPath),".colony.bin");
        File.WriteAllBytes(destination,envelope.CopyEncodedData());
        var parsed=ColonySaveEnvelope.Parse(File.ReadAllBytes(destination),744825,2,catalogHash);
        var admitted=NativeDomainCheckpointPayload.Parse(parsed.CustomData);
        var phases=Enumerable.Range(0,contract.Count).ToDictionary(i=>System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(contract.Elements.AsSpan(4+164*i)),i=>(MixedAtmospheres.Core.Interop.NativeMatterPhase)contract.Phases[i]);
        int Hash(int index)=>System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(contract.Elements.AsSpan(4+164*index));
        admitted.ValidateNativeInventory(parsed.NativeData,phases,Hash(contract.Vacuum),Hash(contract.Oxygen),Hash(contract.Water),Hash(contract.Unobtanium));
        Require(admitted.CompletedFrame==parsed.CompletedFrame,"Outer and physical source frames differ.");
        var geometry=admitted.Geometry; var physical=admitted.PhysicalImage.ToArray();var native=parsed.NativeData.ToArray();
        var retained=(byte[])native.Clone();var padded=Width+2;var paddedCells=padded*(Height+2);
        var outside=2+2*padded;var diseaseOffset=29+16*paddedCells+8*outside;
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(retained.AsSpan(diseaseOffset),0);
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(retained.AsSpan(diseaseOffset+4),24);
        var originalRetained=(byte[])retained.Clone();
        admitted.ValidateNativeInventory(retained,phases,Hash(contract.Vacuum),Hash(contract.Oxygen),Hash(contract.Water),Hash(contract.Unobtanium));
        Require(retained.SequenceEqual(originalRetained),"Native-only disease counter validation rewrote the saved source.");
        var forbidden=(byte[])native.Clone();var ownedDiseaseCell=DomainX+1+(DomainY+1)*padded;
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(forbidden.AsSpan(29+16*paddedCells+8*ownedDiseaseCell),0);
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(forbidden.AsSpan(29+16*paddedCells+8*ownedDiseaseCell+4),24);
        var ownedRejected=false;
        try {admitted.ValidateNativeInventory(forbidden,phases,Hash(contract.Vacuum),Hash(contract.Oxygen),Hash(contract.Water),Hash(contract.Unobtanium));}
        catch(InvalidDataException) {ownedRejected=true;}
        Require(ownedRejected,"Owned fluid checkpoint accepted an unassigned disease quantity.");
        report["nativeOnlyDiseaseCounterPreservedWithoutRelaxingOwnedCells"]=true;
        var messagesBefore=observer.Capture().Hooks.Single(h=>h.Name=="message").Entries;
        for(var fault=0;fault<8;++fault)
        {
            var inner=payload.CopyEncodedData();
            if(fault==0 || fault==2) Array.Clear(inner,12+8*fault,8);
            if(fault==1) Array.Clear(inner,48,4);
            if(fault==3) System.Buffers.Binary.BinaryPrimitives.WriteInt64LittleEndian(inner.AsSpan(36),BitConverter.DoubleToInt64Bits(double.NaN));
            if(fault==4) Array.Clear(inner,44,4);
            if(fault==5) Array.Clear(inner,68,4);
            if(fault==6) inner[84]^=1;
            if(fault==7) inner[0]^=1;
            var invalid=false;
            try { NativeDomainCheckpointPayload.Parse(inner); } catch(InvalidDataException) {invalid=true;}
            Require(invalid,"Invalid inner checkpoint metadata was admitted.");
        }
        for(var fault=0;fault<4;++fault)
        {
            var invalidNative=(byte[])native.Clone();var index=DomainX+1+(DomainY+1)*(Width+2);
            if(fault==0) invalidNative[29+16*index+8]^=1;
            if(fault==1) invalidNative[29+16*66*66+8*index+4]=1;
            if(fault==2) Array.Clear(invalidNative,29+16*(index-1),4);
            if(fault==3) Array.Clear(invalidNative,29,4);
            var invalid=false;
            try {admitted.ValidateNativeInventory(invalidNative,phases,Hash(contract.Vacuum),Hash(contract.Oxygen),Hash(contract.Water),Hash(contract.Unobtanium));}
            catch(InvalidDataException) {invalid=true;}
            Require(invalid,"Invalid native checkpoint inventory was admitted.");
        }
        Require(OniNativeDomain.ValidateCheckpoint(ref geometry,physical,(uint)physical.Length,admitted.PhysicalTime)==0,
            "Complete saved physical image was rejected: "+Marshal.PtrToStringAnsi(OniNativeDomain.LastError()));
        for(var fault=0;fault<4;++fault)
        {
            var wrong=geometry;var image=(byte[])physical.Clone();var time=admitted.PhysicalTime;
            if(fault==0) ++wrong.Width;
            if(fault==1) wrong.Scenario=geometry.Scenario==1 ? 0U : 1U;
            if(fault==2) time+=.2;
            if(fault==3) image[image.Length/2]^=1;
            Require(OniNativeDomain.ValidateCheckpoint(ref wrong,image,(uint)image.Length,time)!=0,"Invalid restore source reached native admission.");
        }
        var corrupt=File.ReadAllBytes(destination);corrupt[corrupt.Length/2]^=1;var rejected=false;
        try { ColonySaveEnvelope.Parse(corrupt,744825,2,catalogHash); } catch(InvalidDataException) {rejected=true;}
        Require(rejected && observer.Capture().Hooks.Single(h=>h.Name=="message").Entries==messagesBefore,
            "Invalid save was not rejected before any native load/message.");
        report["restoreAdmissionRejectedInvalidSourcesBeforeNativeLoad"]=true;

        // Compact native saves omit property flags. Compare against the exact
        // normal loader result after its empty backwall cache is initialized.
        Stage("native-load-control-zero-time");
        sim.RestartAndLoad(contract,contract.Elements,native);
        sim.Send(-775326397,Frame(0)); var control=sim.PrepareAfterFrame(contract);
        Require(control.FallingLiquidEvents==0 && control.OreEvents==0,"Unmodified load control emitted material.");
        Stage("restore-physical-owner-before-zero-time-flags");
        sim.RestartAndLoad(contract,contract.Elements,native);
        var status=OniNativeDomain.Status.Empty;
        Require(OniNativeDomain.RestoreCheckpoint(ref geometry,physical,(uint)physical.Length,admitted.PhysicalTime,ref status)==0,
            "Native physical restore failed: "+Marshal.PtrToStringAnsi(OniNativeDomain.LastError()));
        Require(status.Session!=admitted.Session && status.CompletedFrames==0 && status.TimeSeconds==admitted.PhysicalTime &&
            ReadPhysicalCheckpoint(status.Session,0).SequenceEqual(physical),"Native restore reset saved clock, revision or canonical state.");
        var restoredSession=status.Session;
        var positiveBefore=observer.Capture().PositiveTimestepReturns;
        for(var i=0;i<6;++i)
        {
            var cell=DomainCell(i);
            sim.Send(-469311643,WritePayload(writer=>
            {writer.Write(cell);writer.Write(-1);writer.Write((byte)3);writer.Write((byte)1);writer.Write((ushort)0);}));
        }
        sim.Send(-775326397,Frame(0));var flagged=sim.PrepareAfterFrame(contract);
        Require(Enumerable.Range(0,6).All(i=>(flagged.CellData[DomainCell(i)].Properties&3)==3),"Restored flow flags were not acknowledged at zero time.");
        var liquid=geometry.Scenario==1;
        for(var i=0;i<6;++i)
        {
            var column=i%DomainWidth;var cell=DomainCell(i);
            sim.Send(-1252920804,WritePayload(writer=>
            {
                writer.Write(cell);writer.Write(-1);writer.Write(DomainProjectionTemperature(column));writer.Write(DomainProjectionMass(column,liquid));writer.Write(0);
                writer.Write((ushort)(liquid ? contract.Water : contract.Oxygen));writer.Write((byte)1);writer.Write((byte)255);writer.Write((byte)1);writer.Write(new byte[3]);
            }));
        }
        sim.Send(-775326397,Frame(0));var restored=sim.PrepareAfterFrame(contract);
        Require(observer.Capture().PositiveTimestepReturns==positiveBefore && ReadPhysicalCheckpoint(restoredSession,0).SequenceEqual(physical),
            "Zero-time restoration advanced the saved physical or native clock.");
        var owned=Enumerable.Range(0,6).Select(DomainCell).ToHashSet();
        for(var cell=0;cell<Cells;++cell)
        {
            if(!owned.Contains(cell)) Require(restored.CellData[cell]==control.CellData[cell],"Restoration changed outside cell "+cell+": "+control.CellData[cell]+" -> "+restored.CellData[cell]);
            else
            {
                var column=(cell%Width)-DomainX;var value=restored.CellData[cell];
                Require(value.Mass==DomainProjectionMass(column,liquid) && value.Temperature==DomainProjectionTemperature(column) &&
                    value.Element==(liquid ? contract.Water : contract.Oxygen) && (value.Properties&3)==3,"Restored native compatibility tuple differs.");
            }
        }
        Require(restored.FallingLiquidEvents==0 && restored.OreEvents==0,"Restoring compatibility cells emitted material.");
        report["restoreZeroTimeFlagsAndCompatibilityVerified"]=true;report["restoreOutsideCellsUnchanged"]=true;
        VerifyPairedCheckpoint(sim,contract,observer,status,report);
        report["restoredFrameZeroPairVerified"]=true;
        var expected=physical;var expectedTime=admitted.PhysicalTime;var trace=new List<object>();
        Stage("advance-restored-native-worker");
        for(var frame=1;frame<=12;++frame)
        {
            expected=ResumePhysicalCheckpoint(expected,(double).2f);expectedTime+=(double).2f;
            sim.Send(-775326397,Frame(.2f));var snapshot=sim.PrepareAfterFrame(contract);
            status=OniNativeDomain.Status.Empty;
            Require(OniNativeDomain.Copy(ref status,new OniNativeDomain.Cell[6],6)==0 && status.State==1 && status.Session==restoredSession &&
                status.CompletedFrames==(ulong)frame && status.TimeSeconds==expectedTime,"Restored worker clock or epoch differs.");
            var actual=ReadPhysicalCheckpoint(restoredSession,(ulong)frame);
            Require(actual.SequenceEqual(expected),"Restored native-worker image differs from independent saved continuation at frame "+frame);
            Require(owned.All(cell=>snapshot.CellData[cell]==restored.CellData[cell]) && snapshot.FallingLiquidEvents==0 && snapshot.OreEvents==0,
                "Native writer changed restored compatibility cells or emitted material.");
            Require(status.MaximumRelativeMassError<=1e-8 && status.MaximumRelativeEnergyError<=1e-6,"Restored physical conservation failed.");
            trace.Add(new{status.CompletedFrames,status.TimeSeconds,status.Substeps,status.MaximumRelativeMassError,status.MaximumRelativeEnergyError,
                Sha256=Convert.ToHexStringLower(SHA256.HashData(actual))});
        }
        sim.VerifyNativeSaveRejected();
        Require(sim.Diagnostics.Count==0,"Native restoration emitted diagnostic errors.");
        report["restoredNativeDomainVerified"]=true;report["restoredNativeDomainFrames"]=trace;
        report["restoredNativeDomain"]=new{SourceSession=admitted.Session,RestoredSession=restoredSession,SourceFrame=admitted.CompletedFrame,
            SourceTime=admitted.PhysicalTime,SourceEpoch=admitted.NativeEpoch,PhysicalBytes=physical.Length,NativeBytes=native.Length,
            EnvelopeBytes=envelope.EncodedLength,Scenario=geometry.Scenario,ExactContinuationFrames=12,OrdinaryGameSaveIntegrated=false,WholeColonyCheckpoint=false};
    }
}
