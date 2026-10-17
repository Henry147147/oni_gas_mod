using System;
using System.Buffers.Binary;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using MixedAtmospheres.Oni;

internal static class NativeBinary32Tests
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint GetControl();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void SetControl(uint control);
    [MethodImpl(MethodImplOptions.NoInlining)] private static double OrdinaryPromotion(float value)=>value;
    [MethodImpl(MethodImplOptions.NoInlining)] private static float OrdinaryProjection(double value)=>(float)value;
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        test("native binary32 wire conversion preserves zero signs subnormals extrema and exceptional classes",()=> {
            check(BitConverter.DoubleToInt64Bits(NativeBinary32.ToDouble(1))==0x36a0000000000000L);
            check(BitConverter.DoubleToInt64Bits(NativeBinary32.ToDouble(0x800000))==0x3810000000000000L);
            check(BitConverter.DoubleToInt64Bits(NativeBinary32.ToDouble(0x7f7fffff))==0x47efffffe0000000L);
            check(BitConverter.DoubleToInt64Bits(NativeBinary32.ToDouble(unchecked((int)0x80000000)))==long.MinValue);
            check(double.IsPositiveInfinity(NativeBinary32.ToDouble(0x7f800000)));
            check(double.IsNegativeInfinity(NativeBinary32.ToDouble(unchecked((int)0xff800000))));
            check(double.IsNaN(NativeBinary32.ToDouble(0x7fc00001)));
        });
        test("all binary32 exponent classes and sampled significands match independent normal CLR conversion exactly",()=> {
            uint random=0x87c4a5b3;
            for(var exponent=0;exponent<255;++exponent)
                for(var sample=0;sample<1024;++sample) {
                    random=unchecked(random*1664525+1013904223);
                    var fraction=sample<3 ? new uint[]{0,1,0x7fffff}[sample] : random&0x7fffff;
                    for(var sign=0;sign<2;++sign) {
                        var bits=unchecked((int)(((uint)sign<<31)|((uint)exponent<<23)|fraction));
                        var native=BitConverter.Int32BitsToSingle(bits);var exact=NativeBinary32.ToDouble(bits);
                        check(BitConverter.DoubleToInt64Bits(exact)==BitConverter.DoubleToInt64Bits((double)native));
                        check(BitConverter.SingleToInt32Bits(NativeBinary32.ToSingleExact(exact))==bits);
                    }
                }
        });
        test("exact native callback projection rejects rounded overflow underflow and nonfinite delivery",()=> {
            foreach(var value in new[]{double.Epsilon,-double.Epsilon,Math.ScaleB(1,-150),Math.ScaleB(1,128),
                1+Math.ScaleB(1,-52),Math.BitDecrement(NativeBinary32.ToDouble(1)),double.NaN,double.PositiveInfinity,double.NegativeInfinity})
                reject(()=>NativeBinary32.ToSingleExact(value));
        });
        test("actual additive and point transfer packets retain a positive binary32 subnormal as a normal double",()=> {
            var additive=new byte[28];BinaryPrimitives.WriteInt32LittleEndian(additive,650);
            BinaryPrimitives.WriteInt32LittleEndian(additive.AsSpan(4),-1);
            BinaryPrimitives.WriteInt32LittleEndian(additive.AsSpan(8),BitConverter.SingleToInt32Bits(300));
            BinaryPrimitives.WriteInt32LittleEndian(additive.AsSpan(12),1);additive[24]=1;
            var edit=NativeCellMassEditPlan.Decode(28,1,additive,_=>true,_=>4)[0];
            check(edit.MassKg==NativeBinary32.ToDouble(1) && edit.MassKg>0 && edit.TemperatureK==300);
            foreach(var inject in new[]{false,true}) {
                var packet=new byte[inject ? 24 : 16];BinaryPrimitives.WriteInt32LittleEndian(packet,650);
                BinaryPrimitives.WriteInt32LittleEndian(packet.AsSpan(4),-1);BinaryPrimitives.WriteInt32LittleEndian(packet.AsSpan(8),1);
                if(inject) BinaryPrimitives.WriteInt32LittleEndian(packet.AsSpan(12),BitConverter.SingleToInt32Bits(300));
                var request=NativeFluidTransferPlan.Decode(inject ? 797274363 : 1727657959,packet.Length,1,packet,_=>true,_=>4)[0];
                check(request.MassKg==edit.MassKg && request.MassKg>0);
            }
        });
        var fixture=Environment.GetEnvironmentVariable("MA_NATIVE_BINARY32_FENV_FIXTURE");
        if(string.IsNullOrEmpty(fixture)) Console.WriteLine("SKIP native binary32 DAZ/FTZ scope: MA_NATIVE_BINARY32_FENV_FIXTURE is not set.");
        else test("native parcel decoding and callback projection retain subnormals while the caller enables DAZ and FTZ",()=> {
            var library=NativeLibrary.Load(fixture);try {
                var get=Marshal.GetDelegateForFunctionPointer<GetControl>(NativeLibrary.GetExport(library,"ma_test_mxcsr_get"));
                var set=Marshal.GetDelegateForFunctionPointer<SetControl>(NativeLibrary.GetExport(library,"ma_test_mxcsr_set"));
                var saved=get();try {
                    set(saved|0x8040U);check((get()&0x8040U)==0x8040U);
                    NativeBinary32.RequireSubnormalRoundTrip();
                    var smallest=BitConverter.Int32BitsToSingle(1);var exact=NativeBinary32.ToDouble(1);
                    check(OrdinaryPromotion(smallest)==0 && BitConverter.SingleToInt32Bits(OrdinaryProjection(exact))==0);
                    check(BitConverter.DoubleToInt64Bits(NativeBinary32.ToDouble(smallest))==0x36a0000000000000L);
                    check(BitConverter.SingleToInt32Bits(NativeBinary32.ToSingleExact(exact))==1);
                    var packet=new byte[28];BinaryPrimitives.WriteInt32LittleEndian(packet,650);
                    BinaryPrimitives.WriteInt32LittleEndian(packet.AsSpan(4),-1);
                    BinaryPrimitives.WriteInt32LittleEndian(packet.AsSpan(8),BitConverter.SingleToInt32Bits(300));
                    BinaryPrimitives.WriteInt32LittleEndian(packet.AsSpan(12),1);packet[24]=1;
                    var decoded=NativeCellMassEditPlan.Decode(28,1,packet,_=>true,_=>4)[0];
                    check(BitConverter.DoubleToInt64Bits(decoded.MassKg)==0x36a0000000000000L);
                    check((get()&0x8040U)==0x8040U);
                } finally {set(saved);}
                check(get()==saved);
            } finally {NativeLibrary.Free(library);}
        });
    }
}
