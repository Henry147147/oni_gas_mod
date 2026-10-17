using System.Buffers.Binary;
using System.Security.Cryptography;
using MixedAtmospheres.Core.Interop;

internal static unsafe partial class Program
{
    private static void VerifyNativeDiseaseRetention(NativeSim sim,Contract contract,Snapshot initial,Dictionary<string,object?> report)
    {
        Stage("load-native-unassigned-disease-counter");
        var saved=sim.Save();var width=BinaryPrimitives.ReadInt32LittleEndian(saved.AsSpan(12));
        var height=BinaryPrimitives.ReadInt32LittleEndian(saved.AsSpan(16));var count=checked(width*height);
        var index=OxygenCell%Width+1+(OxygenCell/Width+1)*width;
        var offset=29+16*count+8*index;
        BinaryPrimitives.WriteInt32LittleEndian(saved.AsSpan(offset),0);
        BinaryPrimitives.WriteInt32LittleEndian(saved.AsSpan(offset+4),24);
        report["sourceNativeSha256"]=Convert.ToHexStringLower(SHA256.HashData(saved));
        report["sourceDiseaseHash"]=0;report["sourceUnassignedCount"]=24;
        var restored=sim.RestartAndLoad(contract,contract.Elements,saved);
        var cell=restored.CellData[OxygenCell];
        report["loadedDiseaseIndex"]=cell.Disease;report["loadedDiseaseCount"]=cell.DiseaseCount;
        Require(cell.Disease==255 && cell.DiseaseCount==0,"Original native loader changed its unassigned-counter normalization contract.");
        Require(initial.CellData.Zip(restored.CellData).All(pair=>pair.First.Element==pair.Second.Element &&
            pair.First.Mass==pair.Second.Mass && pair.First.Temperature==pair.Second.Temperature),
            "Native disease-state load changed original material inventory or temperature.");
        report["originalNativeLoadAccepted"]=true;report["loadedDiseaseIndex"]=cell.Disease;report["loadedDiseaseCount"]=cell.DiseaseCount;
        var resaved=sim.Save();report["resavedDiseaseHash"]=BinaryPrimitives.ReadInt32LittleEndian(resaved.AsSpan(offset));
        report["resavedDiseaseCount"]=BinaryPrimitives.ReadInt32LittleEndian(resaved.AsSpan(offset+4));
        Require(BinaryPrimitives.ReadInt32LittleEndian(resaved.AsSpan(offset))==0 && BinaryPrimitives.ReadInt32LittleEndian(resaved.AsSpan(offset+4))==0,
            "Native reserialization did not retain its normalized no-disease state.");
        for(var frame=0;frame<4;++frame)
        {sim.Send(-775326397,Frame(.2f));Require(sim.PrepareAfterFrame(contract).FramesProcessed>0,"Native worker did not continue after its disease-state load.");}
        report["continuedNativeFrames"]=4;
        var phases=Enumerable.Range(0,contract.Count).ToDictionary(i=>BinaryPrimitives.ReadInt32LittleEndian(contract.Elements.AsSpan(4+164*i)),i=>(NativeMatterPhase)contract.Phases[i]);
        var vacuum=BinaryPrimitives.ReadInt32LittleEndian(contract.Elements.AsSpan(4+164*contract.Vacuum));
        var rejected=false;try {NativeWorldBlob.Split(saved,phases,vacuum);} catch(System.IO.InvalidDataException) {rejected=true;}
        Require(rejected,"Fluid migration unexpectedly accepted an unassigned disease quantity.");
        report["strictFluidMigrationStillRejects"]=true;
        var original=(byte[])saved.Clone();NativeWorldBlob.ValidatePreserved(saved,phases,vacuum);
        Require(saved.SequenceEqual(original),"Preserved native admission changed its source bytes.");report["preservedNativeAdmissionRetainsOpaqueBytes"]=true;
    }
}
