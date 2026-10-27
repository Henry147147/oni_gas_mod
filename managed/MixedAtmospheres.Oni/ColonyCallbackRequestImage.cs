using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // MACCBR01, MACOLP03 section 4 (docs/colony-owner.md#saves): for each
    // ordinal of owner wrapper 13, the persistent actor answering that
    // callback-owned request. Kind 0 releases the request at load.
    // A persistent object: a component (by prefab instance, exact type and
    // ordinal among that type's components) or a state machine instance
    // (by prefab instance and exact type).
    internal sealed class ColonyCallbackIdentity : IComparable<ColonyCallbackIdentity>
    {
        internal byte Kind; // 1 component, 2 state machine instance
        internal int Prefab, Ordinal;
        internal string Type = "";
        public int CompareTo(ColonyCallbackIdentity? other)
        {
            if (other == null) return 1;
            var order = Prefab.CompareTo(other.Prefab); if (order != 0) return order;
            order = Kind.CompareTo(other.Kind); if (order != 0) return order;
            order = string.CompareOrdinal(Type, other.Type); if (order != 0) return order;
            return Ordinal.CompareTo(other.Ordinal);
        }
    }
    internal sealed class ColonyCallbackEntry
    {
        internal byte Kind; // Released, Consumed, Emitted, Actor
        internal ColonyCallbackIdentity? Target; // callback data (Consumed/Emitted) or the actor component
        internal byte Delegate; // 0 static method, 1 instance method of the data, 2 compiler closure singleton
        internal string DeclaringType = "", Method = "", Debug = "";
    }

    internal static class ColonyCallbackRequestImage
    {
        internal const byte Released = 0, Consumed = 1, Emitted = 2, Actor = 3;
        internal const int MaximumText = 512;
        private static readonly byte[] Magic = Encoding.ASCII.GetBytes("MACCBR01");
        private const int Version = 1, MaximumEntries = 1 << 20;
        // The native queue and its actor section are one save boundary. A
        // missing section must refuse before the native loader can release
        // unanswered requests under its legacy fallback policy.
        internal static void ValidateOwner(ReadOnlySpan<byte> owner, byte[]? bytes)
        {
            var expected = ColonyCheckpointMetadata.CallbackRequestCount(owner);
            var actual = bytes == null ? 0 : Decode(bytes).Count;
            if (expected != actual)
                throw new InvalidDataException("The colony's callback-owned requests do not match their saved actors.");
        }
        internal static byte[] Encode(IReadOnlyList<ColonyCallbackEntry> entries)
        {
            if (entries.Count == 0 || entries.Count > MaximumEntries) throw new InvalidDataException("Invalid callback-owned request count.");
            using var stream = new MemoryStream(); using var writer = new BinaryWriter(stream, Encoding.UTF8);
            writer.Write(Magic); writer.Write(Version); writer.Write(entries.Count);
            foreach (var entry in entries)
            {
                writer.Write(entry.Kind);
                if (entry.Kind == Released) continue;
                var target = entry.Target ?? throw new InvalidDataException("A bound callback-owned request has no actor.");
                writer.Write(target.Kind); writer.Write(target.Prefab); writer.Write(target.Ordinal); Text(writer, target.Type);
                if (entry.Kind == Actor) continue;
                writer.Write(entry.Delegate); Text(writer, entry.DeclaringType); Text(writer, entry.Method); Text(writer, entry.Debug);
            }
            return stream.ToArray();
        }
        private static void Text(BinaryWriter writer, string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value);
            if (bytes.Length > MaximumText) throw new InvalidDataException("Callback identity text exceeds its bound.");
            writer.Write(bytes.Length); writer.Write(bytes);
        }
        internal static List<ColonyCallbackEntry> Decode(byte[] bytes)
        {
            using var reader = new BinaryReader(new MemoryStream(bytes, false), Encoding.UTF8);
            try
            {
                if (!reader.ReadBytes(8).AsSpan().SequenceEqual(Magic) || reader.ReadInt32() != Version)
                    throw new InvalidDataException("Unknown callback-owned request section.");
                var count = reader.ReadInt32();
                if (count <= 0 || count > MaximumEntries) throw new InvalidDataException("Invalid callback-owned request count.");
                var entries = new List<ColonyCallbackEntry>(count);
                for (var k = 0; k < count; ++k)
                {
                    var entry = new ColonyCallbackEntry { Kind = reader.ReadByte() };
                    if (entry.Kind > Actor) throw new InvalidDataException("Unknown callback-owned request kind.");
                    if (entry.Kind != Released)
                    {
                        var target = new ColonyCallbackIdentity { Kind = reader.ReadByte(), Prefab = reader.ReadInt32(), Ordinal = reader.ReadInt32(), Type = ReadText(reader) };
                        if ((target.Kind != 1 && target.Kind != 2) || target.Ordinal < 0 || (target.Kind == 2 && target.Ordinal != 0) || target.Type.Length == 0 ||
                            (entry.Kind == Actor && target.Kind != 1))
                            throw new InvalidDataException("Invalid callback-owned request actor.");
                        entry.Target = target;
                        if (entry.Kind != Actor)
                        {
                            entry.Delegate = reader.ReadByte(); entry.DeclaringType = ReadText(reader); entry.Method = ReadText(reader); entry.Debug = ReadText(reader);
                            if (entry.Delegate > 2 || entry.DeclaringType.Length == 0 || entry.Method.Length == 0)
                                throw new InvalidDataException("Invalid callback-owned request callback.");
                        }
                    }
                    entries.Add(entry);
                }
                if (reader.BaseStream.Position != bytes.Length) throw new InvalidDataException("Trailing callback-owned request data.");
                return entries;
            }
            catch (EndOfStreamException) { throw new InvalidDataException("Truncated callback-owned request section."); }
        }
        private static string ReadText(BinaryReader reader)
        {
            var length = reader.ReadInt32();
            if (length < 0 || length > MaximumText) throw new InvalidDataException("Invalid callback identity text.");
            var bytes = reader.ReadBytes(length);
            if (bytes.Length != length) throw new EndOfStreamException();
            return Encoding.UTF8.GetString(bytes);
        }
    }
}
