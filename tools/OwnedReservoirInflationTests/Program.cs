using System.Security.Cryptography;
using System.Text.Json;
using MixedAtmospheres.Oni;

internal static class Program
{
    private static readonly List<string> Passed = new();
    private static string Sha(byte[] data) => Convert.ToHexStringLower(SHA256.HashData(data));
    private static void Check(bool value, string message = "Bounded inflation assertion failed")
    { if (!value) throw new InvalidOperationException(message); }
    private static void Test(string name, Action body)
    { try { body(); Passed.Add(name); Console.WriteLine("PASS " + name); } catch (Exception error) { throw new InvalidOperationException("FIRST FAILED " + name, error); } }
    private static SavedInflationReport Inspect(byte[] source, int start = 0, int? length = null, SavedInflationLimits? limits = null)
        => SavedZlibInflationInspector.Inspect(SavedImage.CopyFrom(source), new SavedByteRange(start, length ?? source.Length), limits);
    private static void Denials(SavedInflationReport value)
    {
        Check(value.DeflateFinalBlockProven && value.ExactCompressedRangeValidated && value.DecoderEofObserved && !value.DecoderSourcePositionUsedAsFramingProof && value.DeflateStructureValidated);
        Check(value.DeflateStructure.OutputBytes == value.BodyImage.Length && value.DeflateStructure.Blocks > 0 && value.DeflateStructure.Steps > 0);
        Check(!value.NativeReaderUsed && !value.NativeSimInvoked && !value.NativeSimValidityProven && !value.NativeLoadPermitted &&
            !value.WholeFilePreflightProven && !value.OwnedRestorePermitted && !value.WriterPermission &&
            !value.QuarantineActivated && !value.FirstPlayableColonyPassed);
    }
    private static void Reject(Action body, SavedInspectionFailureKind expected)
    {
        try { body(); }
        catch (SavedInspectionException error)
        { Check(error.Kind == expected && error.RecordPath == "zlib" && error.Offset >= 0, error.ToString()); return; }
        throw new InvalidOperationException("Expected " + expected);
    }
    private static void Argument<T>(Action body) where T : Exception
    { try { body(); } catch (T) { return; } throw new InvalidOperationException("Expected " + typeof(T).Name); }
    private static uint Trailer(byte[] data) => ((uint)data[^4] << 24) | ((uint)data[^3] << 16) | ((uint)data[^2] << 8) | data[^1];
    private static byte[] Join(params byte[][] arrays) => arrays.SelectMany(x => x).ToArray();
    private static byte[] ReadExact(string path, long maximum)
    {
        using var stream = File.OpenRead(path);
        Check(stream.Length <= maximum && stream.Length <= int.MaxValue, "Fixture exceeds its independent read bound");
        byte[] bytes = new byte[(int)stream.Length]; int at = 0;
        while (at < bytes.Length) { int got = stream.Read(bytes, at, bytes.Length - at); Check(got != 0, "Fixture changed/truncated"); at += got; }
        Check(stream.ReadByte() == -1, "Fixture grew"); return bytes;
    }

