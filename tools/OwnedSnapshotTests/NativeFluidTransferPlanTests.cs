using System;
using System.Buffers.Binary;
using MixedAtmospheres.Oni;

internal static class NativeFluidTransferPlanTests
{
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        byte[] Consume(int cell,int callback,byte radius=0,byte height=0)
        {
            var data=new byte[16];BinaryPrimitives.WriteInt32LittleEndian(data,cell);BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(4),callback);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(8),BitConverter.SingleToInt32Bits(.02f));
            BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(12),11);data[14]=radius;data[15]=height;return data;
        }
        uint Gas(ushort value)=>value==11 ? 1U : throw new NotSupportedException("Not gas");
        bool Owned(int cell)=>cell>=100 && cell<106;
        test("actual duplicant radius-three consumption retains its callback and radius capability flags",()=> {
            var input=Consume(101,37,3);var copy=(byte[])input.Clone();
            var plan=NativeFluidTransferPlan.Decode(1727657959,16,1,input,Owned,Gas);
            check(plan.Length==1 && plan[0].Cell==101 && plan[0].Callback==37 && plan[0].Species==1 &&
                plan[0].Flags==(1U|(3U<<8)) && plan[0].MassKg==.02f && !plan[0].Injection && plan[0].Temperature==0);
            check(input.AsSpan().SequenceEqual(copy));
        });
        test("point and outside native consumption preserve original scope while rectangular and mixed-owner packets reject",()=> {
            var point=NativeFluidTransferPlan.Decode(1727657959,16,1,Consume(100,7),Owned,Gas);
            check(point[0].Flags==1 && point[0].Callback==7);
            check(NativeFluidTransferPlan.Decode(1727657959,16,1,Consume(12,7,3),Owned,_=>throw new Exception("Outside classifier called")).Length==0);
            reject(()=>NativeFluidTransferPlan.Decode(1727657959,16,1,Consume(100,7,3,2),Owned,Gas));
            var mixed=new byte[32];Consume(100,7).CopyTo(mixed,0);Consume(12,8).CopyTo(mixed,16);
            reject(()=>NativeFluidTransferPlan.Decode(1727657959,16,2,mixed,Owned,Gas));
            var outside=new byte[257*16];
            check(NativeFluidTransferPlan.Decode(1727657959,16,257,outside,Owned,_=>throw new Exception("Outside classifier called")).Length==0);
            reject(()=>NativeFluidTransferPlan.Decode(1727657959,16,257,outside,_=>true,Gas));
        });
        test("mixed phase injection uses the shared phase classifier rather than a gas-only test",()=> {
            var input=new byte[24];BinaryPrimitives.WriteInt32LittleEndian(input,100);BinaryPrimitives.WriteInt32LittleEndian(input.AsSpan(4),9);
            BinaryPrimitives.WriteInt32LittleEndian(input.AsSpan(8),BitConverter.SingleToInt32Bits(.05f));
            BinaryPrimitives.WriteInt32LittleEndian(input.AsSpan(12),BitConverter.SingleToInt32Bits(310));
            BinaryPrimitives.WriteUInt16LittleEndian(input.AsSpan(20),12);
            var plan=NativeFluidTransferPlan.Decode(797274363,24,1,input,Owned,v=>v==12 ? 3U : Gas(v));
            check(plan[0].Species==3 && plan[0].Injection && plan[0].MassKg==.05f && plan[0].Temperature==310 && plan[0].Flags==1);
            reject(()=>NativeFluidTransferPlan.Decode(797274363,24,1,input,Owned,Gas));
            BinaryPrimitives.WriteInt32LittleEndian(input.AsSpan(16),1);
            reject(()=>NativeFluidTransferPlan.Decode(797274363,24,1,input,Owned,_=>3));
        });
        test("late invalid native transfer rejects the entire decoded batch without changing caller bytes",()=> {
            var input=new byte[32];Consume(100,7,3).CopyTo(input,0);Consume(101,8).CopyTo(input,16);
            BinaryPrimitives.WriteInt32LittleEndian(input.AsSpan(24),BitConverter.SingleToInt32Bits(float.NaN));var copy=(byte[])input.Clone();
            reject(()=>NativeFluidTransferPlan.Decode(1727657959,16,2,input,Owned,Gas));check(input.AsSpan().SequenceEqual(copy));
        });
        bool BoxOwned(int cell)=>cell>=0 && cell%64>=10 && cell%64<13 && cell/64>=10 && cell/64<12;
        test("rectangular native overload retains exact width height callback and binary32 input",()=> {
            var input=Consume(650,91,3,2);var held=(byte[])input.Clone();
            var plan=NativeFluidTransferPlan.Decode(1727657959,16,1,input,BoxOwned,Gas,64);
            check(plan.Length==1 && plan[0].Cell==650 && plan[0].Callback==91 && plan[0].Flags==(1U|(3U<<8)|(2U<<16)) &&
                plan[0].MassKg==(double).02f && plan[0].Species==1 && !plan[0].Injection);
            check(input.AsSpan().SequenceEqual(held));
            var empty=NativeFluidTransferPlan.Decode(1727657959,16,1,Consume(716,92,0,255),BoxOwned,Gas,64);
            check(empty.Length==1 && empty[0].Flags==(1U|(255U<<16)) && empty[0].Callback==92);
        });
        test("rectangular ownership inspects the entire footprint including outside anchors",()=> {
            foreach(var input in new[]{Consume(652,1,2,1),Consume(649,2,2,1),Consume(650,3,3,3),Consume(63,4,2,1)})
            {
                var held=(byte[])input.Clone();
                reject(()=>NativeFluidTransferPlan.Decode(1727657959,16,1,input,BoxOwned,_=>throw new Exception("Premature classification"),64));
                check(input.AsSpan().SequenceEqual(held));
            }
            check(NativeFluidTransferPlan.Decode(1727657959,16,1,Consume(100,5,2,2),BoxOwned,
                _=>throw new Exception("Outside classifier called"),64).Length==0);
            var batch=new byte[32];Consume(650,1,3,2).CopyTo(batch,0);Consume(649,2,2,1).CopyTo(batch,16);
            var copy=(byte[])batch.Clone();reject(()=>NativeFluidTransferPlan.Decode(1727657959,16,2,batch,BoxOwned,Gas,64));
            check(batch.AsSpan().SequenceEqual(copy));
        });
    }
}
