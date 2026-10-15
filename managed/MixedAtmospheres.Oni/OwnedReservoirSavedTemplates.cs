using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Security.Cryptography;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // Original bounded wire inspection. These types never load a saved CLR type
    // or call the game's serialization system.
    internal enum SavedInspectionFailureKind { Malformed, Unsupported, LimitExceeded }

    internal sealed class SavedInspectionException : Exception
    {
        internal SavedInspectionFailureKind Kind { get; }
        internal int Offset { get; }
        internal string RecordPath { get; }
        internal SavedInspectionException(SavedInspectionFailureKind kind, int offset, string path, string message)
            : base(message + " at " + offset + " (" + path + ")")
        { Kind = kind; Offset = offset; RecordPath = path; }
    }

    internal sealed class SavedInspectionLimits
    {
        internal int MaximumImageBytes { get; }
        internal int MaximumDirectoryBytes { get; }
        internal int MaximumTemplates { get; }
        internal int MaximumMembers { get; }
        internal int MaximumGroups { get; }
        internal int MaximumRoots { get; }
        internal int MaximumComponentsPerRoot { get; }
        internal int MaximumComponents { get; }
        internal int MaximumStorageDepth { get; }
        internal int MaximumDescriptorDepth { get; }
        internal int MaximumDescriptorNodes { get; }
        internal int MaximumTypeNameBytes { get; }
        internal int MaximumMemberNameBytes { get; }
        internal int MaximumValueStringBytes { get; }
        internal int MaximumTags { get; }

        internal SavedInspectionLimits(int maximumImageBytes = 1 << 30, int maximumDirectoryBytes = 64 << 20,
            int maximumTemplates = 65536, int maximumMembers = 65536, int maximumGroups = 65536,
            int maximumRoots = 262144, int maximumComponentsPerRoot = 4096, int maximumComponents = 1048576,
            int maximumStorageDepth = 64, int maximumDescriptorDepth = 32, int maximumDescriptorNodes = 262144,
            int maximumTypeNameBytes = 16384, int maximumMemberNameBytes = 4096, int maximumValueStringBytes = 1 << 20,
            int maximumTags = 1048576)
        {
            MaximumImageBytes = Positive(maximumImageBytes, nameof(maximumImageBytes), 1 << 30);
            MaximumDirectoryBytes = Positive(maximumDirectoryBytes, nameof(maximumDirectoryBytes), 64 << 20);
            MaximumTemplates = Positive(maximumTemplates, nameof(maximumTemplates), 65536);
            MaximumMembers = Positive(maximumMembers, nameof(maximumMembers), 65536);
            MaximumGroups = Positive(maximumGroups, nameof(maximumGroups), 65536);
            MaximumRoots = Positive(maximumRoots, nameof(maximumRoots), 262144);
            MaximumComponentsPerRoot = Positive(maximumComponentsPerRoot, nameof(maximumComponentsPerRoot), 4096);
            MaximumComponents = Positive(maximumComponents, nameof(maximumComponents), 1048576);
            MaximumStorageDepth = Positive(maximumStorageDepth, nameof(maximumStorageDepth), 64);
            MaximumDescriptorDepth = Positive(maximumDescriptorDepth, nameof(maximumDescriptorDepth), 32);
            MaximumDescriptorNodes = Positive(maximumDescriptorNodes, nameof(maximumDescriptorNodes), 262144);
            MaximumTypeNameBytes = Positive(maximumTypeNameBytes, nameof(maximumTypeNameBytes), 16384);
            MaximumMemberNameBytes = Positive(maximumMemberNameBytes, nameof(maximumMemberNameBytes), 4096);
            MaximumValueStringBytes = Positive(maximumValueStringBytes, nameof(maximumValueStringBytes), 1 << 20);
            MaximumTags = Positive(maximumTags, nameof(maximumTags), 1048576);
        }

        private static int Positive(int value, string name, int upper)
        { if (value <= 0 || value > upper) throw new ArgumentOutOfRangeException(name); return value; }
    }

    internal sealed class SavedImage
    {
        private readonly byte[] bytes;
        internal int Length => bytes.Length;
        private SavedImage(byte[] copied) { bytes = copied; }
        internal static SavedImage CopyFrom(byte[] source)
        {
            if (source == null) throw new ArgumentNullException(nameof(source));
            if (source.Length > 1 << 30) throw new ArgumentOutOfRangeException(nameof(source));
            return new SavedImage((byte[])source.Clone());
        }
        internal byte[] CopySlice(int offset, int length)
        {
            CheckSlice(offset, length);
            var result = new byte[length];
            Buffer.BlockCopy(bytes, offset, result, 0, length);
            return result;
        }
        internal string HashSlice(int offset, int length)
        {
            CheckSlice(offset, length);
            using var hash = SHA256.Create();
            var digest = hash.ComputeHash(bytes, offset, length);
            var result = new StringBuilder(64);
            foreach (byte value in digest) result.Append(value.ToString("x2", System.Globalization.CultureInfo.InvariantCulture));
            return result.ToString();
        }
        internal byte ByteAt(int index) => bytes[index];
        internal string DecodeString(int offset, int length) => new UTF8Encoding(false, true).GetString(bytes, offset, length);
        private void CheckSlice(int offset, int length)
        { if (offset < 0 || length < 0 || offset > bytes.Length - length) throw new ArgumentOutOfRangeException(nameof(offset)); }
    }

    internal sealed class SavedByteRange
    {
        internal int Offset { get; }
        internal int Length { get; }
        internal int End => Offset + Length;
        internal SavedByteRange(int offset, int length) { Offset = offset; Length = length; }
    }

    internal sealed class SavedTypeDescriptor
    {
        internal byte Encoding { get; }
        internal int Kind => Encoding & 63;
        internal string? TypeName { get; }
        internal IReadOnlyList<SavedTypeDescriptor> Arguments { get; }
        internal SavedByteRange Range { get; }
        internal SavedTypeDescriptor(byte encoding, string? typeName, List<SavedTypeDescriptor> arguments, SavedByteRange range)
        { Encoding = encoding; TypeName = typeName; Arguments = Array.AsReadOnly(arguments.ToArray()); Range = range; }
    }

    internal sealed class SavedMemberDescriptor
    {
        internal string Name { get; }
        internal bool IsProperty { get; }
        internal SavedTypeDescriptor Type { get; }
        internal SavedMemberDescriptor(string name, bool property, SavedTypeDescriptor type)
        { Name = name; IsProperty = property; Type = type; }
    }

    internal sealed class SavedTemplateDescriptor
    {
        internal string TypeName { get; }
        internal IReadOnlyList<SavedMemberDescriptor> Members { get; }
        internal SavedByteRange Range { get; }
        internal SavedTemplateDescriptor(string name, List<SavedMemberDescriptor> members, SavedByteRange range)
        { TypeName = name; Members = Array.AsReadOnly(members.ToArray()); Range = range; }
    }

    internal sealed class SavedDirectory
    {
        internal SavedImage Image { get; }
        internal SavedByteRange Range { get; }
        internal string Sha256 { get; }
        internal IReadOnlyDictionary<string, SavedTemplateDescriptor> Templates { get; }
        internal SavedDirectory(SavedImage image, SavedByteRange range, Dictionary<string, SavedTemplateDescriptor> templates)
        {
            Image = image; Range = range; Sha256 = image.HashSlice(range.Offset, range.Length);
            Templates = new ReadOnlyDictionary<string, SavedTemplateDescriptor>(new Dictionary<string, SavedTemplateDescriptor>(templates, StringComparer.Ordinal));
        }
    }

    internal sealed class SavedBoundedCursor
    {
        private readonly int start;
        private readonly int? maximumConsumedBytes;
        internal SavedImage Image { get; }
        internal int Position { get; private set; }
        internal int End { get; }
        internal int Remaining => End - Position;
        internal string Path { get; }
        internal SavedBoundedCursor(SavedImage image, int offset, int length, string path, int? maximumConsumedBytes = null)
        {
            Image = image ?? throw new ArgumentNullException(nameof(image)); Path = path;
            if (offset < 0 || length < 0 || (long)offset + length > image.Length)
                throw new SavedInspectionException(SavedInspectionFailureKind.Malformed, Math.Max(0, offset), path, "Section is outside the copied image");
            Position = offset; start = offset; End = offset + length;
            this.maximumConsumedBytes = maximumConsumedBytes;
        }
        internal SavedInspectionException Fail(string message, SavedInspectionFailureKind kind = SavedInspectionFailureKind.Malformed)
            => new SavedInspectionException(kind, Position, Path, message);
        internal void Need(int length)
        {
            if (length < 0 || length > Remaining) throw Fail("Truncated or crossing record");
            NeedBudget(length, 1);
        }
        internal void NeedBudget(int count, int minimumRecordBytes)
        {
            if (maximumConsumedBytes.HasValue && count > (maximumConsumedBytes.Value - (Position - start)) / minimumRecordBytes)
                throw Fail("Directory byte budget exceeded", SavedInspectionFailureKind.LimitExceeded);
        }
        internal void PrefixRecords(int count, int minimumRecordBytes)
        {
            if (!maximumConsumedBytes.HasValue) return;
            if (count > Remaining / minimumRecordBytes) throw Fail("Type arguments cannot fit the directory");
            NeedBudget(count, minimumRecordBytes);
        }
        internal byte ReadByte() { Need(1); return Image.ByteAt(Position++); }
        internal uint ReadUInt32()
        {
            Need(4); uint result = (uint)Image.ByteAt(Position) | ((uint)Image.ByteAt(Position + 1) << 8) |
                ((uint)Image.ByteAt(Position + 2) << 16) | ((uint)Image.ByteAt(Position + 3) << 24);
            Position += 4; return result;
        }
        internal int ReadInt32() => unchecked((int)ReadUInt32());
        internal long ReadInt64() { ulong low = ReadUInt32(); return unchecked((long)(low | ((ulong)ReadUInt32() << 32))); }
        internal void Skip(int length) { Need(length); Position += length; }
        internal SavedByteRange Take(int length) { Need(length); var result = new SavedByteRange(Position, length); Position += length; return result; }
        internal int Count(int maximum, int minimumRecordBytes, string label)
        {
            int count = ReadInt32();
            if (count < 0) throw Fail("Negative " + label);
            if (count > maximum) throw Fail(label + " exceeds its limit", SavedInspectionFailureKind.LimitExceeded);
            if (minimumRecordBytes > 0 && count > Remaining / minimumRecordBytes) throw Fail(label + " cannot fit the enclosing bytes");
            if (minimumRecordBytes > 0) NeedBudget(count, minimumRecordBytes);
            return count;
        }
        internal string? String(int maximumBytes, bool required, bool name)
        {
            int length = ReadInt32();
            if (length == -1 && !required) return null;
            if (length < 0 || (required && length == 0)) throw Fail("Invalid required or negative string length");
            if (length > maximumBytes) throw Fail("String exceeds its byte limit", SavedInspectionFailureKind.LimitExceeded);
            Need(length);
            string result;
            try { result = Image.DecodeString(Position, length); }
            catch (DecoderFallbackException) { throw Fail("Invalid UTF-8"); }
            Position += length;
            if (name) foreach (char character in result) if (char.IsControl(character)) throw Fail("Control character in a name");
            return result;
        }
        internal void RequireEnd()
        { if (Position != End) throw Fail("Unconsumed bytes in a bounded record"); }
    }

    internal static class SavedDirectoryInspector
    {
        internal static SavedDirectory Inspect(SavedImage image, int offset, int exactLength, SavedInspectionLimits? limits = null)
        {
            if (image == null) throw new ArgumentNullException(nameof(image));
            limits ??= new SavedInspectionLimits();
            if (image.Length > limits.MaximumImageBytes || exactLength > limits.MaximumDirectoryBytes)
                throw new SavedInspectionException(SavedInspectionFailureKind.LimitExceeded, Math.Max(0, offset), "directory", "Input exceeds its byte limit");
            var cursor = new SavedBoundedCursor(image, offset, exactLength, "directory");
            var templates = ReadDirectory(cursor, limits);
            cursor.RequireEnd();
            return new SavedDirectory(image, new SavedByteRange(offset, exactLength), templates);
        }

        internal static SavedDirectory InspectPrefix(SavedImage image, int offset, SavedInspectionLimits? limits = null)
        {
            if (image == null) throw new ArgumentNullException(nameof(image));
            limits ??= new SavedInspectionLimits();
            if (image.Length > limits.MaximumImageBytes)
                throw new SavedInspectionException(SavedInspectionFailureKind.LimitExceeded, Math.Max(0, offset), "directory", "Input exceeds its byte limit");
            if (offset < 0 || offset > image.Length)
                throw new SavedInspectionException(SavedInspectionFailureKind.Malformed, Math.Max(0, offset), "directory", "Section is outside the copied image");
            // The physical end and the consumed-byte budget are independent: a real
            // truncation must not be relabelled as an imposed byte-budget failure.
            var cursor = new SavedBoundedCursor(image, offset, image.Length - offset, "directory", limits.MaximumDirectoryBytes);
            var templates = ReadDirectory(cursor, limits);
            return new SavedDirectory(image, new SavedByteRange(offset, cursor.Position - offset), templates);
        }

        private static Dictionary<string, SavedTemplateDescriptor> ReadDirectory(SavedBoundedCursor cursor, SavedInspectionLimits limits)
        {
            int count = cursor.Count(limits.MaximumTemplates, 13, "template count"), membersUsed = 0, nodesUsed = 0;
            var templates = new Dictionary<string, SavedTemplateDescriptor>(StringComparer.Ordinal);
            for (int i = 0; i < count; i++)
            {
                int start = cursor.Position;
                string name = cursor.String(limits.MaximumTypeNameBytes, true, true)!;
                if (templates.ContainsKey(name)) throw cursor.Fail("Duplicate template name");
                int fields = cursor.Count(limits.MaximumMembers - membersUsed, 0, "field count");
                int properties = cursor.Count(limits.MaximumMembers - membersUsed - fields, 0, "property count");
                int total = fields + properties;
                if (total > cursor.Remaining / 6) throw cursor.Fail("Members cannot fit the directory");
                cursor.NeedBudget(total, 6);
                membersUsed += total;
                var members = new List<SavedMemberDescriptor>();
                var names = new HashSet<string>(StringComparer.Ordinal);
                for (int j = 0; j < total; j++)
                {
                    string memberName = cursor.String(limits.MaximumMemberNameBytes, true, true)!;
                    if (!names.Add(memberName)) throw cursor.Fail("Duplicate or ambiguous saved member name");
                    var type = ReadType(cursor, limits, 1, ref nodesUsed);
                    members.Add(new SavedMemberDescriptor(memberName, j >= fields, type));
                }
                templates.Add(name, new SavedTemplateDescriptor(name, members, new SavedByteRange(start, cursor.Position - start)));
            }
            return templates;
        }

        private static SavedTypeDescriptor ReadType(SavedBoundedCursor cursor, SavedInspectionLimits limits, int depth, ref int nodes)
        {
            if (depth > limits.MaximumDescriptorDepth || nodes >= limits.MaximumDescriptorNodes)
                throw cursor.Fail("Type descriptor budget exceeded", SavedInspectionFailureKind.LimitExceeded);
            nodes++;
            int start = cursor.Position;
            byte encoding = cursor.ReadByte();
            int kind = encoding & 63;
            bool generic = (encoding & 128) != 0, value = (encoding & 64) != 0;
            string? name = null;
            var children = new List<SavedTypeDescriptor>();
            if (kind > 23 || (value && (kind != 0 || generic))) throw cursor.Fail("Unsupported descriptor flags or kind", SavedInspectionFailureKind.Unsupported);
            if (generic)
            {
                if (kind != 0 && (kind < 18 || kind > 22)) throw cursor.Fail("Unsupported generic kind", SavedInspectionFailureKind.Unsupported);
                if (kind == 0) name = cursor.String(limits.MaximumTypeNameBytes, true, true);
                int arity = cursor.ReadByte();
                int expected = kind == 18 || kind == 19 ? 2 : (kind >= 20 && kind <= 22 ? 1 : 0);
                if (arity == 0 || arity > 32 || (expected != 0 && arity != expected)) throw cursor.Fail("Invalid generic arity", SavedInspectionFailureKind.Unsupported);
                cursor.PrefixRecords(arity, 1);
                for (int i = 0; i < arity; i++) children.Add(ReadType(cursor, limits, depth + 1, ref nodes));
            }
            else
            {
                if (kind >= 18 && kind <= 22) throw cursor.Fail("Generic kind is missing its flag", SavedInspectionFailureKind.Unsupported);
                if (kind == 0 || kind == 13) name = cursor.String(limits.MaximumTypeNameBytes, true, true);
                if (kind == 17)
                {
                    cursor.PrefixRecords(1, 1);
                    children.Add(ReadType(cursor, limits, depth + 1, ref nodes));
                }
            }
            return new SavedTypeDescriptor(encoding, name, children, new SavedByteRange(start, cursor.Position - start));
        }
    }
}
