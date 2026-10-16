using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Core
{
    internal static class CoupledPassiveMetadata
    {
        internal const ulong RadiationIdentity = 0x100000000UL;
        internal static readonly IReadOnlyDictionary<int, double> Empty = new ReadOnlyDictionary<int, double>(new Dictionary<int, double>());
        internal static IReadOnlyDictionary<int, double> Copy(IReadOnlyDictionary<int, double> source)
        {
            if (source == null) throw new ArgumentNullException(nameof(source));
            if (source.Count > FluidChecks.MaximumDiseaseKinds) throw new ArgumentException("Too many disease identities.");
            var result = new Dictionary<int, double>();
            foreach (var entry in source)
            {
                if (entry.Key == 0) throw new ArgumentException("A disease identity cannot be zero.");
                FluidChecks.Nonnegative(entry.Value, "disease count");
                if (entry.Value != 0) result.Add(entry.Key, entry.Value);
            }
            return new ReadOnlyDictionary<int, double>(result);
        }
    }
    public sealed partial class CoupledCellInitialization
    {
        public IReadOnlyDictionary<int, double> DiseaseCounts { get; } = CoupledPassiveMetadata.Empty;
        public double RadiationQuantity { get; }
        private CoupledCellInitialization(CoupledCellInitialization source, IReadOnlyDictionary<int, double> disease, double radiation)
            : this(source.MassesKg, source.InternalEnergyJoules, source.MomentumX, source.MomentumY, source.LiquidPressurePa, source.IsWall)
        {
            DiseaseCounts = CoupledPassiveMetadata.Copy(disease); FluidChecks.Nonnegative(radiation, nameof(radiation));
            if (IsWall && (DiseaseCounts.Count != 0 || radiation != 0)) throw new ArgumentException("Native solid metadata belongs to its nonfluid owner.");
            RadiationQuantity = radiation;
        }
        public CoupledCellInitialization WithPassiveMetadata(IReadOnlyDictionary<int, double> diseaseCounts, double radiationQuantity = 0) =>
            new CoupledCellInitialization(this, diseaseCounts, radiationQuantity);
    }
    public sealed partial class CoupledCellSnapshot
    {
        public IReadOnlyDictionary<int, double> DiseaseCounts { get; } = CoupledPassiveMetadata.Empty;
        public double RadiationQuantity { get; }
        internal CoupledCellSnapshot(CoupledNativeCell value, Dictionary<string, double> masses, Dictionary<int, double> disease, double radiation)
            : this(value, masses)
        { DiseaseCounts = new ReadOnlyDictionary<int, double>(disease); RadiationQuantity = radiation; }
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativePassive { internal ulong Identity; internal double Quantity; }
    internal static partial class CoupledNativeMethods
    {
        [DllImport(Library, EntryPoint = "ma_coupled_passive_abi_version", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint PassiveAbiVersion();
        [DllImport(Library, EntryPoint = "ma_coupled_create_full", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int CreateFull(in CoupledNativeConfig config, [In] CoupledNativeInput[] cells, ulong cellCount,
            [In] double[] masses, ulong massCount, [In] ulong[] offsets, ulong offsetCount,
            [In] CoupledNativePassive[] entries, ulong entryCount, out IntPtr world);
        [DllImport(Library, EntryPoint = "ma_coupled_passive_count", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int PassiveCount(CoupledWorldHandle world, out ulong count);
        [DllImport(Library, EntryPoint = "ma_coupled_copy_full_snapshot", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int FullSnapshot(CoupledWorldHandle world, out CoupledNativeSnapshot snapshot,
            [Out] CoupledNativeCell[] cells, ulong cellCapacity, [Out] double[] masses, ulong massCapacity,
            [Out] ulong[] offsets, ulong offsetCapacity, [Out] CoupledNativePassive[] entries, ulong entryCapacity);
        internal static void CheckPassiveAbi()
        {
            try { if (Marshal.SizeOf<CoupledNativePassive>() != 16 || PassiveAbiVersion() != 1) throw new NotSupportedException("Coupled passive metadata ABI differs."); }
            catch (EntryPointNotFoundException error) { throw new NotSupportedException("Coupled library lacks the passive metadata extension.", error); }
        }
    }
    public sealed partial class CoupledSimulationWorld
    {
        private static IntPtr CreateNative(CoupledNativeConfig config, CoupledNativeInput[] cells, double[] masses,
            CoupledCellInitialization[] source, bool passives)
        {
            IntPtr pointer;
            if (!passives)
                CoupledNativeMethods.Check(CoupledNativeMethods.Create(config, cells, (ulong)cells.Length, masses, (ulong)masses.Length, out pointer));
            else
            {
                CoupledNativeMethods.CheckPassiveAbi();
                var offsets = new ulong[cells.Length + 1]; var entries = new List<CoupledNativePassive>();
                for (var i = 0; i < cells.Length; ++i)
                {
                    var sorted = new SortedDictionary<ulong, double>();
                    foreach (var entry in source[i].DiseaseCounts) sorted.Add(unchecked((uint)entry.Key), entry.Value);
                    if (source[i].RadiationQuantity != 0) sorted.Add(CoupledPassiveMetadata.RadiationIdentity, source[i].RadiationQuantity);
                    foreach (var entry in sorted) entries.Add(new CoupledNativePassive { Identity = entry.Key, Quantity = entry.Value });
                    if (entries.Count > 8000000) throw new ArgumentException("Coupled passive record budget exceeded.");
                    offsets[i + 1] = (ulong)entries.Count;
                }
                var values = entries.ToArray();
                CoupledNativeMethods.Check(CoupledNativeMethods.CreateFull(config, cells, (ulong)cells.Length, masses, (ulong)masses.Length,
                    offsets, (ulong)offsets.Length, values, (ulong)values.Length, out pointer));
            }
            return pointer;
        }
        private CoupledNativeSnapshot ReadNativeSnapshot(CoupledNativeCell[] cells, double[] masses,
            out ulong[]? offsets, out CoupledNativePassive[]? entries)
        {
            offsets = null; entries = null;
            if (!hasPassiveMetadata)
            {
                CoupledNativeMethods.Check(CoupledNativeMethods.Snapshot(handle, out var basic, cells, (ulong)cells.Length, masses, (ulong)masses.Length));
                return basic;
            }
            CoupledNativeMethods.Check(CoupledNativeMethods.PassiveCount(handle, out var count));
            if (count > 8000000) throw new NotSupportedException("Native passive record budget differs.");
            offsets = new ulong[cells.Length + 1]; entries = new CoupledNativePassive[checked((int)count)];
            CoupledNativeMethods.Check(CoupledNativeMethods.FullSnapshot(handle, out var header, cells, (ulong)cells.Length, masses, (ulong)masses.Length,
                offsets, (ulong)offsets.Length, entries, (ulong)entries.Length));
            if (offsets[0] != 0 || offsets[cells.Length] != count) throw new NotSupportedException("Native passive offsets differ.");
            return header;
        }
        private static CoupledCellSnapshot ReadPassiveCell(CoupledNativeCell cell, Dictionary<string, double> masses, int index,
            ulong[] offsets, CoupledNativePassive[] entries)
        {
            var start = offsets[index]; var end = offsets[index + 1];
            if (start > end || end > (ulong)entries.Length || end - start > 65537) throw new NotSupportedException("Invalid native passive row.");
            var disease = new Dictionary<int, double>(); double radiation = 0; ulong previous = 0;
            for (var i = start; i < end; ++i)
            {
                var entry = entries[checked((int)i)];
                if (entry.Identity <= previous || entry.Identity > CoupledPassiveMetadata.RadiationIdentity || entry.Quantity <= 0 ||
                    double.IsNaN(entry.Quantity) || double.IsInfinity(entry.Quantity)) throw new NotSupportedException("Invalid native passive value.");
                previous = entry.Identity;
                if (entry.Identity == CoupledPassiveMetadata.RadiationIdentity) radiation = entry.Quantity;
                else disease.Add(unchecked((int)(uint)entry.Identity), entry.Quantity);
            }
            return new CoupledCellSnapshot(cell, masses, disease, radiation);
        }
    }
}
