using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Oni
{
    internal readonly struct FluidMessageElementBinding
    {
        internal ushort ElementIndex { get; }
        internal NativeMatterPhase Phase { get; }
        internal string? MaterialId { get; }
        internal FluidMessageElementBinding(ushort index, NativeMatterPhase phase, string? materialId = null)
        { ElementIndex = index; Phase = phase; MaterialId = materialId; }
    }

    // A deliberately unhooked, fail-closed first adapter. It does not claim
    // ownership of emitter/consumer registries, displacement, pipes, reactions,
    // radius searches or native fluid-solid exchanges. A coordinator must not
    // enable colony takeover just because these four message families work.
    internal sealed class ExperimentalFluidMessageRouter
    {
        private const int ModifyCell = -1252920804, ModifyEnergy = 818320644;
        private const int ConsumeMass = 1727657959, EmitMass = 797274363;
        private readonly ColonyFluidState state;
        private readonly FluidMaterialCatalog catalog;
        private readonly Dictionary<ushort, FluidMessageElementBinding> elements;
        private readonly Dictionary<int, byte> diseaseIndices;
        private readonly Dictionary<byte, int> diseaseHashes;

        internal ExperimentalFluidMessageRouter(ColonyFluidState state, FluidMaterialCatalog catalog,
            IEnumerable<FluidMessageElementBinding> elementBindings, IReadOnlyDictionary<byte, int> diseaseBindings)
        {
            this.state = state ?? throw new ArgumentNullException(nameof(state));
            this.catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
            var snapshot = state.CaptureSnapshot();
            if (snapshot.MaterialModelVersion != catalog.Version || snapshot.MaterialModelFingerprint != catalog.Fingerprint)
                throw new ArgumentException("Router and colony authority require the identical material model.");
            if (elementBindings == null) throw new ArgumentNullException(nameof(elementBindings));
            if (diseaseBindings == null) throw new ArgumentNullException(nameof(diseaseBindings));
            elements = new Dictionary<ushort, FluidMessageElementBinding>();
            var materialIds = new HashSet<string>(StringComparer.Ordinal);
            foreach (var binding in elementBindings)
            {
                if (elements.ContainsKey(binding.ElementIndex) || !Enum.IsDefined(typeof(NativeMatterPhase), binding.Phase))
                    throw new ArgumentException("Duplicate or invalid element binding.");
                if (IsFluid(binding.Phase) && (string.IsNullOrWhiteSpace(binding.MaterialId) || !materialIds.Add(binding.MaterialId!)))
                    throw new ArgumentException("Fluid elements require distinct stable material identities.");
                if (!IsFluid(binding.Phase) && binding.MaterialId != null)
                    throw new ArgumentException("Nonfluid bindings cannot name fluid inventories.");
                // Unmodeled native fluids remain identifiable so routing can
                // explicitly reject them rather than pass them to native ONI.
                if (IsFluid(binding.Phase) && catalog.Materials.TryGetValue(binding.MaterialId!, out var material) && material.Phase != binding.Phase)
                    throw new ArgumentException("Element phase disagrees with the material catalog.");
                elements.Add(binding.ElementIndex, binding);
            }
            if (elements.Values.Count(binding => binding.Phase == NativeMatterPhase.Vacuum) != 1)
                throw new ArgumentException("Exactly one native vacuum identity is required.");
            diseaseHashes = new Dictionary<byte, int>(); diseaseIndices = new Dictionary<int, byte>();
            foreach (var binding in diseaseBindings)
            {
                if (binding.Key == byte.MaxValue || binding.Value == 0 || diseaseIndices.ContainsKey(binding.Value))
                    throw new ArgumentException("Disease bindings must be distinct nonzero identities.");
                diseaseHashes.Add(binding.Key, binding.Value); diseaseIndices.Add(binding.Value, binding.Key);
            }
        }

        // false means a specifically recognized nonfluid native operation, with
        // no custom state changed. Unknown or unsupported operations THROW.
        // No game callbacks run and no custom revision publishes during staging.
        internal bool TryStage(int message, ReadOnlySpan<byte> payload, out StagedFluidMutation? pending)
        {
            pending = null;
            using var failureGuard = new TransactionGuard(state.BeginTransaction());
            var transaction = failureGuard.Transaction;
            switch (message)
            {
                case ModifyCell:
                    RequireLength(payload, 28);
                    pending = StageModification(transaction, payload);
                    break;
                case ModifyEnergy:
                    RequireLength(payload, 16);
                    pending = StageHeat(transaction, payload);
                    break;
                case ConsumeMass:
                    RequireLength(payload, 16);
                    pending = StageConsumption(transaction, payload);
                    break;
                case EmitMass:
                    RequireLength(payload, 24);
                    pending = StageEmission(transaction, payload);
                    break;
                default:
                    throw new NotSupportedException("Custom fluid message adapter is not implemented: " + message);
            }
            if (pending == null) return false;
            failureGuard.Detach(); return true;
        }

        private StagedFluidMutation? StageModification(FluidTransaction transaction, ReadOnlySpan<byte> payload)
        {
            var cell = Int(payload, 0); var callback = Callback(payload, 4); var temperature = Float(payload, 8);
            var mass = Float(payload, 12); var diseaseCount = Int(payload, 16);
            var element = Element(UShort(payload, 20)); var replace = payload[22]; var diseaseIndex = payload[23];
            if (replace > 2 || payload[24] > 1) throw new InvalidDataException("Unrecognized cell replacement/displacement mode.");
            Nonnegative(diseaseCount, "disease count");
            var before = transaction.SampleCell(cell);
            if (element.Phase == NativeMatterPhase.Solid)
            {
                if (before.Inventory.TotalMassKg != 0) throw new NotSupportedException("Native solid placement requires coupled custom-fluid displacement.");
                return null;
            }
            if (before.IsSolid)
            {
                if (element.Phase == NativeMatterPhase.Vacuum) return null;
                throw new NotSupportedException("Fluid placement cannot replace or displace a native solid in this adapter.");
            }
            if (replace == 2 && before.Inventory.TotalMassKg != 0)
                throw new NotSupportedException("ReplaceAndDisplace requires an atomic neighboring-capacity transaction.");
            if (replace != 0 && mass < 0) throw new InvalidDataException("A replacement cannot request negative mass.");
            if (diseaseCount != 0 && mass <= 0)
                throw new NotSupportedException("Disease-only/removal deltas need their own conserved adapter.");

            if (replace != 0)
            {
                if (before.Inventory.TotalMassKg != 0)
                    transaction.Extract(cell, before.Inventory.TotalMassKg, "oni-replace-old-fluid");
                else if (before.Inventory.DiseaseCounts.Count != 0 || before.Inventory.RadiationQuantity != 0)
                    throw new NotSupportedException("Replacing massless fluid metadata needs an explicit metadata transaction.");
            }
            if (element.Phase == NativeMatterPhase.Vacuum)
            {
                if (mass > 0) throw new NotSupportedException("Positive ModifyMass without a material is ambiguous in a mixture.");
                if (mass < 0 && before.Inventory.TotalMassKg > 0)
                    transaction.Extract(cell, Math.Min(-(double)mass, before.Inventory.TotalMassKg), "oni-modify-bulk-mass");
            }
            else if (mass > 0)
                transaction.Inject(cell, Parcel(element, mass, temperature, diseaseIndex, diseaseCount), "oni-modify-fluid-source");
            else if (mass < 0)
            {
                var material = Modeled(element);
                before.Inventory.MassesKg.TryGetValue(material.Id, out var available);
                var actual = Math.Min(-(double)mass, available);
                if (actual > 0) transaction.ExtractSpecies(cell, material.Id, actual, "oni-modify-selected-mass");
            }
            return new StagedFluidMutation(transaction, cell, callback < 0 ? Array.Empty<Sim.CallbackInfo>() :
                new[] { new Sim.CallbackInfo { callbackIdx = callback } });
        }

        private StagedFluidMutation? StageHeat(FluidTransaction transaction, ReadOnlySpan<byte> payload)
        {
            var cell = Int(payload, 0); var joules = (double)Float(payload, 4) * 1000;
            var maximumTemperature = Float(payload, 8); var source = Int(payload, 12);
            if (maximumTemperature <= 0) throw new InvalidDataException("Cell heating needs a positive temperature limit.");
            var before = transaction.SampleCell(cell);
            if (before.IsSolid) return null;
            if (before.Inventory.TotalMassKg == 0 && joules != 0)
                throw new NotSupportedException("Vacuum cannot accept fluid energy; the source must retain unapplied heat.");
            var heatCapacity = before.Inventory.MassesKg.Sum(pair => catalog.Materials[pair.Key].SpecificHeatCvJoulesPerKgKelvin * pair.Value);
            if (joules > 0 && before.TemperatureKelvin + joules / heatCapacity > maximumTemperature)
                throw new NotSupportedException("Temperature-limited heat requires reserving and returning unapplied energy.");
            transaction.ApplyHeat(cell, joules, "oni-cell-energy-source-" + source.ToString(CultureInfo.InvariantCulture));
            return new StagedFluidMutation(transaction, cell);
        }

        private StagedFluidMutation? StageConsumption(FluidTransaction transaction, ReadOnlySpan<byte> payload)
        {
            var cell = Int(payload, 0); var callback = Callback(payload, 4); var mass = Float(payload, 8);
            var element = Element(UShort(payload, 12)); Nonnegative(mass, "consumption mass");
            if (payload[14] != 0 || payload[15] != 0)
                throw new NotSupportedException("Radius/rectangle consumption needs an explicit multi-cell reservation adapter.");
            var before = transaction.SampleCell(cell);
            if (!IsFluid(element.Phase))
            {
                if (before.Inventory.TotalMassKg == 0) return null;
                throw new NotSupportedException("A nonfluid consumer cannot debit a custom mixture.");
            }
            if (before.IsSolid) throw new NotSupportedException("Fluid consumption target is a native solid.");
            var material = Modeled(element);
            before.Inventory.MassesKg.TryGetValue(material.Id, out var available);
            var actual = Math.Min((double)mass, available);
            var parcel = actual == 0 ? FluidInventory.Empty : transaction.ExtractSpecies(cell, material.Id, actual, "oni-selected-consumer");
            var callbacks = Array.Empty<Sim.MassConsumedCallback>();
            if (callback >= 0)
            {
                var disease = CallbackDisease(parcel);
                var temperature = actual == 0 ? 0 : before.TemperatureKelvin;
                callbacks = new[] { new Sim.MassConsumedCallback
                {
                    callbackIdx = callback, elemIdx = element.ElementIndex, mass = ExactFloat(actual, "consumed mass"),
                    temperature = ExactFloat(temperature, "consumed temperature"), diseaseIdx = disease.index, diseaseCount = disease.count
                } };
            }
            return new StagedFluidMutation(transaction, cell, consumed: callbacks);
        }

        private StagedFluidMutation? StageEmission(FluidTransaction transaction, ReadOnlySpan<byte> payload)
        {
            var cell = Int(payload, 0); var callback = Callback(payload, 4); var mass = Float(payload, 8);
            var temperature = Float(payload, 12); var diseaseCount = Int(payload, 16);
            var element = Element(UShort(payload, 20)); var diseaseIndex = payload[22];
            Nonnegative(mass, "emission mass"); Nonnegative(diseaseCount, "disease count");
            var before = transaction.SampleCell(cell);
            if (!IsFluid(element.Phase))
            {
                if (before.Inventory.TotalMassKg == 0) return null;
                throw new NotSupportedException("Nonfluid emission requires native/custom displacement coordination.");
            }
            if (before.IsSolid) throw new NotSupportedException("Emission into native solids requires displacement support.");
            if (mass == 0 && diseaseCount != 0) throw new NotSupportedException("Massless disease emission needs a metadata adapter.");
            if (mass > 0) transaction.Inject(cell, Parcel(element, mass, temperature, diseaseIndex, diseaseCount), "oni-mass-emission");
            var callbacks = callback < 0 ? Array.Empty<Sim.MassEmittedCallback>() : new[] { new Sim.MassEmittedCallback
            {
                callbackIdx = callback, elemIdx = element.ElementIndex, suceeded = 1, mass = mass,
                temperature = temperature, diseaseIdx = diseaseCount == 0 ? byte.MaxValue : diseaseIndex, diseaseCount = diseaseCount
            } };
            return new StagedFluidMutation(transaction, cell, emitted: callbacks);
        }

        private FluidInventory Parcel(FluidMessageElementBinding element, double mass, double temperature, byte diseaseIndex, int diseaseCount)
        {
            var material = Modeled(element);
            if (temperature <= 0) throw new InvalidDataException("Positive fluid mass requires positive temperature.");
            Dictionary<int, double>? diseases = null;
            if (diseaseCount != 0)
            {
                if (!diseaseHashes.TryGetValue(diseaseIndex, out var hash)) throw new NotSupportedException("Unknown emitted disease identity.");
                diseases = new Dictionary<int, double> { [hash] = diseaseCount };
            }
            return catalog.CreateInventory(new Dictionary<string, double> { [material.Id] = mass }, temperature, diseaseCounts: diseases);
        }

        private (byte index, int count) CallbackDisease(FluidInventory parcel)
        {
            if (parcel.DiseaseCounts.Count == 0) return (byte.MaxValue, 0);
            if (parcel.DiseaseCounts.Count != 1) throw new NotSupportedException("Vanilla callbacks cannot represent multiple diseases; use a mixture-aware inventory adapter.");
            var disease = parcel.DiseaseCounts.Single();
            if (!diseaseIndices.TryGetValue(disease.Key, out var index) || disease.Value > int.MaxValue || disease.Value != Math.Floor(disease.Value))
                throw new NotSupportedException("Callback disease projection requires an exact integral, registered disease quantity.");
            return (index, (int)disease.Value);
        }

        private FluidMaterial Modeled(FluidMessageElementBinding element)
        {
            if (!IsFluid(element.Phase) || element.MaterialId == null || !catalog.Materials.TryGetValue(element.MaterialId, out var material))
                throw new NotSupportedException("No explicit custom material model for native element index " + element.ElementIndex);
            return material;
        }
        private FluidMessageElementBinding Element(ushort index) => elements.TryGetValue(index, out var binding)
            ? binding : throw new InvalidDataException("Unknown native element index " + index);
        private static bool IsFluid(NativeMatterPhase phase) => phase == NativeMatterPhase.Gas || phase == NativeMatterPhase.Liquid;
        private static void RequireLength(ReadOnlySpan<byte> data, int length)
        { if (data.Length != length) throw new InvalidDataException("Native fluid-message payload length changed."); }
        private static int Int(ReadOnlySpan<byte> data, int offset) => BinaryPrimitives.ReadInt32LittleEndian(data.Slice(offset));
        private static ushort UShort(ReadOnlySpan<byte> data, int offset) => BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(offset));
        private static float Float(ReadOnlySpan<byte> data, int offset)
        {
            var value = BitConverter.Int32BitsToSingle(Int(data, offset));
            if (float.IsNaN(value) || float.IsInfinity(value)) throw new InvalidDataException("Nonfinite fluid-message value.");
            return value;
        }
        private static int Callback(ReadOnlySpan<byte> data, int offset)
        { var value = Int(data, offset); return value >= -1 ? value : throw new InvalidDataException("Invalid callback handle."); }
        private static void Nonnegative(double value, string name)
        { if (value < 0) throw new InvalidDataException("Negative " + name + "."); }
        private static float ExactFloat(double value, string name)
        {
            var result = (float)value;
            if ((double)result != value || float.IsNaN(result) || float.IsInfinity(result))
                throw new NotSupportedException("Vanilla callback cannot represent exact " + name + "; a precision-residual adapter is required.");
            return result;
        }

        private sealed class TransactionGuard : IDisposable
        {
            internal FluidTransaction Transaction { get; }
            private bool owns = true;
            internal TransactionGuard(FluidTransaction transaction) { Transaction = transaction; }
            internal void Detach() { owns = false; }
            public void Dispose() { if (owns) Transaction.Dispose(); }
        }
    }

    internal sealed class StagedFluidMutation : IDisposable
    {
        private FluidTransaction? transaction;
        private readonly FluidMutationCommit result;
        internal StagedFluidMutation(FluidTransaction transaction, int cell, Sim.CallbackInfo[]? callbacks = null,
            Sim.MassConsumedCallback[]? consumed = null, Sim.MassEmittedCallback[]? emitted = null)
        {
            this.transaction = transaction;
            result = new FluidMutationCommit(cell, callbacks ?? Array.Empty<Sim.CallbackInfo>(),
                consumed ?? Array.Empty<Sim.MassConsumedCallback>(), emitted ?? Array.Empty<Sim.MassEmittedCallback>());
        }
        internal FluidMutationCommit Commit()
        {
            if (transaction == null) throw new ObjectDisposedException(nameof(StagedFluidMutation));
            var snapshot = transaction.Commit();
            transaction = null; result.MarkCommitted(snapshot); return result;
        }
        public void Dispose() { transaction?.Dispose(); transaction = null; }
    }

    internal sealed class FluidMutationCommit
    {
        private ColonyFrameSnapshot? snapshot;
        private readonly Sim.CallbackInfo[] callbacks;
        private readonly Sim.MassConsumedCallback[] consumed;
        private readonly Sim.MassEmittedCallback[] emitted;
        internal int CellIndex { get; }
        internal ColonyFrameSnapshot Snapshot => snapshot ?? throw new InvalidOperationException("Fluid mutation has not committed.");
        internal ReadOnlySpan<Sim.CallbackInfo> Callbacks => callbacks;
        internal ReadOnlySpan<Sim.MassConsumedCallback> ConsumedCallbacks => consumed;
        internal ReadOnlySpan<Sim.MassEmittedCallback> EmittedCallbacks => emitted;
        internal FluidMutationCommit(int cell, Sim.CallbackInfo[] callbacks, Sim.MassConsumedCallback[] consumed, Sim.MassEmittedCallback[] emitted)
        { CellIndex = cell; this.callbacks = callbacks; this.consumed = consumed; this.emitted = emitted; }
        internal void MarkCommitted(ColonyFrameSnapshot completed) { snapshot = completed; }
        // The coordinator must append this batch exactly once to a completed
        // publication and retain it across any failed publication attempt.
        internal void AppendCallbacks(OwnedSnapshotOverlay overlay)
        {
            _ = Snapshot;
            overlay.AppendEvents<Sim.CallbackInfo>(SnapshotEventChannel.CallbackInfo, callbacks);
            overlay.AppendEvents<Sim.MassConsumedCallback>(SnapshotEventChannel.MassConsumedCallbacks, consumed);
            overlay.AppendEvents<Sim.MassEmittedCallback>(SnapshotEventChannel.MassEmittedCallbacks, emitted);
        }
    }
}
