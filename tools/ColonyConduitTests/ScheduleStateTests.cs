using System;
using System.IO;
using System.Linq;
using MixedAtmospheres.Oni;

internal static class ScheduleStateTests
{
    internal static void Run()
    {
        var geometry=new PipeGeometry(8,1,0,0,8,1);
        var state=new PipeNativeScheduleState();
        var gas=new PipeNativeScheduleState.Flow {Phase=1,Elapsed=MathF.BitIncrement(.6f),UpdatersDirty=true};
        var liquid=new PipeNativeScheduleState.Flow {Phase=2,Elapsed=1.25f};
        foreach(var index in new[]{5,1,3})
            gas.Cells.Add(new PipeNativeScheduleState.Cell {Index=index,Permitted=15,Source=2,Pull=4,Target=8,
                Current=new PipeNativeScheduleState.Contents {Element=-123,Initial=.75f,Added=MathF.BitIncrement(.25f),Removed=.125f,
                    Temperature=MathF.BitDecrement(300f),Disease=2,Germs=17},
                Initial=new PipeNativeScheduleState.Contents {Element=-123,Initial=.5f,Temperature=300,Disease=255}});
        gas.Networks.Add(new[]{5,1,5});gas.Networks.Add(new[]{3});
        gas.Updaters.Add(new PipeNativeScheduleState.Updater {Kind=1,Prefab=7,Ordinal=1,Priority=5,
            TargetType="Pump",DeclaringType="Pump",Method="Tick"});
        gas.Updaters.Add(new PipeNativeScheduleState.Updater {Kind=0,Priority=5,DeclaringType="Global",Method="Tick"});
        state.Flows.Add(gas);state.Flows.Add(liquid);
        byte[] Write(PipeNativeScheduleState value)
        {using var bytes=new MemoryStream();using var writer=new BinaryWriter(bytes);value.Write(writer,geometry);return bytes.ToArray();}
        PipeNativeScheduleState Read(byte[] bytes)
        {using var stream=new MemoryStream(bytes);using var reader=new BinaryReader(stream);var value=PipeNativeScheduleState.Read(reader,geometry);
            if(stream.Position!=stream.Length)throw new Exception("Native scheduling bytes left unread.");return value;}
        void Reject(Action action)
        {try{action();}catch(Exception error)when(error is InvalidDataException||error is EndOfStreamException){return;}
            throw new Exception("Malformed native schedule was accepted.");}
        var encoded=Write(state);var restored=Read(encoded);var first=restored.Flows[0];
        if(!Write(restored).SequenceEqual(encoded)||first.Elapsed!=gas.Elapsed||!first.UpdatersDirty||restored.Flows[1].Elapsed!=1.25f||
            !first.Cells.Select(c=>c.Index).SequenceEqual(new[]{1,3,5})||!first.Networks[0].SequenceEqual(new[]{5,1,5})||
            first.Updaters[0].Kind!=1||first.Updaters[1].Kind!=0||first.Cells[0].Current.Added!=MathF.BitIncrement(.25f)||
            first.Cells[0].Current.Temperature!=MathF.BitDecrement(300f))
            throw new Exception("Native staging, callback ties, network order or clock bits changed.");
        for(var length=0;length<encoded.Length;++length)Reject(()=>Read(encoded.Take(length).ToArray()));
        gas.Networks.Add(new[]{1});Reject(()=>Write(state));gas.Networks.RemoveAt(2);
        gas.Networks.Add(new[]{7});Reject(()=>Write(state));gas.Networks.RemoveAt(2);
        gas.Cells[0].Permitted=16;Reject(()=>Write(state));gas.Cells[0].Permitted=15;
        gas.Cells[0].Current.Added=float.NaN;Reject(()=>Write(state));gas.Cells[0].Current.Added=.25f;
        gas.Updaters[0].Kind=3;Reject(()=>Write(state));gas.Updaters[0].Kind=1;
        gas.Updaters[0].TargetType=new string('x',4097);Reject(()=>Write(state));
        Console.WriteLine("PASS native schedule preserves staged mass, float clocks, network traversal and callback ties; rejects truncated and inconsistent images");
    }
}
