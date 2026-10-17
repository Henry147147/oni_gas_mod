using System;
using MixedAtmospheres.Oni;

internal static class GpuPageReadbackTests
{
    internal static void Run(Action<string, System.Action> test, Action<bool> check, Action<System.Action> reject)
    {
        test("GPU rectangle reflection uses the texture height and the entire rectangle", () => {
            check(GpuPageReadbackPlan.ReadY(207,2,384,false)==207);
            check(GpuPageReadbackPlan.ReadY(207,2,384,true)==175);
            check(GpuPageReadbackPlan.ReadY(206,4,384,true)==174);
            reject(()=>GpuPageReadbackPlan.ReadY(-1,2,384,true));
            reject(()=>GpuPageReadbackPlan.ReadY(383,2,384,true));
            reject(()=>GpuPageReadbackPlan.ReadY(0,0,384,true));
            reject(()=>GpuPageReadbackPlan.ReadY(int.MaxValue,2,int.MaxValue,true));
        });
        test("reflected GPU readback reverses rows while retaining every column", () => {
            var expected=new[]{3,4,5,0,1,2};
            for(var index=0;index<6;++index) {
                check(GpuPageReadbackPlan.SourceIndex(index,3,2,true)==expected[index]);
                check(GpuPageReadbackPlan.SourceIndex(index,3,2,false)==index);
            }
            reject(()=>GpuPageReadbackPlan.SourceIndex(6,3,2,true));
            reject(()=>GpuPageReadbackPlan.SourceIndex(0,0,2,true));
        });
        test("native phase and mass pattern uniquely selects either GPU orientation", () => {
            var water=new[]{new[]{250,0,0,255},new[]{0,255,0,140},new[]{250,0,0,255}};
            var rock=new[]{new[]{255,0,0,131},new[]{255,0,0,130},new[]{255,0,0,131}};
            check(GpuPageReadbackPlan.SelectReflection(water,rock,water));
            check(!GpuPageReadbackPlan.SelectReflection(water,water,rock));
        });
        test("ambiguous missing and contradictory GPU coordinate controls reject", () => {
            var water=new[]{new[]{0,255,0,140}};var rock=new[]{new[]{255,0,0,131}};
            reject(()=>GpuPageReadbackPlan.SelectReflection(water,water,water));
            reject(()=>GpuPageReadbackPlan.SelectReflection(water,rock,rock));
            reject(()=>GpuPageReadbackPlan.SelectReflection(Array.Empty<int[]>(),rock,rock));
            reject(()=>GpuPageReadbackPlan.SelectReflection(water,Array.Empty<int[]>(),Array.Empty<int[]>()));
        });
    }
}
