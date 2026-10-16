internal static partial class Program
{
    private static void VerifyZeroTimeFrame(NativeSim sim, Contract contract, Snapshot initial, Dictionary<string, object?> report)
    {
        sim.Send(-775326397, Frame(0));
        var primed=sim.PrepareWithoutFrame(contract);
        // The initial published backwall indices are uninitialized sentinels;
        // the first native update resolves empty ones to the registered vacuum
        // index. Accept only that exact empty-cache mapping, then use a fully
        // initialized snapshot for the strict no-mutation control.
        var initialized=initial.CellData.Select(cell=>cell.Backwall==ushort.MaxValue && cell.BackwallMass==0 && cell.BackwallTemperature==0
            ? cell with {Backwall=checked((ushort)contract.Vacuum)} : cell).ToArray();
        report["initialZeroTimeBackwallCacheNormalization"]=primed.CellData.SequenceEqual(initialized);
        report["initialZeroTimeCacheUnexpectedChanges"]=primed.CellData.Select((cell,index)=>new {index,before=initialized[index],after=cell})
            .Where(item=>item.before!=item.after).Take(32).ToArray();
        Require((bool)report["initialZeroTimeBackwallCacheNormalization"]!, "Initial zero-time update changed more than its explicit empty backwall cache mapping.");
        initial=primed;
        sim.Send(-775326397, Frame(0));
        var unchanged=sim.PrepareWithoutFrame(contract);
        report["zeroTimeNoMutationCellsUnchanged"]=unchanged.CellData.SequenceEqual(initial.CellData);
        var changed=unchanged.CellData.Select((cell,index)=>new {index,before=initial.CellData[index],after=cell}).Where(item=>item.before!=item.after).ToArray();
        report["zeroTimeNoMutationChangedCells"]=changed.Length;
        report["zeroTimeNoMutationDifferences"]=changed.Take(32).ToArray();
        report["zeroTimeNoMutationSnapshot"]=unchanged;
        Require((bool)report["zeroTimeNoMutationCellsUnchanged"]!, "An unmodified zero-time frame changed native cells.");
        Stage("queue-zero-time-lifecycle-mutations");
        sim.Send(-1252920804, ModifyCell(OxygenCell, contract.Vacuum));
        sim.Send(-1252920804, ModifyCell(WaterCell, contract.Vacuum));
        // Test flag acknowledgement separately from native displacement of an
        // occupied cell: the first experiment retained 2 kg of fluid.
        const int flagCell=OxygenCell+5;
        sim.Send(-469311643, WritePayload(writer =>
        { writer.Write(flagCell); writer.Write(-1); writer.Write((byte)3); writer.Write((byte)1); writer.Write((ushort)0); }));
        var before = sim.PrepareWithoutFrame(contract);
        report["beforeZeroTimeFrame"] = before;
        Require(before.FramesProcessed == 0 && before.FluidCells == 2, "Zero-time fixture lost its queued-but-uncommitted negative control.");
        sim.Send(-775326397, Frame(0));
        var clock = System.Diagnostics.Stopwatch.StartNew(); Snapshot after;
        do { after = sim.PrepareWithoutFrame(contract); }
        while (clock.ElapsedMilliseconds < 3000 && (after.FluidCells != 0 || (after.CellData[flagCell].Properties & 3) != 3));
        report["afterZeroTimeFrame"] = after;
        report["zeroTimeFrameCommitsMutations"] = after.FluidCells == 0 && (after.CellData[flagCell].Properties & 3) == 3;
        report["flagCell"]=flagCell; report["flagCellProperties"]=after.CellData[flagCell].Properties;
        report["remainingFluids"]=after.CellData.Select((cell,index)=>new {cell,index}).Where(item=>contract.IsFluid(item.cell.Element))
            .Select(item=>new {item.index,item.cell.Element,item.cell.Mass,item.cell.Temperature,item.cell.Properties}).ToArray();
        report["zeroTimeFrameKeepsSolidTemperature"] = after.ColdTemperature == initial.ColdTemperature && after.HotTemperature == initial.HotTemperature;
        Require((bool)report["zeroTimeFrameCommitsMutations"]!, "An explicit zero-time frame did not commit queued cell/flow-flag mutations.");
        Require((bool)report["zeroTimeFrameKeepsSolidTemperature"]!, "An explicit zero-time frame advanced solid heat.");
        Require(sim.Diagnostics.Count == 0, "Zero-time native frame emitted diagnostics.");
    }
}
