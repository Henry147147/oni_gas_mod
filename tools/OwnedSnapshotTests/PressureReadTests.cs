using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using MixedAtmospheres.Oni;

internal static class PressureReadTests
{
    internal static void Run(System.Action<string, System.Action> test, System.Action<bool> check, System.Action<System.Action> reject)
    {
        test("native pressure units use physical pascals for mixed gas and compressed water", () => {
            check(NativePressureReadPlan.Kilopascals(100000) == 100 && NativePressureReadPlan.Kilopascals(101325) == 101.325f);
            check(NativePressureReadPlan.Kilopascals(220000000) == 220000 && NativePressureReadPlan.Kilopascals(0) == 0);
        });
        test("native pressure projection rejects invalid or unrepresentable values without floors", () => {
            foreach (var value in new[] { -1, double.NaN, double.PositiveInfinity, double.NegativeInfinity, double.MaxValue, double.Epsilon })
                reject(() => NativePressureReadPlan.Kilopascals(value));
        });
        test("all actual game pressure accessor callers match the complete inspected rewrite plan", () => {
            var getter = typeof(Grid.PressureIndexer).GetProperty("Item")!.GetMethod!;
            check(getter.ReturnType == typeof(float) && getter.GetParameters().Single().ParameterType == typeof(int));
            var expected = NativePressureReadPlan.CallSites(typeof(Grid).Assembly).Select(method => method.MetadataToken).ToHashSet();
            check(expected.Count == 5);
            using var file = File.OpenRead(typeof(Grid).Assembly.Location);
            using var pe = new PEReader(file);
            var metadata = pe.GetMetadataReader(); var token = BitConverter.GetBytes(getter.MetadataToken);
            var actual = new HashSet<int>();
            foreach (var handle in metadata.MethodDefinitions) {
                var method = metadata.GetMethodDefinition(handle);
                if (method.RelativeVirtualAddress == 0) continue;
                var bytes = pe.GetMethodBody(method.RelativeVirtualAddress).GetILBytes()!;
                for (var offset = 0; offset + 4 < bytes.Length; ++offset) {
                    if ((bytes[offset] == 0x28 || bytes[offset] == 0x6f) && bytes.AsSpan(offset + 1, 4).SequenceEqual(token)) {
                        actual.Add(MetadataTokens.GetToken(handle)); break;
                    }
                }
            }
            check(actual.SetEquals(expected));
            check(NativePressureReadPlan.CallSites(typeof(Grid).Assembly).All(method => method.GetMethodBody() != null && !method.ContainsGenericParameters));
        });
    }
}
