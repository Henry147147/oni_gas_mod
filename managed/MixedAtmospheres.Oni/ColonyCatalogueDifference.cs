using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace MixedAtmospheres.Oni
{
    // How a colony's saved gases and liquids differ from the game's current
    // ones (docs/game-update-resilience.md#saves-across-builds). The envelope
    // records only a hash of the 168-byte material records; the owner image
    // holds the records themselves, so a refused colony can say what changed.
    // Pure: no game types, so tools/DistributionTests controls it.
    internal sealed class ColonyCatalogueDifference
    {
        internal const int RecordBytes = 168;

        internal sealed class Change
        {
            internal OniNativeColony.Material Saved, Current;
            internal string[] Groups = Array.Empty<string>();
            internal bool IndexOnly => Groups.Length == 1 && Groups[0] == "index";
        }

        internal readonly List<OniNativeColony.Material> Added = new List<OniNativeColony.Material>(), Removed = new List<OniNativeColony.Material>();
        internal readonly List<Change> Changed = new List<Change>();
        internal int SavedCount, CurrentCount;
        internal bool Empty => Added.Count == 0 && Removed.Count == 0 && Changed.Count == 0;
        // Every element is the same except for its place in the element list.
        internal bool OrderOnly => Added.Count == 0 && Removed.Count == 0 && Changed.Count != 0 && Changed.All(c => c.IndexOnly);

        // The records as the native colony owner receives and saves them.
        internal static byte[] Encode(OniNativeColony.Material[] materials)
        {
            var bytes = new byte[materials.Length * RecordBytes];
            var buffer = Marshal.AllocHGlobal(RecordBytes);
            var zero = new byte[RecordBytes];
            try
            {
                for (var k = 0; k < materials.Length; ++k)
                {
                    // Zeroed first, as the bytes after the ID's terminator are part of the hash.
                    Marshal.Copy(zero, 0, buffer, RecordBytes);
                    Marshal.StructureToPtr(materials[k], buffer, false);
                    Marshal.Copy(buffer, bytes, k * RecordBytes, RecordBytes);
                    Marshal.DestroyStructure<OniNativeColony.Material>(buffer);
                }
            }
            finally { Marshal.FreeHGlobal(buffer); }
            return bytes;
        }

        // The envelope's material catalogue identity.
        internal static byte[] Hash(OniNativeColony.Material[] materials)
        {
            using var hash = SHA256.Create(); return hash.ComputeHash(Encode(materials));
        }

        internal static OniNativeColony.Material[] Decode(ReadOnlySpan<byte> table, int count)
        {
            if (count < 0 || table.Length != (long)count * RecordBytes) throw new ArgumentException("The material table is incomplete.");
            var materials = new OniNativeColony.Material[count];
            var buffer = Marshal.AllocHGlobal(RecordBytes);
            try
            {
                var record = new byte[RecordBytes];
                for (var k = 0; k < count; ++k)
                {
                    table.Slice(k * RecordBytes, RecordBytes).CopyTo(record);
                    Marshal.Copy(record, 0, buffer, RecordBytes);
                    materials[k] = Marshal.PtrToStructure<OniNativeColony.Material>(buffer);
                }
            }
            finally { Marshal.FreeHGlobal(buffer); }
            return materials;
        }

        // The records a colony's owner image was saved with (ColonyCheckpointMetadata walks its framing).
        internal static OniNativeColony.Material[] Saved(ReadOnlySpan<byte> ownerImage)
        {
            var table = ColonyCheckpointMetadata.MaterialTable(ownerImage, out var count);
            return Decode(table, count);
        }

        internal static ColonyCatalogueDifference Compare(OniNativeColony.Material[] saved, OniNativeColony.Material[] current)
        {
            var difference = new ColonyCatalogueDifference { SavedCount = saved.Length, CurrentCount = current.Length };
            var before = new Dictionary<int, OniNativeColony.Material>();
            foreach (var m in saved) before[m.ElementHash] = m;
            var now = new Dictionary<int, OniNativeColony.Material>();
            foreach (var m in current) now[m.ElementHash] = m;
            foreach (var m in current)
                if (!before.TryGetValue(m.ElementHash, out var old)) difference.Added.Add(m);
                else if (Groups(old, m) is var groups && groups.Length != 0) difference.Changed.Add(new Change { Saved = old, Current = m, Groups = groups });
            foreach (var m in saved) if (!now.ContainsKey(m.ElementHash)) difference.Removed.Add(m);
            return difference;
        }

        // Which properties differ, in a word or two each (Player.log only).
        private static string[] Groups(OniNativeColony.Material a, OniNativeColony.Material b)
        {
            static bool Same(double x, double y) => BitConverter.DoubleToInt64Bits(x) == BitConverter.DoubleToInt64Bits(y);
            var groups = new List<string>();
            if (a.Phase != b.Phase) groups.Add("phase");
            if (!Same(a.MolarMassKgPerMol, b.MolarMassKgPerMol)) groups.Add("molar mass");
            if (!Same(a.SpecificHeatJPerKgK, b.SpecificHeatJPerKgK)) groups.Add("heat capacity");
            if (!Same(a.ConductivityWPerMK, b.ConductivityWPerMK)) groups.Add("conductivity");
            if (!Same(a.DensityKgPerM3, b.DensityKgPerM3)) groups.Add("density");
            if (!Same(a.LowTemperatureK, b.LowTemperatureK) || !Same(a.HighTemperatureK, b.HighTemperatureK)) groups.Add("transition temperatures");
            if (a.LowTarget != b.LowTarget || a.HighTarget != b.HighTarget) groups.Add("transition targets");
            if (a.LowByproduct != b.LowByproduct || a.HighByproduct != b.HighByproduct ||
                !Same(a.LowByproductFraction, b.LowByproductFraction) || !Same(a.HighByproductFraction, b.HighByproductFraction)) groups.Add("byproducts");
            if (a.Id != b.Id) groups.Add("name");
            if (a.Size != b.Size || a.Version != b.Version || a.Reserved != b.Reserved) groups.Add("record");
            if (a.NativeIndex != b.NativeIndex) groups.Add("index");
            return groups.ToArray();
        }

        // `gameName`: the game's display name for an element hash, or null.
        // Without one, the saved or current element ID.
        internal string Name(OniNativeColony.Material material, Func<int, string?> gameName)
        {
            string? name = null;
            try { name = gameName(material.ElementHash); } catch (Exception) { }
            name = name == null ? "" : Plain(name);
            return name.Length != 0 ? name : material.Id;
        }

        // An element name without the game's link and colour markup.
        internal static string Plain(string text) => Regex.Replace(text, "<[^>]*>", "").Trim();

        // What the player is shown: names of added, removed and changed
        // elements; an element whose place in the list alone moved is left out.
        internal (string[] Added, string[] Removed, string[] Changed) PlayerNames(Func<int, string?> gameName) => (
            Added.Select(m => Name(m, gameName)).ToArray(), Removed.Select(m => Name(m, gameName)).ToArray(),
            Changed.Where(c => !c.IndexOnly).Select(c => Name(c.Current, gameName)).ToArray());

        // Player.log: the counts, then one line per element.
        internal string Log(Func<int, string?> gameName) =>
            $"{Added.Count} added, {Removed.Count} removed, {Changed.Count} changed ({Changed.Count(c => c.IndexOnly)} only in their place in the element list); " +
            $"saved with {SavedCount} gases and liquids, the game has {CurrentCount}:" +
            string.Concat(Added.Select(m => "\n  added " + Named(m, gameName))) +
            string.Concat(Removed.Select(m => "\n  removed " + Named(m, gameName))) +
            string.Concat(Changed.Select(c => "\n  changed " + Named(c.Current, gameName) + ": " + string.Join(", ", c.Groups) +
                (c.IndexOnly ? " only" : "")));

        // "Display name (ID)", or the ID alone.
        private string Named(OniNativeColony.Material material, Func<int, string?> gameName)
        {
            var name = Name(material, gameName);
            return name == material.Id ? name : name + " (" + material.Id + ")";
        }
    }
}