    private static int Main(string[] args)
    {
        try
        {
            Check(args.Length is 0 or 1, "Expected [FIXTURE_DIRECTORY]");
            string directory = Path.GetFullPath(args.Length == 0 ? Path.Combine(AppContext.BaseDirectory, "Fixtures") : args[0]);
            using var manifest = JsonDocument.Parse(ReadExact(Path.Combine(directory, "manifest-final.json"), 1 << 20));
            var records = manifest.RootElement.GetProperty("records").EnumerateArray().Select(x => x.Clone()).ToArray();
            Check(records.Length == 49 && !manifest.RootElement.GetProperty("productInvoked").GetBoolean());
            var encoded = new Dictionary<string, byte[]>(StringComparer.Ordinal);
            var decoded = new Dictionary<string, byte[]>(StringComparer.Ordinal);
            foreach (var row in records)
            {
                string name = row.GetProperty("name").GetString()!;
                byte[] bytes = ReadExact(Path.Combine(directory, row.GetProperty("encoded").GetString()!), 1 << 20);
                Check(bytes.Length == row.GetProperty("encodedBytes").GetInt32() && Sha(bytes) == row.GetProperty("encodedSha256").GetString());
                encoded.Add(name, bytes);
                if (row.GetProperty("decoded").ValueKind != JsonValueKind.Null)
                {
                    byte[] plain = ReadExact(Path.Combine(directory, row.GetProperty("decoded").GetString()!), 1 << 20);
                    Check(plain.Length == row.GetProperty("decodedBytes").GetInt32() && Sha(plain) == row.GetProperty("decodedSha256").GetString());
                    decoded.Add(name, plain);
                    Test("valid " + name, () =>
                    {
                        var result = Inspect(bytes);
                        Check(result.BodyImage.CopySlice(0, result.BodyImage.Length).SequenceEqual(plain));
                        Check(result.ValidatedCompressedBytes == bytes.Length && result.TotalBytesOut == plain.Length && result.Adler32 == Trailer(bytes));
                        Check(result.CompressedRange.Offset == 0 && result.CompressedRange.Length == bytes.Length && result.ReadCalls > 0);
                        Denials(result);
                    });
                }
                else
                {
                    var kind = Enum.Parse<SavedInspectionFailureKind>(row.GetProperty("failureKind").GetString()!);
                    Test("invalid " + name, () => Reject(() => Inspect(bytes, limits: new SavedInflationLimits(maximumOutputBytes: 4096)), kind));
                }
            }
            byte[] empty = encoded["empty"], small = encoded["one-distance-code"], text = decoded["one-distance-code"];
            Test("nonzero ranges and opaque prefix suffix", () =>
            {
                foreach (int offset in new[] { 0, 1, 17, 65536 })
                {
                    byte[] source = Join(Enumerable.Repeat((byte)255, offset).ToArray(), small, new byte[] { 0, 75, 83, 65, 86, 255 });
                    var result = Inspect(source, offset, small.Length);
                    Check(result.BodyImage.CopySlice(0, text.Length).SequenceEqual(text) && result.ValidatedCompressedBytes == small.Length);
                    Check(result.SourceImage.HashSlice(0, source.Length) == Sha(source) && result.CompressedRange.Offset == offset);
                }
            });
            Test("every small stream truncation rejects without a partial report", () =>
            {
                foreach (string name in new[] { "empty", "one-distance-code", "literal-no-distance" })
                for (int length = 0; length < encoded[name].Length; length++)
                {
                    byte[] partial = encoded[name].Take(length).ToArray(); SavedInflationReport? result = null;
                    Reject(() => result = Inspect(partial, limits: new SavedInflationLimits(maximumOutputBytes: 4096)), SavedInspectionFailureKind.Malformed);
                    Check(result == null);
                }
            });
            Test("checksum corruption cannot pass complete output", () =>
            {
                foreach (string name in new[] { "stored", "fixed-overlap", "dynamic", "empty" })
                for (int index = encoded[name].Length - 4; index < encoded[name].Length; index++)
                { byte[] bad = (byte[])encoded[name].Clone(); bad[index] ^= 1; Reject(() => Inspect(bad), SavedInspectionFailureKind.Malformed); }
            });
            Test("bad header methods windows checks and dictionaries", () =>
            {
                byte[] bad = (byte[])small.Clone(); bad[1] ^= 1; Reject(() => Inspect(bad), SavedInspectionFailureKind.Malformed);
                bad = (byte[])small.Clone(); bad[0] = 0x79; Reject(() => Inspect(bad), SavedInspectionFailureKind.Unsupported);
                bad = (byte[])small.Clone(); bad[0] = 0x88; Reject(() => Inspect(bad), SavedInspectionFailureKind.Malformed);
                foreach (byte cmf in new byte[] { 0x08, 0x68 })
                {
                    bad = (byte[])small.Clone(); bad[0] = cmf; bad[1] = (byte)((31 - cmf * 256 % 31) % 31);
                    Reject(() => Inspect(bad), SavedInspectionFailureKind.Unsupported);
                }
                bad = (byte[])small.Clone(); bad[1] = (byte)(32 + (31 - (0x7800 + 32) % 31) % 31);
                Reject(() => Inspect(bad), SavedInspectionFailureKind.Unsupported);
            });
            Test("trailing and concatenated streams reject", () =>
            {
                foreach (byte[] tail in new[] { new byte[] { 0 }, new byte[] { 255 }, empty, small, new byte[65536] })
                    Reject(() => Inspect(Join(small, tail)), SavedInspectionFailureKind.Malformed);
            });
            Test("output cap including exact completion and one byte over", () =>
            {
                foreach (int chunk in new[] { 1, 2, 3, 65536 })
                {
                    Check(Inspect(small, limits: new SavedInflationLimits(maximumOutputBytes: text.Length, chunkBytes: chunk)).TotalBytesOut == text.Length);
                    for (int maximum = 1; maximum < text.Length; maximum++)
                        Reject(() => Inspect(small, limits: new SavedInflationLimits(maximumOutputBytes: maximum, chunkBytes: chunk)), SavedInspectionFailureKind.LimitExceeded);
                    Check(Inspect(empty, limits: new SavedInflationLimits(maximumOutputBytes: 1, chunkBytes: chunk)).TotalBytesOut == 0);
                }
            });
            Test("source and compressed byte caps precede decoding", () =>
            {
                byte[] padded = Join(new byte[8], small, new byte[8]);
                Reject(() => Inspect(padded, 8, small.Length, new SavedInflationLimits(maximumImageBytes: padded.Length - 1)), SavedInspectionFailureKind.LimitExceeded);
                Reject(() => Inspect(small, limits: new SavedInflationLimits(maximumCompressedBytes: small.Length - 1)), SavedInspectionFailureKind.LimitExceeded);
                Check(Inspect(padded, 8, small.Length, new SavedInflationLimits(maximumImageBytes: padded.Length, maximumCompressedBytes: small.Length)).TotalBytesOut == text.Length);
            });
            Test("explicit decoder Read call budget bounds small-chunk work", () =>
            {
                Reject(() => Inspect(encoded["multichunk"], limits: new SavedInflationLimits(maximumReadCalls: 1)), SavedInspectionFailureKind.LimitExceeded);
                Check(Inspect(encoded["multichunk"], limits: new SavedInflationLimits(maximumReadCalls: 512)).TotalBytesOut == decoded["multichunk"].Length);
            });
            Test("physical negative and overflowing ranges reject", () =>
            {
                foreach (var range in new[] { (-1, 0), (0, -1), (int.MaxValue, 1), (1, int.MaxValue), (small.Length + 1, 0), (small.Length, 1) })
                    Reject(() => SavedZlibInflationInspector.Inspect(SavedImage.CopyFrom(small), new SavedByteRange(range.Item1, range.Item2)), SavedInspectionFailureKind.Malformed);
                for (int length = 0; length < 6; length++) Reject(() => Inspect(new byte[length]), SavedInspectionFailureKind.Malformed);
            });
            Test("nulls and limit constructors reject", () =>
            {
                Argument<ArgumentNullException>(() => SavedZlibInflationInspector.Inspect(null!, new SavedByteRange(0, 0)));
                Argument<ArgumentNullException>(() => SavedZlibInflationInspector.Inspect(SavedImage.CopyFrom(empty), null!));
                Action<int>[] make = { x => _ = new SavedInflationLimits(maximumImageBytes: x), x => _ = new SavedInflationLimits(maximumCompressedBytes: x),
                    x => _ = new SavedInflationLimits(maximumOutputBytes: x), x => _ = new SavedInflationLimits(chunkBytes: x), x => _ = new SavedInflationLimits(maximumReadCalls: x),
                    x => _ = new SavedInflationLimits(maximumStructureBlocks: x), x => _ = new SavedInflationLimits(maximumStructureSteps: x) };
                int[] caps = { 1 << 30, 64 << 20, 256 << 20, 65536, 65536, 65536, 1 << 26 };
                for (int index = 0; index < make.Length; index++) foreach (int value in new[] { 0, -1, caps[index] + 1 })
                { int i = index, v = value; Argument<ArgumentOutOfRangeException>(() => make[i](v)); }
            });
            Test("source and body caller copies cannot mutate a report", () =>
            {
                byte[] caller = (byte[])small.Clone(); var image = SavedImage.CopyFrom(caller); Array.Fill(caller, (byte)255);
                var result = SavedZlibInflationInspector.Inspect(image, new SavedByteRange(0, small.Length));
                Check(ReferenceEquals(result.SourceImage, image) && result.BodyImage.CopySlice(0, text.Length).SequenceEqual(text));
                byte[] copy = result.BodyImage.CopySlice(0, text.Length); Array.Fill(copy, (byte)255);
                Check(result.BodyImage.HashSlice(0, text.Length) == Sha(text) && result.SourceImage.HashSlice(0, small.Length) == Sha(small));
            });
            Test("parallel calls have private stream and output state", () =>
            {
                var source = SavedImage.CopyFrom(encoded["multichunk"]);
                var tasks = Enumerable.Range(0, 12).Select(_ => Task.Run(() => SavedZlibInflationInspector.Inspect(source, new SavedByteRange(0, source.Length)))).ToArray();
                Task.WaitAll(tasks);
                foreach (var task in tasks) Check(task.Result.BodyImage.HashSlice(0, task.Result.BodyImage.Length) == Sha(decoded["multichunk"]));
                Check(tasks.Select(x => x.Result.BodyImage).Distinct().Count() == tasks.Length);
            });
            Test("structure-only valid and malformed fixtures without a codec", () =>
            {
                foreach (var row in records)
                {
                    string name = row.GetProperty("name").GetString()!;
                    if (decoded.TryGetValue(name, out var plain))
                    {
                        var report = SavedDeflateStructureInspector.Inspect(encoded[name], 0, new SavedInflationLimits());
                        Check(report.OutputBytes == plain.Length && report.Blocks > 0 && report.Steps > 0);
                    }
                    else Reject(() => SavedDeflateStructureInspector.Inspect(encoded[name], 0, new SavedInflationLimits()), SavedInspectionFailureKind.Malformed);
                }
            });
            Test("exact structural block and symbol budgets", () =>
            {
                byte[] overlap = encoded["overlap-length-258-distance-2"];
                var report = Inspect(overlap, limits: new SavedInflationLimits(maximumStructureBlocks: 1, maximumStructureSteps: 6));
                Check(report.DeflateStructure.Blocks == 1 && report.DeflateStructure.Steps == 6 &&
                    report.DeflateStructure.FinalCodeBitPosition == 55 && report.BodyImage.Length == 260);
                Reject(() => Inspect(overlap, limits: new SavedInflationLimits(maximumStructureSteps: 5)), SavedInspectionFailureKind.LimitExceeded);
                byte[] many = encoded["twenty-blocks"];
                Check(Inspect(many, limits: new SavedInflationLimits(maximumStructureBlocks: 20)).DeflateStructure.Blocks == 20);
                Reject(() => Inspect(many, limits: new SavedInflationLimits(maximumStructureBlocks: 19)), SavedInspectionFailureKind.LimitExceeded);
            });
            Test("complete malformed payloads fail at their structural guard", () =>
            {
                foreach (var row in records)
                {
                    if (!row.TryGetProperty("expectedStructuralError", out var reason) || reason.ValueKind == JsonValueKind.Null) continue;
                    string name = row.GetProperty("name").GetString()!; bool rejected = false;
                    try { SavedDeflateStructureInspector.Inspect(encoded[name], 0, new SavedInflationLimits()); }
                    catch (SavedInspectionException error)
                    {
                        Check(error.Kind == SavedInspectionFailureKind.Malformed && error.RecordPath == "zlib" &&
                            error.Message.Contains(reason.GetString()!, StringComparison.Ordinal), error.ToString());
                        rejected = true;
                    }
                    Check(rejected, "The complete malformed control escaped its required guard: " + name);
                }
            });
            Check(Passed.Count == 64, "Unexpected complete group count: " + Passed.Count);
            Console.WriteLine(JsonSerializer.Serialize(new { Passed = true, Groups = Passed.Count, Names = Passed,
                Decoder = SavedZlibInflationInspector.Decoder, NativeSimInvoked = false,
                WholeFilePreflightProven = false, OwnedRestorePermitted = false, WriterPermission = false, FirstPlayableColonyPassed = false }));
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
