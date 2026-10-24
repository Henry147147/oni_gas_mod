using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // World germ evolution (docs/colony-owner.md#germs). The owner evolves every
    // disease identity of every owned cell by the game's own disease table,
    // handed over before it starts or restores; the game's cell germ edits of
    // owned cells go to the owner instead of the native cells, which the owner
    // overwrites with its shadow each frame.
    internal static class ColonyWorldGerms
    {
        private const string Library = "mixed_atmospheres_oni";
        internal const int CellDiseaseModification = -1853671274;

        [StructLayout(LayoutKind.Sequential)]
        internal struct Disease
        {
            public uint Size, Version; public float Strength;
            public float Range0, Range1, Range2, Range3, HalfLife0, HalfLife1, HalfLife2, HalfLife3;
            public uint Reserved;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Growth
        {
            public float UnderPopulationDeathRate, PopulationHalfLife, OverPopulationHalfLife, DiffusionScale, MinimumCountPerKg, MaximumCountPerKg;
            public int MinimumDiffusionCount; public uint MinimumDiffusionTicks;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct GermEdit { public uint PublicCell, Disease; public double Change; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Summary
        {
            public uint Size, Version, Mode, Identities;
            public ulong Frames, Cultures, GermCells, ExpectedCultures;
            public double Born, Died, Culled, Moved, EditedIn, EditedOut, AuditError;
            public ulong OracleComparisons, OracleMismatches, OracleExtinctions, OracleReserved;
            public double OracleBorn, OracleDied;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 160)] public byte[] OracleLastBytes;
            public string OracleLast => Encoding.UTF8.GetString(OracleLastBytes ?? Array.Empty<byte>()).TrimEnd('\0');
            internal static Summary Empty => new Summary { Size = 312, Version = 1, OracleLastBytes = new byte[160] };
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Account { public ulong Identity; public double Born, Died, Culled, Moved, EditedIn, EditedOut, Held; }

        [DllImport(Library, EntryPoint = "ma_oni_colony_world_biology_version", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint Version();
        [DllImport(Library, EntryPoint = "ma_oni_colony_set_diseases", CallingConvention = CallingConvention.Cdecl)]
        private static extern int SetDiseases([In] Disease[] diseases, uint count, [In] Growth[] growth, uint elements);
        [DllImport(Library, EntryPoint = "ma_oni_colony_queue_germs", CallingConvention = CallingConvention.Cdecl)]
        private static extern int QueueGerms([In] GermEdit[] edits, uint count);
        [DllImport(Library, EntryPoint = "ma_oni_colony_biology", CallingConvention = CallingConvention.Cdecl)]
        private static extern int Biology(ref Summary summary, [Out] Account[]? accounts, uint capacity, out uint count);

        internal static bool Supported { get; private set; }
        internal static string ConfigureFailure { get; private set; } = "";
        internal static long RoutedEdits { get; private set; }

        internal static void Reset() { RoutedEdits = 0; }

        // The game's disease table, as its native simulation receives it: per
        // disease its strength and temperature rules, per element its growth.
        // Without it a new colony keeps the legacy mode (native germ evolution
        // of the projected identity) and a world-germ save cannot load.
        internal static void Configure()
        {
            Supported = false; ConfigureFailure = "";
            MixtureInspection.SetCellGermReader(Describe);
            try { if (Version() != 1) { ConfigureFailure = "native world germ capability differs"; return; } }
            catch (EntryPointNotFoundException) { ConfigureFailure = "native library has no world germ evolution"; return; }
            if (Marshal.SizeOf<Disease>() != 48 || Marshal.SizeOf<Growth>() != 32 || Marshal.SizeOf<GermEdit>() != 16 ||
                Marshal.SizeOf<Summary>() != 312 || Marshal.SizeOf<Account>() != 64)
                throw new InvalidOperationException("World germ ABI layouts differ from the native library.");
            var diseases = new List<Disease>(); var growth = new List<Growth>();
            var elements = ElementLoader.elements.Count;
            try
            {
                var table = Db.Get().Diseases;
                for (var i = 0; i < table.Count; ++i)
                {
                    var d = table[i];
                    if (d.elemGrowthInfo == null || d.elemGrowthInfo.Length < elements)
                        throw new InvalidOperationException("Disease " + d.Id + " has no growth rules for every element.");
                    diseases.Add(new Disease
                    {
                        Size = 48, Version = 1, Strength = d.strength,
                        Range0 = d.temperatureRange.minViable, Range1 = d.temperatureRange.minGrowth, Range2 = d.temperatureRange.maxGrowth, Range3 = d.temperatureRange.maxViable,
                        HalfLife0 = d.temperatureHalfLives.minViable, HalfLife1 = d.temperatureHalfLives.minGrowth,
                        HalfLife2 = d.temperatureHalfLives.maxGrowth, HalfLife3 = d.temperatureHalfLives.maxViable
                    });
                    for (var e = 0; e < elements; ++e)
                    {
                        var g = d.elemGrowthInfo[e];
                        growth.Add(new Growth
                        {
                            UnderPopulationDeathRate = g.underPopulationDeathRate, PopulationHalfLife = g.populationHalfLife,
                            OverPopulationHalfLife = g.overPopulationHalfLife, DiffusionScale = g.diffusionScale,
                            MinimumCountPerKg = g.minCountPerKG, MaximumCountPerKg = g.maxCountPerKG,
                            MinimumDiffusionCount = g.minDiffusionCount, MinimumDiffusionTicks = g.minDiffusionInfestationTickCount
                        });
                    }
                }
                OniNativeColony.Check(SetDiseases(diseases.ToArray(), (uint)diseases.Count, growth.ToArray(), (uint)elements));
                Supported = true;
            }
            catch (Exception error)
            {
                ConfigureFailure = error.Message;
                try { OniNativeColony.Check(SetDiseases(Array.Empty<Disease>(), 0, Array.Empty<Growth>(), 0)); } catch (Exception) { }
                UnityEngine.Debug.LogWarning("[Mixed Atmospheres] World germ rules unavailable; native germ evolution of the shown germ remains: " + error.Message);
            }
        }

        // ModifyDiseaseOnCell (cell, disease byte, three pad bytes, count): the
        // owner takes an owned cell's edit and the native record keeps a zero
        // count, so the batch still reaches native code for other cells.
        internal static unsafe void Route(int length, int count, byte* payload, Func<int, bool> owned)
        {
            if (length != 12 || !Supported) return;
            List<GermEdit>? edits = null;
            for (var k = 0; k < count; ++k)
            {
                var record = payload + k * length;
                var cell = *(int*)record; var disease = record[4]; var change = *(int*)(record + 8);
                if (disease == byte.MaxValue || change == 0 || !owned(cell)) continue;
                (edits ??= new List<GermEdit>()).Add(new GermEdit { PublicCell = (uint)cell, Disease = disease + 1u, Change = change });
                *(int*)(record + 8) = 0;
            }
            if (edits == null) return;
            OniNativeColony.Check(QueueGerms(edits.ToArray(), (uint)edits.Count));
            RoutedEdits += edits.Count;
        }

        // Every disease identity of an owned cell, most numerous first, for the
        // hover and details (the game's grid shows only the most numerous).
        private static readonly uint[] one = new uint[1];
        internal static string? Describe(int cell)
        {
            if (!ColonyOwner.Active || !ColonyOwner.OwnsFluidCell(cell)) return null;
            one[0] = (uint)cell;
            WorldPassiveSnapshot? snapshot = null;
            for (var attempt = 0; attempt < 4 && snapshot == null; ++attempt)
                if (ColonyWorldPassives.Read(OniNativeColony.ReadPassives, 0, 0, one, out snapshot) is var result && result != ColonyWorldPassives.Busy && result != 0) return null;
            if (snapshot == null) return null;
            var germs = new List<(string name, double count)>();
            var diseases = Db.Get().Diseases;
            foreach (var row in snapshot.Rows)
                if (row.Identity >= 1 && row.Identity <= (ulong)diseases.Count && row.Quantity >= 1)
                    germs.Add((diseases[(int)row.Identity - 1].Name, Math.Floor(row.Quantity)));
            if (germs.Count == 0) return null;
            germs.Sort((a, b) => b.count.CompareTo(a.count));
            return "Germs: " + string.Join(" · ", germs.ConvertAll(g => g.name + " " + g.count.ToString("N0", System.Globalization.CultureInfo.InvariantCulture)));
        }

        internal static bool TryRead(out Summary summary, out Account[] accounts)
        {
            summary = Summary.Empty; accounts = Array.Empty<Account>();
            if (!Supported || !ColonyOwner.Active) return false;
            var buffer = new Account[256];
            OniNativeColony.Check(Biology(ref summary, buffer, (uint)buffer.Length, out var count));
            accounts = new Account[count]; Array.Copy(buffer, accounts, (int)count);
            return true;
        }

        internal static object Report()
        {
            if (!TryRead(out var s, out var accounts)) return new { Supported, ConfigureFailure, RoutedEdits };
            return new
            {
                Supported, s.Mode, s.Frames, s.Cultures, s.GermCells, s.ExpectedCultures, s.Born, s.Died, s.Culled, s.Moved, s.EditedIn, s.EditedOut,
                s.AuditError, s.OracleComparisons, s.OracleMismatches, s.OracleExtinctions, s.OracleBorn, s.OracleDied, s.OracleLast, RoutedEdits,
                Accounts = Array.ConvertAll(accounts, a => new { a.Identity, a.Born, a.Died, a.Culled, a.Moved, a.EditedIn, a.EditedOut, a.Held })
            };
        }
    }
}
