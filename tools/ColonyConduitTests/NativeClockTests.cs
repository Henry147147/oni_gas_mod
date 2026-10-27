using System;
using System.Buffers.Binary;
using System.IO;
using System.Linq;
using MixedAtmospheres.Oni;

internal static class NativeClockTests
{
    static void Check(bool value,string why){if(!value)throw new Exception(why);}
    static void Reject(Action action)
    {try{action();}catch(InvalidDataException){return;}throw new Exception("invalid native clock record accepted");}
    internal static void Run()
    {
        var state=new ColonyNativeClockState(258,386,98304,uint.MaxValue,-1,1,ushort.MaxValue);var bytes=state.Encode();
        Check(bytes.Length==40&&System.Text.Encoding.ASCII.GetString(bytes,0,8)=="MACNRC01"&&
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(8))==1&&BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(12))==258&&
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(24))==uint.MaxValue&&BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(36))==ushort.MaxValue,
            "native clock wire identity, geometry, full-range RNG or tick changed");
        var cold=ColonyNativeClockState.Decode(bytes);Check(cold.Same(state)&&cold.Encode().SequenceEqual(bytes),"native loop state changed on cold encoding");
        for(var i=0;i<bytes.Length;++i)Reject(()=>ColonyNativeClockState.Decode(bytes.Take(i).ToArray()));
        Reject(()=>ColonyNativeClockState.Decode(bytes.Concat(new byte[]{0}).ToArray()));
        foreach(var offset in new[]{0,8,12,20,28,32,38})
        {var broken=(byte[])bytes.Clone();broken[offset]^=2;Reject(()=>ColonyNativeClockState.Decode(broken));}
        Reject(()=>new ColonyNativeClockState(int.MaxValue,int.MaxValue,1,0,1,1,0));
        Reject(()=>new ColonyNativeClockState(2,386,98304,0,1,1,0));
        Check(!cold.Same(new ColonyNativeClockState(258,386,98304,0,-1,1,ushort.MaxValue)),"native clock identity omitted RNG state");
        Console.WriteLine("PASS native loop record: canonical40-byte image, full-range RNG/tick, exact cold encoding and malformed/overflow/truncated state refusal");
    }
}
