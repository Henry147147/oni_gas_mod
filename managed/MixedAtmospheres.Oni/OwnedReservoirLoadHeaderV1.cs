using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace MixedAtmospheres.Oni
{
    internal enum OwnedReservoirLoadInputKind { NoSelection, Unmarked, OwnedHeader, InvalidHeader, ReadError, UnsupportedEnvelope, InvalidNativeSection }

    // A declaration found in bounded header bytes. It does not validate a body,
    // checkpoint, native owners or permission to restore any owned state.
    internal sealed class OwnedReservoirLoadHeaderV1
    {
        internal const string ReservedPropertyName = "mixedAtmospheresOwnedReservoirs";
        internal const string Scope = "two-gas-reservoirs-idle";
        internal const int MaximumJsonBytes = 1024 * 1024;
        internal const int MaximumJsonDepth = 32;
        internal const int MaximumJsonTokens = 65536;
        internal const string AssemblyCSharpSha256 = "a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599";
        internal const string FirstpassSha256 = "72d21f51cda136443bfa1a2f6eadb08167a4c508c3d03f558b8cdb60160f249c";
        private static readonly Encoding StrictUtf8 = new UTF8Encoding(false, true);
        internal Guid NativeColonyId { get; }
        internal string CheckpointSha256 { get; }
        internal string EnvelopeSha256 { get; }
        internal string CatalogVersion { get; }
        internal string CatalogFingerprint { get; }
        private OwnedReservoirLoadHeaderV1(Guid colony, string checkpoint, string envelope, string version, string fingerprint)
        { NativeColonyId = colony; CheckpointSha256 = checkpoint; EnvelopeSha256 = envelope; CatalogVersion = version; CatalogFingerprint = fingerprint; }

        internal static OwnedReservoirLoadHeaderInspection Inspect(ReadOnlySpan<byte> completeHeaderBytes)
        {
            if (completeHeaderBytes.Length > MaximumJsonBytes + 16)
                return Invalid(completeHeaderBytes.Slice(0, 16), "", -1, false, "Header exceeds the bounded inspection limit.");
            // Capture before parsing so exposed caller buffers are never retained
            // or sampled a second time for the immutable result.
            return InspectHeader(completeHeaderBytes.ToArray(), "", -1);
        }

        // Checks the actual copied native reader's header without advancing it.
        // A prior path read is not authenticity: even a different valid ordinary
        // header must reject rather than silently replace the selected input.
        internal static OwnedReservoirLoadHeaderInspection VerifyActualHeader(OwnedReservoirLoadHeaderInspection expected,
            ReadOnlySpan<byte> rawBytes, int position)
        {
            if (expected.Kind != OwnedReservoirLoadInputKind.Unmarked) throw new ArgumentException("Only an inspected ordinary selection may enter native header reading.", nameof(expected));
            if (position != 0 || rawBytes.Length < 16)
                return Invalid(rawBytes.Slice(0, Math.Min(rawBytes.Length, 16)), expected.SelectedPath, rawBytes.Length, false, "Actual selected header reader has a shifted/incomplete prefix.");
            var count = BinaryPrimitives.ReadInt32LittleEndian(rawBytes.Slice(4));
            if (count <= 0 || count > MaximumJsonBytes || count > rawBytes.Length - 16)
                return Invalid(rawBytes.Slice(0, 16), expected.SelectedPath, rawBytes.Length, false, "Actual selected header length is invalid or truncated.");
            var header = rawBytes.Slice(0, 16 + count).ToArray();
            var actual = InspectHeader(header, expected.SelectedPath, rawBytes.Length);
            if (actual.Kind == OwnedReservoirLoadInputKind.Unmarked && header.AsSpan().SequenceEqual(expected.CopyHeaderBytes())) return actual;
            return Invalid(header, expected.SelectedPath, rawBytes.Length, actual.ReservedNamespaceObserved,
                "Actual selected header differs from preflight or requires rejection (" + actual.Kind + "): " + actual.Error);
        }

        // Magic/framing check only. No envelope/checkpoint acceptance or generic
        // native-body decoder. Null means the normal native path is untouched.
        internal static OwnedReservoirLoadHeaderInspection? InspectNativeSection(ReadOnlySpan<byte> rawBytes, int position, string selectedPath)
        {
            if (position < 0 || position > rawBytes.Length || rawBytes.Length - position < 4)
                return new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.InvalidNativeSection, selectedPath, rawBytes.Length, false,
                    "Incomplete native section length prefix.", null, Array.Empty<byte>());
            var remaining = rawBytes.Slice(position);
            var count = BinaryPrimitives.ReadInt32LittleEndian(remaining);
            if (count <= 0 || count > remaining.Length - 4)
                return new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.InvalidNativeSection, selectedPath, rawBytes.Length, false,
                    "Native section length exceeds the actual copied reader.", null, remaining.Slice(0, 4).ToArray());
            if (count >= 8 && remaining.Slice(4, 8).SequenceEqual(new byte[] { 77, 65, 67, 79, 76, 79, 78, 89 }))
                return new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.UnsupportedEnvelope, selectedPath, rawBytes.Length, false,
                    "MACOLONY native section is unsupported by this rejection-only boundary, including with an unmarked header.", null, remaining.Slice(0, 12).ToArray());
            return null;
        }

        // No path means the normal new-colony flow. Any actual nonempty path
        // must be proved to have a valid unmarked header before native fallback.
        internal static OwnedReservoirLoadHeaderInspection InspectFile(string? selectedPath)
        {
            if (string.IsNullOrEmpty(selectedPath)) return OwnedReservoirLoadHeaderInspection.NoSelection;
            string path = selectedPath!;
            try
            {
                if (path.Length > 32768 || string.IsNullOrWhiteSpace(path) || path.IndexOf('\0') >= 0 ||
                    path.StartsWith("\\\\.\\", StringComparison.Ordinal) || path.StartsWith("\\\\?\\", StringComparison.Ordinal) ||
                    path.IndexOf("://", StringComparison.Ordinal) >= 0)
                    throw new ArgumentException("Expected an ordinary filesystem path, not a URI or device namespace.");
                path = Path.GetFullPath(path);
                using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
                var prefix = new byte[16];
                var got = ReadToEnd(stream, prefix, 0, prefix.Length);
                if (got != prefix.Length) return InspectHeader(prefix.AsSpan(0, got), path, stream.Length);
                var count = BinaryPrimitives.ReadInt32LittleEndian(prefix.AsSpan(4));
                if (count <= 0 || count > MaximumJsonBytes) return InspectHeader(prefix, path, stream.Length);
                if ((long)count > stream.Length - 16) return Invalid(prefix, path, stream.Length, false, "Truncated declared JSON header.");
                var bytes = new byte[16 + count];
                prefix.CopyTo(bytes, 0);
                got = ReadToEnd(stream, bytes, 16, count);
                if (got != count) return Invalid(bytes.AsSpan(0, 16 + got), path, stream.Length, false, "Truncated JSON header read.");
                return InspectHeader(bytes, path, stream.Length);
            }
            catch (Exception error) when (error is IOException || error is UnauthorizedAccessException || error is ArgumentException ||
                error is NotSupportedException || error is System.Security.SecurityException)
            {
                return new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.ReadError, path, -1, false,
                    "Cannot safely inspect selected save header: " + error.GetType().Name + ": " + error.Message, null, Array.Empty<byte>());
            }
        }

        private static int ReadToEnd(Stream stream, byte[] bytes, int start, int count)
        {
            var read = 0;
            while (read < count) { var got = stream.Read(bytes, start + read, count - read); if (got == 0) break; read += got; }
            return read;
        }

        private static OwnedReservoirLoadHeaderInspection Invalid(ReadOnlySpan<byte> bytes, string path, long size, bool observed, string reason)
            => new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.InvalidHeader, path, size, observed, reason, null, bytes.ToArray());

        private static OwnedReservoirLoadHeaderInspection InspectHeader(ReadOnlySpan<byte> bytes, string path, long size)
        {
            if (bytes.Length < 16) return Invalid(bytes, path, size, false, "Incomplete native header prefix.");
            var build = BinaryPrimitives.ReadUInt32LittleEndian(bytes);
            var count = BinaryPrimitives.ReadInt32LittleEndian(bytes.Slice(4));
            var headerVersion = BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(8));
            var compression = BinaryPrimitives.ReadInt32LittleEndian(bytes.Slice(12));
            if (build == 0 || headerVersion != 1 || (compression != 0 && compression != 1) ||
                count <= 0 || count > MaximumJsonBytes || bytes.Length != 16L + count)
                return Invalid(bytes.Slice(0, Math.Min(bytes.Length, MaximumJsonBytes + 16)), path, size, false,
                    "Unsupported header prefix/version/compression or inconsistent bounded JSON length.");
            JsonParser? parser = null;
            try
            {
                parser = new JsonParser(StrictUtf8.GetString(bytes.Slice(16).ToArray()));
                var root = parser.Parse();
                if (root.Kind != JsonKind.Object) throw new InvalidDataException("Header JSON must be an object.");
                if (!parser.ReservedObserved)
                    return new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.Unmarked, path, size, false, "", null, bytes.ToArray());
                if (!root.Members!.TryGetValue(ReservedPropertyName, out var owned))
                    throw new InvalidDataException("The reserved owned namespace must use its exact case.");
                if (owned.Kind != JsonKind.Object) throw new InvalidDataException("The owned header namespace must be an object.");
                if (Integer(owned, "formatVersion") != 1) throw new InvalidDataException("Unsupported owned header formatVersion.");
                var names = new[] { "formatVersion", "scope", "oniBuild", "nativeColonyId", "ownerCount", "checkpointFormatVersion", "checkpointSha256",
                    "envelopeFormatVersion", "envelopeSha256", "coreFormatVersion", "materialModelVersion", "catalogVersion", "catalogFingerprint",
                    "assemblyCSharpSha256", "assemblyCSharpFirstpassSha256" };
                if (owned.Members!.Count != names.Length) throw new InvalidDataException("Owned header fields are missing or unsupported.");
                foreach (var name in names) if (!owned.Members.ContainsKey(name)) throw new InvalidDataException("Missing owned header field: " + name);
                if (Integer(owned, "formatVersion") != 1 || Integer(owned, "oniBuild") != 744825 || build != 744825 ||
                    Integer(owned, "ownerCount") != 2 || Integer(owned, "checkpointFormatVersion") != 1 ||
                    Integer(owned, "envelopeFormatVersion") != 1 || Integer(owned, "coreFormatVersion") != 3 ||
                    Integer(owned, "materialModelVersion") != 1 || Text(owned, "scope") != Scope)
                    throw new InvalidDataException("Unsupported owned header version, scope, build or owner count.");
                if (Integer(root, "saveMajorVersion") != 7 || Integer(root, "saveMinorVersion") != 38)
                    throw new InvalidDataException("Owned header requires the pinned native object-save version 7.38.");
                var colonyText = Text(owned, "nativeColonyId");
                if (!Guid.TryParseExact(colonyText, "N", out var colony) || colony == Guid.Empty || colony.ToString("N") != colonyText)
                    throw new InvalidDataException("Owned native colony identity is not a canonical nonempty Guid.");
                var nativeText = Text(root, "colonyGuid");
                if (!Guid.TryParseExact(nativeText, "D", out var nativeColony) || nativeColony != colony || nativeColony.ToString("D") != nativeText)
                    throw new InvalidDataException("Owned and ordinary native colony identities disagree.");
                if (Hash(owned, "assemblyCSharpSha256") != AssemblyCSharpSha256 || Hash(owned, "assemblyCSharpFirstpassSha256") != FirstpassSha256)
                    throw new InvalidDataException("Owned header requires different licensed managed contracts.");
                var catalog = Text(owned, "catalogVersion");
                if (string.IsNullOrWhiteSpace(catalog) || StrictUtf8.GetByteCount(catalog) > 1024)
                    throw new InvalidDataException("Invalid declared catalog version.");
                foreach (var ch in catalog) if (char.IsControl(ch)) throw new InvalidDataException("Control character in declared catalog version.");
                var header = new OwnedReservoirLoadHeaderV1(colony, Hash(owned, "checkpointSha256"), Hash(owned, "envelopeSha256"), catalog, Hash(owned, "catalogFingerprint"));
                return new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.OwnedHeader, path, size, true,
                    "Owned reservoir restore is unavailable: this boundary supports rejection only.", header, bytes.ToArray());
            }
            catch (Exception error) when (error is InvalidDataException || error is DecoderFallbackException || error is FormatException || error is OverflowException)
            { return Invalid(bytes, path, size, parser?.ReservedObserved ?? false, error.Message); }
        }

        // Pure bounded header facts. This file remains independently linkable
        // by the existing rejection-only header host and contract project.
        internal static OwnedReservoirLayoutHeaderFacts InspectLayoutHeader(ReadOnlySpan<byte> completeHeaderBytes, bool allowUncompressed = false)
        {
            var inspection = Inspect(completeHeaderBytes);
            if (inspection.Kind != OwnedReservoirLoadInputKind.Unmarked && inspection.Kind != OwnedReservoirLoadInputKind.OwnedHeader)
                throw new InvalidDataException(inspection.Error);
            var bytes = inspection.CopyHeaderBytes();
            uint build = BinaryPrimitives.ReadUInt32LittleEndian(bytes);
            uint version = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(8));
            int compression = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(12));
            if (build != 744825 || version != 1 || (compression != 1 && !(allowUncompressed && compression == 0)))
                throw new NotSupportedException("Unsupported pinned build/v1 body layout or compression.");
            try
            {
                // A bounded second use of the original strict parser reads
                // facts without changing any existing header entry point.
                var root = new JsonParser(StrictUtf8.GetString(bytes, 16, bytes.Length - 16)).Parse();
                if (root.Kind != JsonKind.Object) throw new InvalidDataException("Header JSON must be an object.");
                long major = Integer(root, "saveMajorVersion"), minor = Integer(root, "saveMinorVersion");
                if (major != 7 || minor != 38)
                    throw new NotSupportedException("Only native object-save version 7.38 is supported.");
                string colonyText = Text(root, "colonyGuid");
                if (!Guid.TryParseExact(colonyText, "D", out var colony) || colony == Guid.Empty || colony.ToString("D") != colonyText)
                    throw new InvalidDataException("Expected a canonical nonempty native colony Guid.");
                return new OwnedReservoirLayoutHeaderFacts(inspection, build, version, compression, (int)major, (int)minor, colony);
            }
            catch (Exception error) when (error is DecoderFallbackException || error is FormatException || error is OverflowException)
            { throw new InvalidDataException(error.Message, error); }
        }

        private static string Text(JsonValue value, string key)
        {
            if (!value.Members!.TryGetValue(key, out var field) || field.Kind != JsonKind.String)
                throw new InvalidDataException("Expected string header field: " + key);
            return field.Scalar!;
        }
        private static long Integer(JsonValue value, string key)
        {
            if (!value.Members!.TryGetValue(key, out var field) || field.Kind != JsonKind.Number ||
                !long.TryParse(field.Scalar, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var number))
                throw new InvalidDataException("Expected integral header field: " + key);
            return number;
        }
        private static string Hash(JsonValue value, string key)
        {
            var text = Text(value, key);
            if (text.Length != 64) throw new InvalidDataException("Expected SHA256 header field: " + key);
            foreach (var ch in text) if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
                throw new InvalidDataException("Noncanonical SHA256 header field: " + key);
            return text;
        }

        private enum JsonKind { Object, Array, String, Number, Boolean, Null }
        private sealed class JsonValue
        {
            internal readonly JsonKind Kind;
            internal readonly string? Scalar;
            internal readonly Dictionary<string, JsonValue>? Members;
            internal JsonValue(JsonKind kind, string? scalar = null, Dictionary<string, JsonValue>? members = null)
            { Kind = kind; Scalar = scalar; Members = members; }
        }
        // Original strict JSON syntax reader: no comments, duplicate keys,
        // unquoted names, trailing commas, NaN, invalid surrogates or recovery.
        private sealed class JsonParser
        {
            private readonly string text;
            private int at, tokens;
            internal bool ReservedObserved { get; private set; }
            internal JsonParser(string text) { this.text = text; }
            internal JsonValue Parse()
            { var value = Value(0); White(); if (at != text.Length) Fail("Trailing JSON content."); return value; }
            private void Fail(string reason) { throw new InvalidDataException(reason + " JSON offset " + at.ToString(CultureInfo.InvariantCulture) + "."); }
            private void White() { while (at < text.Length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) ++at; }
            private bool Eat(char value) { White(); if (at < text.Length && text[at] == value) { ++at; return true; } return false; }
            private void Need(char value) { if (!Eat(value)) Fail("Expected '" + value + "'."); }
            private JsonValue Value(int depth)
            {
                if (depth > MaximumJsonDepth || ++tokens > MaximumJsonTokens) Fail("Header JSON complexity limit.");
                White(); if (at >= text.Length) { Fail("Truncated JSON value."); }
                var ch = text[at];
                if (ch == '{')
                {
                    ++at; var members = new Dictionary<string, JsonValue>(StringComparer.Ordinal);
                    if (Eat('}')) return new JsonValue(JsonKind.Object, members: members);
                    do
                    {
                        White(); if (at >= text.Length || text[at] != '"') Fail("Expected quoted property.");
                        var key = String();
                        if (depth == 0 && string.Equals(key, ReservedPropertyName, StringComparison.OrdinalIgnoreCase)) ReservedObserved = true;
                        if (members.ContainsKey(key)) Fail("Duplicate JSON property.");
                        Need(':'); members.Add(key, Value(depth + 1));
                        if (Eat('}')) return new JsonValue(JsonKind.Object, members: members);
                        Need(',');
                    } while (true);
                }
                if (ch == '[')
                {
                    ++at;
                    if (!Eat(']')) { do { Value(depth + 1); if (Eat(']')) break; Need(','); } while (true); }
                    return new JsonValue(JsonKind.Array);
                }
                if (ch == '"') return new JsonValue(JsonKind.String, String());
                foreach (var literal in new[] { "true", "false", "null" })
                    if (at + literal.Length <= text.Length && string.CompareOrdinal(text, at, literal, 0, literal.Length) == 0)
                    { at += literal.Length; return new JsonValue(literal == "null" ? JsonKind.Null : JsonKind.Boolean, literal); }
                var start = at;
                if (ch == '-') ++at;
                if (at < text.Length && text[at] == '0') ++at;
                else { if (at >= text.Length || text[at] < '1' || text[at] > '9') Fail("Invalid JSON number."); Digits(); }
                if (at < text.Length && text[at] == '.') { ++at; if (!Digit()) Fail("Missing fractional digit."); Digits(); }
                if (at < text.Length && (text[at] == 'e' || text[at] == 'E'))
                { ++at; if (at < text.Length && (text[at] == '+' || text[at] == '-')) ++at; if (!Digit()) Fail("Missing exponent digit."); Digits(); }
                return new JsonValue(JsonKind.Number, text.Substring(start, at - start));
            }
            private bool Digit() => at < text.Length && text[at] >= '0' && text[at] <= '9';
            private void Digits() { while (Digit()) ++at; }
            private char Hex()
            {
                if (text.Length - at < 4) Fail("Truncated Unicode escape.");
                var number = 0;
                for (var i = 0; i < 4; ++i)
                {
                    var ch = text[at++];
                    var digit = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
                    if (digit < 0) Fail("Invalid Unicode escape.");
                    number = number * 16 + digit;
                }
                return (char)number;
            }
            private string String()
            {
                Need('"'); var value = new StringBuilder();
                while (at < text.Length)
                {
                    var ch = text[at++];
                    if (ch == '"') return value.ToString();
                    if (ch < 0x20) Fail("Control character in JSON string.");
                    if (ch == '\\')
                    {
                        if (at == text.Length) Fail("Truncated string escape.");
                        var escaped = text[at++];
                        switch (escaped)
                        {
                            case '"': ch = '"'; break; case '\\': ch = '\\'; break; case '/': ch = '/'; break;
                            case 'b': ch = '\b'; break; case 'f': ch = '\f'; break; case 'n': ch = '\n'; break; case 'r': ch = '\r'; break; case 't': ch = '\t'; break;
                            case 'u':
                                ch = Hex();
                                if (char.IsHighSurrogate(ch))
                                {
                                    if (text.Length - at < 6 || text[at] != '\\' || text[at + 1] != 'u') Fail("Unpaired escaped high surrogate.");
                                    at += 2; var low = Hex(); if (!char.IsLowSurrogate(low)) Fail("Invalid escaped surrogate pair.");
                                    value.Append(ch); ch = low;
                                }
                                else if (char.IsLowSurrogate(ch)) Fail("Unpaired escaped low surrogate.");
                                break;
                            default: Fail("Invalid JSON string escape."); break;
                        }
                    }
                    value.Append(ch);
                    if (value.Length > MaximumJsonBytes) Fail("Header string limit.");
                }
                Fail("Unterminated JSON string."); return "";
            }
        }
    }

    internal sealed class OwnedReservoirLayoutHeaderFacts
    {
        internal OwnedReservoirLoadHeaderInspection Inspection { get; }
        internal uint OniBuild { get; }
        internal uint HeaderVersion { get; }
        internal int Compression { get; }
        internal int SaveMajorVersion { get; }
        internal int SaveMinorVersion { get; }
        internal Guid NativeColonyId { get; }
        internal OwnedReservoirLayoutHeaderFacts(OwnedReservoirLoadHeaderInspection inspection, uint build, uint version,
            int compression, int major, int minor, Guid colony)
        {
            Inspection = inspection; OniBuild = build; HeaderVersion = version; Compression = compression;
            SaveMajorVersion = major; SaveMinorVersion = minor; NativeColonyId = colony;
        }
    }

    internal sealed class OwnedReservoirLoadHeaderInspection
    {
        private readonly byte[] bytes;
        internal static OwnedReservoirLoadHeaderInspection NoSelection { get; } = new OwnedReservoirLoadHeaderInspection(
            OwnedReservoirLoadInputKind.NoSelection, "", -1, false, "", null, Array.Empty<byte>());
        internal OwnedReservoirLoadInputKind Kind { get; }
        internal string SelectedPath { get; }
        internal long FileBytes { get; }
        internal bool ReservedNamespaceObserved { get; }
        internal string Error { get; }
        internal OwnedReservoirLoadHeaderV1? Declaration { get; }
        internal int HeaderBytes => bytes.Length;
        internal string HeaderSha256 { get; }
        internal bool MustReject => Kind != OwnedReservoirLoadInputKind.NoSelection && Kind != OwnedReservoirLoadInputKind.Unmarked;
        internal OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind kind, string path, long size, bool observed,
            string error, OwnedReservoirLoadHeaderV1? declaration, byte[] bytes)
        {
            Kind = kind; SelectedPath = path; FileBytes = size; ReservedNamespaceObserved = observed; Error = error;
            Declaration = declaration; this.bytes = (byte[])bytes.Clone();
            using var hash = SHA256.Create();
            HeaderSha256 = BitConverter.ToString(hash.ComputeHash(this.bytes)).Replace("-", "").ToLowerInvariant();
        }
        internal byte[] CopyHeaderBytes() => (byte[])bytes.Clone();
    }
}
