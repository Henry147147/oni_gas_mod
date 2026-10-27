using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // The colony owner's managed payload inside the MACOLONY envelope
    // (docs/mixed-pipes.md#saved-framing). MACOLP01: config + owner image.
    // MACOLP03: config, owner image, then tagged sections in increasing tag
    // order. A payload without sections is always written as MACOLP01, so a
    // save with nothing in flight keeps the historical bytes. The dormant
    // MACOLP02 layout is refused.
    internal sealed class ColonySavePayload
    {
        // 3: the colony settings record (ColonySettingsRecord, docs/settings.md).
        // 4: callback-owned requests' actors (ColonyCallbackRequests, MACCBR01).
        internal const uint PipeRuntime = 1, CarbonParticles = 2, ColonySettings = 3, CallbackRequests = 4;
        internal const int ConfigBytes = 56;
        private const int MaximumSectionBytes = 256 * 1024 * 1024;
        private static readonly byte[] Legacy = Encoding.ASCII.GetBytes("MACOLP01"), Dormant = Encoding.ASCII.GetBytes("MACOLP02"),
            Sectioned = Encoding.ASCII.GetBytes("MACOLP03");
        internal byte[] Config { get; }
        internal byte[] Owner { get; }
        internal IReadOnlyDictionary<uint, byte[]> Sections { get; }
        private ColonySavePayload(byte[] config, byte[] owner, SortedDictionary<uint, byte[]> sections)
        { Config = config; Owner = owner; Sections = sections; }
        internal byte[]? Section(uint tag) => Sections.TryGetValue(tag, out var bytes) ? bytes : null;

        internal static bool Recognizes(ReadOnlySpan<byte> data) => data.Length >= 8 &&
            (data.Slice(0, 8).SequenceEqual(Legacy) || data.Slice(0, 8).SequenceEqual(Dormant) || data.Slice(0, 8).SequenceEqual(Sectioned));

        internal static byte[] Encode(byte[] config, byte[] owner, IEnumerable<KeyValuePair<uint, byte[]>>? sections = null)
        {
            if (config == null || config.Length != ConfigBytes || owner == null || owner.Length == 0)
                throw new InvalidDataException("A colony payload requires its configuration and owner image.");
            var ordered = new SortedDictionary<uint, byte[]>();
            foreach (var section in sections ?? Array.Empty<KeyValuePair<uint, byte[]>>())
            {
                ValidateSection(section.Key, section.Value?.Length ?? 0);
                if (ordered.ContainsKey(section.Key)) throw new InvalidDataException("Duplicate colony payload section.");
                ordered.Add(section.Key, section.Value!);
            }
            using var stream = new MemoryStream(); using var writer = new BinaryWriter(stream);
            writer.Write(ordered.Count == 0 ? Legacy : Sectioned); writer.Write(config.Length); writer.Write(config);
            if (ordered.Count == 0) { writer.Write(owner); return stream.ToArray(); }
            writer.Write(owner.Length); writer.Write(owner); writer.Write((uint)ordered.Count);
            foreach (var section in ordered) { writer.Write(section.Key); writer.Write(section.Value.Length); writer.Write(section.Value); }
            return stream.ToArray();
        }

        internal static ColonySavePayload Decode(ReadOnlySpan<byte> data)
        {
            if (data.Length < 12 + ConfigBytes + 1 || !Recognizes(data)) throw new InvalidDataException("Unknown or truncated colony payload.");
            if (data.Slice(0, 8).SequenceEqual(Dormant)) throw new NotSupportedException("MACOLP02 colony payloads are not supported.");
            if (BinaryPrimitives.ReadInt32LittleEndian(data.Slice(8)) != ConfigBytes) throw new InvalidDataException("Invalid colony payload configuration size.");
            var config = data.Slice(12, ConfigBytes).ToArray(); var at = 12 + ConfigBytes;
            var sections = new SortedDictionary<uint, byte[]>();
            if (data.Slice(0, 8).SequenceEqual(Legacy)) return new ColonySavePayload(config, data.Slice(at).ToArray(), sections);
            var owner = Take(data, ref at, Int(data, ref at, 1, int.MaxValue));
            var count = Int(data, ref at, 1, 4);
            var previous = 0U;
            for (var k = 0; k < count; ++k)
            {
                var tag = (uint)Int(data, ref at, 1, int.MaxValue);
                if (tag <= previous) throw new InvalidDataException("Colony payload sections are duplicated or out of order.");
                var length = Int(data, ref at, 1, MaximumSectionBytes);
                ValidateSection(tag, length);
                sections.Add(tag, Take(data, ref at, length)); previous = tag;
            }
            if (at != data.Length) throw new InvalidDataException("Trailing colony payload data.");
            return new ColonySavePayload(config, owner, sections);
        }

        private static void ValidateSection(uint tag, int length)
        {
            if (tag != PipeRuntime && tag != CarbonParticles && tag != ColonySettings && tag != CallbackRequests) throw new NotSupportedException("Unknown colony payload section " + tag + ".");
            if (length <= 0 || length > MaximumSectionBytes) throw new InvalidDataException("Empty or oversized colony payload section.");
        }
        private static int Int(ReadOnlySpan<byte> data, ref int at, int minimum, int maximum)
        {
            if (data.Length - at < 4) throw new InvalidDataException("Truncated colony payload.");
            var value = BinaryPrimitives.ReadInt32LittleEndian(data.Slice(at)); at += 4;
            if (value < minimum || value > maximum) throw new InvalidDataException("Invalid colony payload length or count.");
            return value;
        }
        private static byte[] Take(ReadOnlySpan<byte> data, ref int at, int length)
        {
            if (length > data.Length - at) throw new InvalidDataException("Truncated colony payload section.");
            var bytes = data.Slice(at, length).ToArray(); at += length; return bytes;
        }
    }
}
