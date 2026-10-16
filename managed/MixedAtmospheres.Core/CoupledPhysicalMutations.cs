using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Core
{
    /// <summary>A complete physical replacement of one existing fluid cell. Use native U, not display-projected total energy.</summary>
    public sealed class CoupledCellEdit
    {
        public ulong CellIndex { get; }
        public CoupledCellInitialization Value { get; }
        public CoupledCellEdit(ulong cellIndex, CoupledCellInitialization value)
        {
            Value = value ?? throw new ArgumentNullException(nameof(value));
            if (value.IsWall) throw new ArgumentException("A fluid mutation cannot change geometry.", nameof(value));
            CellIndex = cellIndex;
        }
    }

    /// <summary>Signed, caller-declared external accounts into a native world; does not reserve power or validate a process.</summary>
    public sealed class CoupledMutationBudget
    {
        public IReadOnlyDictionary<string, double> MassChangesKg { get; }
        public IReadOnlyDictionary<int, double> DiseaseChanges { get; }
        public double RadiationChange { get; }
        public double MaterialEnergyJoules { get; }
        public double ShaftWorkJoules { get; }
        public double HeatJoules { get; }
        public double MomentumX { get; }
        public double MomentumY { get; }
        public CoupledMutationBudget(IReadOnlyDictionary<string, double> massChangesKg,
            double materialEnergyJoules = 0, double shaftWorkJoules = 0, double heatJoules = 0,
            double momentumX = 0, double momentumY = 0,
            IReadOnlyDictionary<int, double>? diseaseChanges = null, double radiationChange = 0)
        {
            if (massChangesKg == null) throw new ArgumentNullException(nameof(massChangesKg));
            var masses = new Dictionary<string, double>(StringComparer.Ordinal);
            foreach (var entry in massChangesKg)
            { FluidChecks.Identity(entry.Key, nameof(massChangesKg)); FluidChecks.Finite(entry.Value, nameof(massChangesKg)); masses.Add(entry.Key, entry.Value); }
            var disease = new Dictionary<int, double>();
            if (diseaseChanges != null)
            {
                if (diseaseChanges.Count > FluidChecks.MaximumDiseaseKinds) throw new ArgumentException("Too many disease account identities.");
                foreach (var entry in diseaseChanges)
                {
                    if (entry.Key == 0) throw new ArgumentException("A disease identity cannot be zero.");
                    FluidChecks.Finite(entry.Value, nameof(diseaseChanges)); disease.Add(entry.Key, entry.Value);
                }
            }
            foreach (var value in new[] { materialEnergyJoules, shaftWorkJoules, heatJoules, momentumX, momentumY, radiationChange })
                FluidChecks.Finite(value, "mutation account");
            MassChangesKg = new ReadOnlyDictionary<string, double>(masses); DiseaseChanges = new ReadOnlyDictionary<int, double>(disease);
            MaterialEnergyJoules = materialEnergyJoules; ShaftWorkJoules = shaftWorkJoules; HeatJoules = heatJoules;
            MomentumX = momentumX; MomentumY = momentumY; RadiationChange = radiationChange;
        }
    }

    public sealed class CoupledMutationDiagnostics
    {
        public ulong Revision { get; }
        public ulong EditedCells { get; }
        public double InternalEnergyChangeJoules { get; }
        public double KineticEnergyChangeJoules { get; }
        public double PotentialEnergyChangeJoules { get; }
        public double EnergyResidualJoules { get; }
        public double EnergyBoundJoules { get; }
        public double MomentumXResidual { get; }
        public double MomentumYResidual { get; }
        public double MomentumXBound { get; }
        public double MomentumYBound { get; }
        public double MaximumSpeciesMassResidualKg { get; }
        public double MaximumSpeciesMassBoundRatio { get; }
        public double MaximumPassiveResidual { get; }
        public double MaximumPassiveBoundRatio { get; }
        internal CoupledMutationDiagnostics(CoupledNativeMutationResult value)
        {
            if (value.Reserved != 0) throw new NotSupportedException("Coupled mutation result metadata differs.");
            Revision = value.Revision; EditedCells = value.EditedCells;
            InternalEnergyChangeJoules = value.InternalEnergyChange; KineticEnergyChangeJoules = value.KineticEnergyChange;
            PotentialEnergyChangeJoules = value.PotentialEnergyChange; EnergyResidualJoules = value.EnergyResidual;
            EnergyBoundJoules = value.EnergyBound; MomentumXResidual = value.MomentumXResidual; MomentumYResidual = value.MomentumYResidual;
            MomentumXBound = value.MomentumXBound; MomentumYBound = value.MomentumYBound;
            MaximumSpeciesMassResidualKg = value.MaximumSpeciesMassResidual; MaximumSpeciesMassBoundRatio = value.MaximumSpeciesMassBoundRatio;
            MaximumPassiveResidual = value.MaximumPassiveResidual; MaximumPassiveBoundRatio = value.MaximumPassiveBoundRatio;
        }
    }
    public sealed class CoupledMutation
    {
        public CoupledSimulationWorld World { get; }
        public CoupledMutationDiagnostics Diagnostics { get; }
        public CoupledMutationBudget Budget { get; }
        internal CoupledMutation(CoupledSimulationWorld world, CoupledNativeMutationResult result, CoupledMutationBudget budget)
        { World = world; Diagnostics = new CoupledMutationDiagnostics(result); Budget = budget; }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativeEdit { internal ulong CellIndex; internal CoupledNativeInput Value; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativeMutationBudget
    {
        internal double MaterialEnergy, ShaftWork, Heat, MomentumX, MomentumY;
        internal ulong Reserved, Reserved2, Reserved3;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativeMutationResult
    {
        internal ulong Revision, EditedCells;
        internal double InternalEnergyChange, KineticEnergyChange, PotentialEnergyChange, EnergyResidual, EnergyBound;
        internal double MomentumXResidual, MomentumYResidual, MomentumXBound, MomentumYBound;
        internal double MaximumSpeciesMassResidual, MaximumSpeciesMassBoundRatio, MaximumPassiveResidual, MaximumPassiveBoundRatio;
        internal ulong Reserved;
    }
    internal static partial class CoupledNativeMethods
    {
        [DllImport(Library, EntryPoint = "ma_coupled_mutation_abi_version", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint MutationAbiVersion();
        [DllImport(Library, EntryPoint = "ma_coupled_prepare_mutation", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int PrepareMutation(CoupledWorldHandle world, ulong expectedRevision,
            [In] CoupledNativeEdit[] edits, ulong editCount, [In] double[] masses, ulong massCount,
            [In] ulong[] offsets, ulong offsetCount, [In] CoupledNativePassive[] entries, ulong entryCount,
            in CoupledNativeMutationBudget budget, [In] double[] massDeltas, ulong massDeltaCount,
            [In] CoupledNativePassive[] passiveDeltas, ulong passiveDeltaCount, out IntPtr candidate, out CoupledNativeMutationResult result);
        internal static void CheckMutationAbi()
        {
            try
            {
                if (Marshal.SizeOf<CoupledNativeEdit>() != 48 || Marshal.SizeOf<CoupledNativeMutationBudget>() != 64 ||
                    Marshal.SizeOf<CoupledNativeMutationResult>() != 128 || MutationAbiVersion() != 1)
                    throw new NotSupportedException("Coupled mutation ABI differs.");
            }
            catch (EntryPointNotFoundException error) { throw new NotSupportedException("Coupled library lacks the conserved mutation extension.", error); }
            CheckPassiveAbi();
        }
    }
    public sealed partial class CoupledSimulationWorld
    {
        /// <summary>
        /// Prepare an immutable physical candidate against an exact native revision.
        /// The caller must reserve other owners and validate its process before publishing it.
        /// This operation advances no time and never changes its source.
        /// </summary>
        public CoupledMutation PrepareMutation(ulong expectedRevision, IReadOnlyList<CoupledCellEdit> edits, CoupledMutationBudget budget)
        {
            if (edits == null) throw new ArgumentNullException(nameof(edits));
            if (budget == null) throw new ArgumentNullException(nameof(budget));
            if (edits.Count == 0 || edits.Count > 4096) throw new ArgumentException("Prepare 1..4096 complete fluid-cell edits.", nameof(edits));
            var source = edits.ToArray();
            if (source.Length == 0 || source.Length > 4096 || source.Any(value => value == null)) throw new ArgumentException("Invalid fluid-cell edit collection.");
            Array.Sort(source, (a, b) => a.CellIndex.CompareTo(b.CellIndex));
            lock (sync)
            {
                CheckOpen(); CoupledNativeMethods.CheckMutationAbi();
                var indices = materials.Select((definition, index) => (definition.Id, index)).ToDictionary(entry => entry.Id, entry => entry.index, StringComparer.Ordinal);
                var inputs = new CoupledNativeEdit[source.Length]; var masses = new double[checked(source.Length * materials.Length)];
                var offsets = new ulong[source.Length + 1]; var entries = new List<CoupledNativePassive>();
                for (var i = 0; i < source.Length; ++i)
                {
                    var edit = source[i]; var value = edit.Value;
                    if (edit.CellIndex >= (ulong)Width * Height || (i != 0 && edit.CellIndex == source[i - 1].CellIndex))
                        throw new ArgumentException("Fluid-cell edits must be distinct and in range.");
                    inputs[i] = new CoupledNativeEdit { CellIndex = edit.CellIndex, Value = new CoupledNativeInput {
                        InternalEnergy = value.InternalEnergyJoules, MomentumX = value.MomentumX, MomentumY = value.MomentumY, LiquidPressure = value.LiquidPressurePa } };
                    foreach (var entry in value.MassesKg)
                    {
                        if (!indices.TryGetValue(entry.Key, out var s)) throw new ArgumentException("Unknown coupled material: " + entry.Key);
                        masses[i * materials.Length + s] = entry.Value;
                    }
                    var sorted = new SortedDictionary<ulong, double>();
                    foreach (var entry in value.DiseaseCounts) sorted.Add(unchecked((uint)entry.Key), entry.Value);
                    if (value.RadiationQuantity != 0) sorted.Add(CoupledPassiveMetadata.RadiationIdentity, value.RadiationQuantity);
                    foreach (var entry in sorted) entries.Add(new CoupledNativePassive { Identity = entry.Key, Quantity = entry.Value });
                    if (entries.Count > 8000000) throw new ArgumentException("Coupled mutation metadata budget exceeded.");
                    offsets[i + 1] = (ulong)entries.Count;
                }
                var massDeltas = new double[materials.Length];
                foreach (var entry in budget.MassChangesKg)
                {
                    if (!indices.TryGetValue(entry.Key, out var s)) throw new ArgumentException("Unknown coupled material account: " + entry.Key);
                    massDeltas[s] = entry.Value;
                }
                var passiveMap = new SortedDictionary<ulong, double>();
                foreach (var entry in budget.DiseaseChanges) passiveMap.Add(unchecked((uint)entry.Key), entry.Value);
                if (budget.RadiationChange != 0) passiveMap.Add(CoupledPassiveMetadata.RadiationIdentity, budget.RadiationChange);
                var passiveDeltas = passiveMap.Select(entry => new CoupledNativePassive { Identity = entry.Key, Quantity = entry.Value }).ToArray();
                var nativeBudget = new CoupledNativeMutationBudget { MaterialEnergy = budget.MaterialEnergyJoules, ShaftWork = budget.ShaftWorkJoules,
                    Heat = budget.HeatJoules, MomentumX = budget.MomentumX, MomentumY = budget.MomentumY };
                var metadata = entries.ToArray();
                CoupledNativeMethods.Check(CoupledNativeMethods.PrepareMutation(handle, expectedRevision, inputs, (ulong)inputs.Length, masses, (ulong)masses.Length,
                    offsets, (ulong)offsets.Length, metadata, (ulong)metadata.Length, nativeBudget, massDeltas, (ulong)massDeltas.Length,
                    passiveDeltas, (ulong)passiveDeltas.Length, out var pointer, out var result));
                var child = Adopt(pointer, materials, true);
                try { return new CoupledMutation(child, result, budget); } catch { child.Dispose(); throw; }
            }
        }
    }
}
