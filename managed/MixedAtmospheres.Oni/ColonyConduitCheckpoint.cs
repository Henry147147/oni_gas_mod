using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

namespace MixedAtmospheres.Oni
{
    internal readonly struct PipeGeometry : IEquatable<PipeGeometry>
    {
        internal readonly uint GridWidth, GridHeight, X, Y, Width, Height;
        internal PipeGeometry(uint gridWidth, uint gridHeight, uint x, uint y, uint width, uint height)
        {
            if (gridWidth == 0 || gridHeight == 0 || (ulong)gridWidth * gridHeight > 16_777_216 ||
                width == 0 || height == 0 || (ulong)x + width > gridWidth || (ulong)y + height > gridHeight)
                throw new InvalidDataException("Invalid pipe world geometry.");
            GridWidth = gridWidth; GridHeight = gridHeight; X = x; Y = y; Width = width; Height = height;
        }
        internal bool Contains(int cell) => cell >= 0 && (uint)cell % GridWidth >= X && (uint)cell % GridWidth < X + Width &&
            (uint)cell / GridWidth >= Y && (uint)cell / GridWidth < Y + Height;
        public bool Equals(PipeGeometry other) => GridWidth == other.GridWidth && GridHeight == other.GridHeight &&
            X == other.X && Y == other.Y && Width == other.Width && Height == other.Height;
        public override bool Equals(object? other) => other is PipeGeometry value && Equals(value);
        public override int GetHashCode() => HashCode.Combine(GridWidth, GridHeight, X, Y, Width, Height);
        internal void Write(BinaryWriter writer)
        { writer.Write(GridWidth); writer.Write(GridHeight); writer.Write(X); writer.Write(Y); writer.Write(Width); writer.Write(Height); }
        internal static PipeGeometry Read(BinaryReader reader) => new PipeGeometry(reader.ReadUInt32(), reader.ReadUInt32(),
            reader.ReadUInt32(), reader.ReadUInt32(), reader.ReadUInt32(), reader.ReadUInt32());
    }
    internal readonly struct PipeCellImage
    {
        internal readonly byte Phase; // 1 gas, 2 liquid
        internal readonly int Cell;
        internal readonly PipeInventory Inventory;
        internal PipeCellImage(byte phase, int cell, PipeInventory inventory) { Phase = phase; Cell = cell; Inventory = inventory; }
    }

    // A managed-only image, independent of the native MAC checkpoint versions.
    // The enclosing MACOLP02 image and the existing save envelope also hash it.
    internal sealed class ColonyConduitCheckpoint
    {
        private static readonly byte[] Magic = Encoding.ASCII.GetBytes("MACPIP01");
        internal const int MaximumBytes = 256 * 1024 * 1024;
        internal readonly PipeGeometry Geometry;
        internal readonly byte[] CatalogueHash;
        internal readonly PipeCellImage[] Cells;
        internal readonly PipeInventory Initial, Supplied, Removed;
        internal readonly PipeAmount ExternalHeat;
        internal readonly PipePart[] DiseaseExchange;

