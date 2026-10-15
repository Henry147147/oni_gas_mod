using System.Collections;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Oni;

// Independent, synthetic saved wire records. No native serializer or product
// writer constructs these inputs. Expected ends come from fixture bytes.
internal static class DirectoryPrefixTests
{
    internal static int Run()
    {
        int passed = 0;
        void Test(string name, Action body) { body(); passed++; Console.WriteLine("PASS directory prefix " + name); }

        Test("zero templates consumes only the count", () =>
        {
            byte[] bytes = Join(new byte[4], new byte[] { 255, 0, 17, 128, 75, 83, 65, 86 });
            var result = Prefix(bytes);
            Check(result.Range.Offset == 0 && result.Range.Length == 4 && result.Range.End == 4 && result.Templates.Count == 0);
            Check(result.Sha256 == Hash(new byte[4]) && result.Image.CopySlice(4, bytes.Length - 4).SequenceEqual(bytes.Skip(4)));
            Check(Prefix(bytes, limits: new SavedInspectionLimits(maximumDirectoryBytes: 4)).Range.End == 4);
        });

        Test("opaque suffix never participates in range or hash", () =>
        {
            byte[] directory = Directory(Template("A", new[] { Member("x", 6) }));
            var expected = Prefix(directory);
            foreach (byte[] suffix in new[] { Array.Empty<byte>(), new byte[] { 255 }, Encoding.UTF8.GetBytes("KSAV Storage Klei.SaveFileRoot"), Enumerable.Range(0, 256).Select(x => (byte)x).ToArray() })
            {
                byte[] bytes = Join(directory, suffix);
                var result = Prefix(bytes, limits: new SavedInspectionLimits(maximumDirectoryBytes: directory.Length));
                Same(expected, result);
                Check(result.Image.CopySlice(0, bytes.Length).SequenceEqual(bytes));
            }
        });

        Test("nonzero offsets retain absolute nested ranges", () =>
        {
            byte[] directory = Complex();
            foreach (int offset in new[] { 0, 1, 7, 31 })
            {
                byte[] bytes = Join(Enumerable.Repeat((byte)255, offset).ToArray(), directory, new byte[] { 255, 254 });
                var result = Prefix(bytes, offset, new SavedInspectionLimits(maximumDirectoryBytes: directory.Length));
                var exact = SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), offset, directory.Length);
                Same(exact, result);
                Check(result.Range.Offset == offset && result.Range.End == offset + directory.Length && result.Sha256 == Hash(directory));
                foreach (var template in result.Templates.Values)
                {
                    Check(template.Range.Offset >= offset + 4 && template.Range.End <= result.Range.End);
                    foreach (var member in template.Members) Inside(member.Type, template.Range);
                }
            }
        });

        Test("exact closure remains mandatory only for exact API", () =>
        {
            byte[] directory = Complex(), bytes = Join(directory, new byte[] { 255, 255, 255 });
            Same(SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), 0, directory.Length), Prefix(bytes));
            var error = Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), 0, bytes.Length), SavedInspectionFailureKind.Malformed);
            Check(error.Offset == directory.Length && error.Message.StartsWith("Unconsumed bytes in a bounded record", StringComparison.Ordinal));
            var shortError = Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), 0, directory.Length,
                new SavedInspectionLimits(maximumDirectoryBytes: directory.Length - 1)), SavedInspectionFailureKind.LimitExceeded);
            Check(shortError.Offset == 0 && shortError.Message.StartsWith("Input exceeds its byte limit", StringComparison.Ordinal));
        });

        Test("fields then properties preserve saved order and ordinal names", () =>
        {
            byte[] bytes = Directory(Template("Type", new[] { Member("z", 6), Member("A", 12) }, new[] { Member("a", 8), Member("b", 1) }), Template("type"));
            var result = Prefix(bytes);
            var members = result.Templates["Type"].Members;
            Check(result.Templates.Count == 2 && members.Select(x => x.Name).SequenceEqual(new[] { "z", "A", "a", "b" }));
            Check(members.Select(x => x.IsProperty).SequenceEqual(new[] { false, false, true, true }));
            Check(members.Select(x => x.Type.Encoding).SequenceEqual(new byte[] { 6, 12, 8, 1 }));
        });

        Test("named value enum array and generic descriptor trees", () =>
        {
            var result = Prefix(Complex());
            var members = result.Templates["First"].Members;
            Check(members[0].Type.Encoding == 64 && members[0].Type.TypeName == "Tag" && members[0].Type.Arguments.Count == 0);
            Check(members[1].Type.Encoding == 13 && members[1].Type.TypeName == "FixtureEnum");
            var generic = members[2].Type;
            Check(generic.Encoding == 128 && generic.TypeName == "Pair" && generic.Arguments.Count == 2);
            Check(generic.Arguments[0].Encoding == 6 && generic.Arguments[1].Encoding == 17);
            var list = generic.Arguments[1].Arguments.Single();
            Check(list.Encoding == 148 && list.Arguments.Single().Encoding == 12);
            Check(members[3].IsProperty && members[3].Type.Encoding == 0 && members[3].Type.TypeName == "ReferenceType");
        });

        Test("every truncated count record string and descriptor is malformed", () =>
        {
            byte[] bytes = Complex();
            for (int length = 0; length < bytes.Length; length++)
            {
                byte[] truncated = bytes.Take(length).ToArray();
                Reject(() => Prefix(truncated), SavedInspectionFailureKind.Malformed);
            }
        });

        Test("every consumed byte budget enforced despite full suffix", () =>
        {
            byte[] directory = Complex(), bytes = Join(new byte[7], directory, new byte[512]);
            for (int budget = 1; budget < directory.Length; budget++)
            {
                int cap = budget;
                Reject(() => Prefix(bytes, 7, new SavedInspectionLimits(maximumDirectoryBytes: cap)), SavedInspectionFailureKind.LimitExceeded);
            }
            Check(Prefix(bytes, 7, new SavedInspectionLimits(maximumDirectoryBytes: directory.Length)).Range.End == 7 + directory.Length);
        });

        Test("real truncation wins over coincident byte budget crossing", () =>
        {
            for (int length = 0; length < 4; length++)
                Reject(() => Prefix(new byte[length], limits: new SavedInspectionLimits(maximumDirectoryBytes: 1)), SavedInspectionFailureKind.Malformed);
            byte[] countOnly = Bytes(w => w.Write(1));
            var error = Reject(() => Prefix(countOnly, limits: new SavedInspectionLimits(maximumDirectoryBytes: 4)), SavedInspectionFailureKind.Malformed);
            Check(error.Offset == 4);
            Reject(() => Prefix(Join(countOnly, new byte[13]), limits: new SavedInspectionLimits(maximumDirectoryBytes: 4)), SavedInspectionFailureKind.LimitExceeded);
        });

        Test("template counts must fit physical bytes before allocation", () =>
        {
            Reject(() => Prefix(Bytes(w => w.Write(-1))), SavedInspectionFailureKind.Malformed);
            Reject(() => Prefix(Bytes(w => w.Write(int.MaxValue))), SavedInspectionFailureKind.LimitExceeded);
            byte[] one = Directory(Template("A")); Put32(one, 0, 2);
            var error = Reject(() => Prefix(one), SavedInspectionFailureKind.Malformed);
            Check(error.Offset == 4);
            byte[] two = Directory(Template("A"), Template("B"));
            Reject(() => Prefix(two, limits: new SavedInspectionLimits(maximumTemplates: 1)), SavedInspectionFailureKind.LimitExceeded);
            Check(Prefix(two, limits: new SavedInspectionLimits(maximumTemplates: 2)).Templates.Count == 2);
        });

        Test("member counts checked together before records are allocated", () =>
        {
            byte[] one = Directory(Template("A"));
            foreach (int count in new[] { -1, 1, int.MaxValue })
            {
                byte[] fields = (byte[])one.Clone(); Put32(fields, 9, count);
                Reject(() => Prefix(fields), count == int.MaxValue ? SavedInspectionFailureKind.LimitExceeded : SavedInspectionFailureKind.Malformed);
                byte[] properties = (byte[])one.Clone(); Put32(properties, 13, count);
                Reject(() => Prefix(properties), count == int.MaxValue ? SavedInspectionFailureKind.LimitExceeded : SavedInspectionFailureKind.Malformed);
            }
            byte[] impossible = Directory(Template("A", new[] { Member("x", 6) })); Put32(impossible, 13, 1);
            Reject(() => Prefix(impossible), SavedInspectionFailureKind.Malformed);
            Reject(() => Prefix(Join(impossible, new byte[6]), limits: new SavedInspectionLimits(maximumDirectoryBytes: impossible.Length)), SavedInspectionFailureKind.LimitExceeded);
        });

        Test("duplicate templates and field property names rejected", () =>
        {
            Reject(() => Prefix(Directory(Template("A"), Template("A"))), SavedInspectionFailureKind.Malformed);
            Reject(() => Prefix(Directory(Template("A", new[] { Member("x", 6), Member("x", 12) }))), SavedInspectionFailureKind.Malformed);
            Reject(() => Prefix(Directory(Template("A", new[] { Member("x", 6) }, new[] { Member("x", 12) }))), SavedInspectionFailureKind.Malformed);
            Check(Prefix(Directory(Template("A", new[] { Member("x", 6), Member("X", 12) }))).Templates["A"].Members.Count == 2);
        });

        Test("strict names UTF8 and string caps precede decoding", () =>
        {
            foreach (byte[] name in new[] { new byte[] { 255 }, new byte[] { 0 }, new byte[] { 65, 10 }, Array.Empty<byte>() })
                Reject(() => Prefix(Bytes(w => { w.Write(1); w.Write(name.Length); w.Write(name); w.Write(0); w.Write(0); })), SavedInspectionFailureKind.Malformed);
            foreach (int length in new[] { -1, -2, int.MinValue, int.MaxValue })
                Reject(() => Prefix(Bytes(w => { w.Write(1); w.Write(length); w.Write(new byte[16]); })),
                    length == int.MaxValue ? SavedInspectionFailureKind.LimitExceeded : SavedInspectionFailureKind.Malformed);
            Reject(() => Prefix(Directory(Template("AB")), limits: new SavedInspectionLimits(maximumTypeNameBytes: 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Prefix(Directory(Template("A", new[] { Member("xy", 6) })), limits: new SavedInspectionLimits(maximumMemberNameBytes: 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Prefix(Directory(Template("A", new[] { Member("x", Named(0, "AB")) })), limits: new SavedInspectionLimits(maximumTypeNameBytes: 1)), SavedInspectionFailureKind.LimitExceeded);
            Check(Prefix(Directory(Template("A", new[] { Member("x", Named(0, "B")) })), limits: new SavedInspectionLimits(maximumTypeNameBytes: 1, maximumMemberNameBytes: 1)).Templates.Count == 1);
        });

        Test("complete image cap still includes untouched suffix", () =>
        {
            byte[] bytes = Join(new byte[4], new byte[32]);
            Reject(() => Prefix(bytes, limits: new SavedInspectionLimits(maximumImageBytes: bytes.Length - 1)), SavedInspectionFailureKind.LimitExceeded);
            Check(Prefix(bytes, limits: new SavedInspectionLimits(maximumImageBytes: bytes.Length, maximumDirectoryBytes: 4)).Range.End == 4);
        });

        Test("member and descriptor node budgets are global", () =>
        {
            byte[] bytes = Directory(Template("A", new[] { Member("x", 6) }), Template("B", properties: new[] { Member("y", 12) }));
            Reject(() => Prefix(bytes, limits: new SavedInspectionLimits(maximumMembers: 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Prefix(bytes, limits: new SavedInspectionLimits(maximumDescriptorNodes: 1)), SavedInspectionFailureKind.LimitExceeded);
            Check(Prefix(bytes, limits: new SavedInspectionLimits(maximumMembers: 2, maximumDescriptorNodes: 2)).Templates.Count == 2);
            byte[] deep = Directory(Template("A", new[] { Member("x", new byte[] { 17, 17, 6 }) }));
            Reject(() => Prefix(deep, limits: new SavedInspectionLimits(maximumDescriptorDepth: 2)), SavedInspectionFailureKind.LimitExceeded);
            Check(Prefix(deep, limits: new SavedInspectionLimits(maximumDescriptorDepth: 3, maximumDescriptorNodes: 3)).Templates.Count == 1);
        });

        Test("unsupported kinds flags and generic arities stay unsupported", () =>
        {
            foreach (byte[] descriptor in new[] { new byte[] { 24 }, new byte[] { 65 }, new byte[] { 134 }, new byte[] { 20 }, new byte[] { 146, 1, 6 }, Join(Named(128, "T"), new byte[] { 0 }), Join(Named(128, "T"), new byte[] { 33 }) })
                Reject(() => Prefix(Directory(Template("A", new[] { Member("x", descriptor) }))), SavedInspectionFailureKind.Unsupported);
        });

        Test("generic child minima distinguish physical and budget bounds", () =>
        {
            byte[] missing = Directory(Template("A", new[] { Member("x", Join(Named(128, "T"), new byte[] { 32 })) }));
            Reject(() => Prefix(missing), SavedInspectionFailureKind.Malformed);
            Reject(() => Prefix(Join(missing, new byte[] { 6 }), limits: new SavedInspectionLimits(maximumDirectoryBytes: missing.Length)), SavedInspectionFailureKind.Malformed);
            byte[] complete = Join(missing, Enumerable.Repeat((byte)6, 32).ToArray());
            Reject(() => Prefix(complete, limits: new SavedInspectionLimits(maximumDirectoryBytes: missing.Length + 31)), SavedInspectionFailureKind.LimitExceeded);
            Check(Prefix(complete).Templates["A"].Members[0].Type.Arguments.Count == 32);
        });

        Test("negative and overflowing coordinates preserve exact failures", () =>
        {
            byte[] bytes = new byte[4];
            foreach (int offset in new[] { -1, int.MinValue, 5, int.MaxValue })
                Reject(() => Prefix(bytes, offset), SavedInspectionFailureKind.Malformed);
            Reject(() => Prefix(bytes, bytes.Length), SavedInspectionFailureKind.Malformed);
            Argument<ArgumentNullException>(() => SavedDirectoryInspector.InspectPrefix(null!, 0));
            Argument<ArgumentNullException>(() => SavedDirectoryInspector.Inspect(null!, 0, 0));
            var negative = Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), -1, 4), SavedInspectionFailureKind.Malformed);
            Check(negative.Offset == 0 && negative.Message.StartsWith("Section is outside the copied image", StringComparison.Ordinal));
            Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), 0, -1), SavedInspectionFailureKind.Malformed);
            var huge = Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), int.MaxValue, int.MaxValue), SavedInspectionFailureKind.LimitExceeded);
            Check(huge.Offset == int.MaxValue && huge.Message.StartsWith("Input exceeds its byte limit", StringComparison.Ordinal));
        });

        Test("copied bytes and all exposed collections stay immutable", () =>
        {
            byte[] directory = Complex(), source = Join(directory, new byte[] { 9, 8, 7 });
            var result = Prefix(source);
            Array.Fill(source, (byte)255);
            Check(result.Sha256 == Hash(directory) && result.Image.CopySlice(result.Range.End, 3).SequenceEqual(new byte[] { 9, 8, 7 }));
            byte[] copy = result.Image.CopySlice(0, directory.Length); copy[0] ^= 255;
            Check(result.Image.CopySlice(0, directory.Length).SequenceEqual(directory));
            Argument<NotSupportedException>(() => ((IDictionary)result.Templates).Add("forged", result.Templates["First"]));
            Argument<NotSupportedException>(() => ((IList)result.Templates["First"].Members).RemoveAt(0));
            Argument<NotSupportedException>(() => ((IList)result.Templates["First"].Members[2].Type.Arguments).RemoveAt(0));
        });

        Test("all directory limit constructors reject invalid caps", () =>
        {
            Action<int>[] limits = { x => _ = new SavedInspectionLimits(maximumImageBytes: x), x => _ = new SavedInspectionLimits(maximumDirectoryBytes: x),
                x => _ = new SavedInspectionLimits(maximumTemplates: x), x => _ = new SavedInspectionLimits(maximumMembers: x),
                x => _ = new SavedInspectionLimits(maximumDescriptorDepth: x), x => _ = new SavedInspectionLimits(maximumDescriptorNodes: x),
                x => _ = new SavedInspectionLimits(maximumTypeNameBytes: x), x => _ = new SavedInspectionLimits(maximumMemberNameBytes: x) };
            int[] caps = { 1 << 30, 64 << 20, 65536, 65536, 32, 262144, 16384, 4096 };
            for (int i = 0; i < limits.Length; i++)
            {
                Action<int> make = limits[i]; int cap = caps[i];
                Argument<ArgumentOutOfRangeException>(() => make(0));
                Argument<ArgumentOutOfRangeException>(() => make(-1));
                Argument<ArgumentOutOfRangeException>(() => make(cap + 1));
                make(cap);
            }
        });
        return passed;
    }

    private sealed record Field(string Name, byte[] Type);
    private sealed record TemplateRecord(string Name, Field[] Fields, Field[] Properties);
    private static Field Member(string name, byte type) => new(name, new[] { type });
    private static Field Member(string name, byte[] type) => new(name, type);
    private static TemplateRecord Template(string name, Field[]? fields = null, Field[]? properties = null)
        => new(name, fields ?? Array.Empty<Field>(), properties ?? Array.Empty<Field>());
    private static byte[] Directory(params TemplateRecord[] templates) => Bytes(w =>
    {
        w.Write(templates.Length);
        foreach (var template in templates)
        {
            Text(w, template.Name); w.Write(template.Fields.Length); w.Write(template.Properties.Length);
            foreach (var member in template.Fields.Concat(template.Properties)) { Text(w, member.Name); w.Write(member.Type); }
        }
    });
    private static byte[] Complex() => Directory(Template("First", new[] { Member("tag", Named(64, "Tag")), Member("enum", Named(13, "FixtureEnum")),
        Member("nested", Join(Named(128, "Pair"), new byte[] { 2, 6, 17, 148, 1, 12 })) }, new[] { Member("reference", Named(0, "ReferenceType")) }),
        Template("Second", properties: new[] { Member("property", 7) }));
    private static byte[] Named(byte kind, string name) => Bytes(w => { w.Write(kind); Text(w, name); });
    private static void Text(BinaryWriter writer, string text) { byte[] bytes = Encoding.UTF8.GetBytes(text); writer.Write(bytes.Length); writer.Write(bytes); }
    private static byte[] Bytes(Action<BinaryWriter> body)
    { using var stream = new MemoryStream(); using (var writer = new BinaryWriter(stream, Encoding.UTF8, true)) body(writer); return stream.ToArray(); }
    private static byte[] Join(params byte[][] arrays) => arrays.SelectMany(x => x).ToArray();
    private static string Hash(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));
    private static SavedDirectory Prefix(byte[] bytes, int offset = 0, SavedInspectionLimits? limits = null)
        => SavedDirectoryInspector.InspectPrefix(SavedImage.CopyFrom(bytes), offset, limits);
    private static void Put32(byte[] bytes, int offset, int value)
    { uint bits = unchecked((uint)value); for (int i = 0; i < 4; i++) bytes[offset + i] = (byte)(bits >> (8 * i)); }
    private static void Check(bool ok) { if (!ok) throw new Exception("Directory-prefix assertion failed"); }
    private static SavedInspectionException Reject(Action action, SavedInspectionFailureKind kind)
    {
        try { action(); }
        catch (SavedInspectionException error) { Check(error.Kind == kind && error.RecordPath == "directory" && error.Offset >= 0); return error; }
        throw new Exception("Expected directory-prefix inspection failure " + kind);
    }
    private static void Argument<T>(Action action) where T : Exception
    { try { action(); } catch (T) { return; } throw new Exception("Expected " + typeof(T).Name); }
    private static void Inside(SavedTypeDescriptor type, SavedByteRange enclosing)
    { Check(type.Range.Offset >= enclosing.Offset && type.Range.End <= enclosing.End); foreach (var child in type.Arguments) Inside(child, type.Range); }
    private static void Same(SavedDirectory left, SavedDirectory right)
    {
        Check(left.Range.Offset == right.Range.Offset && left.Range.Length == right.Range.Length && left.Sha256 == right.Sha256 && left.Templates.Count == right.Templates.Count);
        foreach (var pair in left.Templates)
        {
            var a = pair.Value; var b = right.Templates[pair.Key];
            Check(a.TypeName == b.TypeName && a.Range.Offset == b.Range.Offset && a.Range.Length == b.Range.Length && a.Members.Count == b.Members.Count);
            for (int i = 0; i < a.Members.Count; i++)
            { Check(a.Members[i].Name == b.Members[i].Name && a.Members[i].IsProperty == b.Members[i].IsProperty); SameType(a.Members[i].Type, b.Members[i].Type); }
        }
    }
    private static void SameType(SavedTypeDescriptor a, SavedTypeDescriptor b)
    {
        Check(a.Encoding == b.Encoding && a.TypeName == b.TypeName && a.Range.Offset == b.Range.Offset && a.Range.Length == b.Range.Length && a.Arguments.Count == b.Arguments.Count);
        for (int i = 0; i < a.Arguments.Count; i++) SameType(a.Arguments[i], b.Arguments[i]);
    }
}