        internal ColonyConduitCheckpoint(PipeGeometry geometry, byte[] catalogueHash, IEnumerable<PipeCellImage> cells,
            PipeInventory initial, PipeInventory supplied, PipeInventory removed, PipeAmount heat, IEnumerable<PipePart>? diseaseExchange = null)
        {
            if (catalogueHash == null || catalogueHash.Length != 32) throw new InvalidDataException("Invalid pipe catalogue identity.");
            Geometry = new PipeGeometry(geometry.GridWidth, geometry.GridHeight, geometry.X, geometry.Y, geometry.Width, geometry.Height);
            CatalogueHash = (byte[])catalogueHash.Clone();
            Cells = cells.OrderBy(c => c.Phase).ThenBy(c => c.Cell).ToArray();
            if ((ulong)Cells.Length > 2UL * geometry.Width * geometry.Height) throw new InvalidDataException("Too many pipe cells.");
            byte phase = 0; var previous = -1;
            foreach (var cell in Cells)
            {
                if (cell.Phase != 1 && cell.Phase != 2 || !geometry.Contains(cell.Cell) ||
                    cell.Phase == phase && cell.Cell <= previous || cell.Inventory == null)
                    throw new InvalidDataException("Invalid or duplicate pipe cell.");
                phase = cell.Phase; previous = cell.Cell;
            }
            Initial = initial; Supplied = supplied; Removed = removed; ExternalHeat = heat;
            DiseaseExchange = (diseaseExchange ?? Array.Empty<PipePart>()).OrderBy(g => g.Id).ToArray();
            if (DiseaseExchange.Length > 256) throw new InvalidDataException("Too many pipe disease exchange accounts.");
            for (var i = 1; i < DiseaseExchange.Length; ++i)
                if (DiseaseExchange[i-1].Id == DiseaseExchange[i].Id) throw new InvalidDataException("Duplicate pipe disease exchange account.");
        }
        internal byte[] Encode()
        {
            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream, Encoding.UTF8, true))
            {
                writer.Write(Magic); writer.Write(1); Geometry.Write(writer); writer.Write(CatalogueHash);
                writer.Write(Cells.Length);
                foreach (var cell in Cells) { writer.Write(cell.Phase); writer.Write(cell.Cell); cell.Inventory.Write(writer); }
                Initial.Write(writer); Supplied.Write(writer); Removed.Write(writer); ExternalHeat.Write(writer);
                writer.Write(DiseaseExchange.Length);
                foreach (var item in DiseaseExchange) { writer.Write(item.Id); item.Amount.Write(writer); }
            }
            if (stream.Length > MaximumBytes - 32) throw new InvalidDataException("Pipe checkpoint exceeds its byte limit.");
            var body = stream.ToArray();
            using var hash = SHA256.Create(); var digest = hash.ComputeHash(body);
            var result = new byte[body.Length + digest.Length]; body.CopyTo(result, 0); digest.CopyTo(result, body.Length); return result;
        }
        internal static ColonyConduitCheckpoint Decode(byte[] image, PipeGeometry expectedGeometry, byte[] expectedCatalogue,
            Func<int, byte>? materialPhase = null)
        {
            if (image == null || image.Length < 8 + 4 + 24 + 32 + 4 + 32 || image.Length > MaximumBytes)
                throw new InvalidDataException("Invalid pipe checkpoint length.");
            var end = image.Length - 32;
            using var hash = SHA256.Create(); var actual = hash.ComputeHash(image, 0, end);
            if (!actual.AsSpan().SequenceEqual(image.AsSpan(end))) throw new InvalidDataException("Pipe checkpoint hash mismatch.");
            try
            {
                using var stream = new MemoryStream(image, 0, end, false);
                using var reader = new BinaryReader(stream);
                if (!reader.ReadBytes(8).AsSpan().SequenceEqual(Magic) || reader.ReadInt32() != 1) throw new InvalidDataException("Unsupported pipe checkpoint.");
                var geometry = PipeGeometry.Read(reader); var catalogue = reader.ReadBytes(32);
                if (!geometry.Equals(expectedGeometry) || expectedCatalogue.Length != 32 || !catalogue.AsSpan().SequenceEqual(expectedCatalogue))
                    throw new InvalidDataException("Pipe checkpoint catalogue or geometry differs.");
                var count = reader.ReadInt32();
                if (count < 0 || (ulong)count > 2UL * geometry.Width * geometry.Height || (long)count * 9 > end - stream.Position)
                    throw new InvalidDataException("Invalid pipe cell count.");
                var cells = new PipeCellImage[count];
                for (var i = 0; i < count; ++i)
                {
                    var phase = reader.ReadByte(); var cell = reader.ReadInt32(); var inventory = PipeInventory.Read(reader);
                    if (materialPhase != null) foreach (var part in inventory.Parts)
                        if (materialPhase(part.Id) != phase) throw new InvalidDataException("Pipe constituent has the wrong phase.");
                    cells[i] = new PipeCellImage(phase, cell, inventory);
                }
                var initial = PipeInventory.Read(reader); var supplied = PipeInventory.Read(reader); var removed = PipeInventory.Read(reader);
                var heat = PipeAmount.Read(reader); count = reader.ReadInt32();
                if (count < 0 || count > 256) throw new InvalidDataException("Invalid pipe disease exchange count.");
                var diseases = new PipePart[count];
                for (var i = 0; i < count; ++i) diseases[i] = new PipePart(reader.ReadInt32(), PipeAmount.Read(reader));
                if (stream.Position != end) throw new InvalidDataException("Trailing pipe checkpoint data.");
                var result = new ColonyConduitCheckpoint(geometry, catalogue, cells, initial, supplied, removed, heat, diseases);
                if (!result.Encode().AsSpan().SequenceEqual(image)) throw new InvalidDataException("Noncanonical pipe checkpoint.");
                return result;
            }
            catch (EndOfStreamException error) { throw new InvalidDataException("Truncated pipe checkpoint.", error); }
        }
    }

    internal static class ColonyPipePayload
    {
        private static readonly byte[] Legacy = Encoding.ASCII.GetBytes("MACOLP01"), Mixed = Encoding.ASCII.GetBytes("MACOLP02");
        internal const int ConfigBytes = 56;
        internal static bool Recognizes(ReadOnlySpan<byte> image) => image.Length >= 8 &&
            (image.Slice(0,8).SequenceEqual(Legacy) || image.Slice(0,8).SequenceEqual(Mixed));
        internal static byte[] Encode(byte[] config, byte[] nativeOwner, byte[]? pipes)
        {
            if (config.Length != ConfigBytes || nativeOwner.Length == 0) throw new InvalidDataException("Invalid colony pipe payload.");
            using var stream = new MemoryStream(); using var writer = new BinaryWriter(stream);
            writer.Write(pipes == null ? Legacy : Mixed); writer.Write(config.Length); writer.Write(config);
            if (pipes != null) writer.Write(nativeOwner.Length);
            writer.Write(nativeOwner);
            if (pipes != null) { writer.Write(pipes.Length); writer.Write(pipes); }
            return stream.ToArray();
        }
        internal static (byte[] Config, byte[] Owner, byte[]? Pipes) Decode(ReadOnlySpan<byte> image)
        {
            if (image.Length < 12 + ConfigBytes + 1) throw new InvalidDataException("Truncated colony pipe payload.");
            var mixed = image.Slice(0, 8).SequenceEqual(Mixed);
            if (!mixed && !image.Slice(0, 8).SequenceEqual(Legacy)) throw new InvalidDataException("Unknown colony pipe payload.");
            using var stream = new MemoryStream(image.ToArray(), false); using var reader = new BinaryReader(stream);
            reader.ReadBytes(8);
            if (reader.ReadInt32() != ConfigBytes) throw new InvalidDataException("Invalid colony pipe configuration size.");
            var config = reader.ReadBytes(ConfigBytes);
            if (!mixed) return (config, reader.ReadBytes(checked((int)(stream.Length - stream.Position))), null);
            try
            {
                var length = reader.ReadInt32();
                if (length <= 0 || length > stream.Length - stream.Position - 4) throw new InvalidDataException("Invalid native owner image length.");
                var owner = reader.ReadBytes(length); length = reader.ReadInt32();
                if (length <= 0 || length > ColonyConduitCheckpoint.MaximumBytes || length != stream.Length - stream.Position)
                    throw new InvalidDataException("Invalid pipe image length.");
                return (config, owner, reader.ReadBytes(length));
            }
            catch (EndOfStreamException error) { throw new InvalidDataException("Truncated mixed colony payload.", error); }
        }
    }
}
