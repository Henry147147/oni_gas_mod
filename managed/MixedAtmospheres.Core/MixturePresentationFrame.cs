using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace MixedAtmospheres.Core
{
    public sealed class MixtureContainerSample
    {
        public string Id { get; }
        public FluidInventory Inventory { get; }
        public double VolumeCubicMetres { get; }
        public long Revision { get; }
        public MixtureContainerSample(string id, FluidInventory inventory, double volumeCubicMetres, long revision)
        {
            FluidChecks.Identity(id, nameof(id)); FluidChecks.Positive(volumeCubicMetres, nameof(volumeCubicMetres));
            if (revision < 0) throw new ArgumentOutOfRangeException(nameof(revision));
            Id = id; Inventory = inventory ?? throw new ArgumentNullException(nameof(inventory)); VolumeCubicMetres = volumeCubicMetres; Revision = revision;
        }
    }

    /// <summary>Allocation-free immutable cell/container reads after capture, suitable for rendering workers.</summary>
    public sealed class MixturePresentationFrame
    {
        private readonly ColonyGeometry geometry;
        public int Width => geometry.Width;
        public int Height => geometry.Height;
        public long Revision { get; }
        public long CompletedFrame { get; }
        public double SimulationTimeSeconds { get; }
        public string MaterialModelVersion { get; }
        public string MaterialModelFingerprint { get; }
        public IReadOnlyDictionary<int, MixturePresentation> Cells { get; }
        public IReadOnlyDictionary<string, MixturePresentation> Containers { get; }
        public MixturePresentation Vacuum { get; }
        public bool IsReadOnly => true;

        internal MixturePresentationFrame(MixturePresentationModel model, ColonyFrameSnapshot frame, IEnumerable<MixtureContainerSample>? containers)
        {
            if (frame == null) throw new ArgumentNullException(nameof(frame)); model.RequireCatalog(frame);
            geometry = frame.Geometry; Revision = frame.Revision; CompletedFrame = frame.CompletedFrame;
            SimulationTimeSeconds = frame.SimulationTimeSeconds; MaterialModelVersion = frame.MaterialModelVersion;
            MaterialModelFingerprint = frame.MaterialModelFingerprint;
            var cells = new Dictionary<int, MixturePresentation>(frame.Inventories.Count);
            foreach (var entry in frame.Inventories)
                cells.Add(entry.Key, model.DescribeCell(entry.Value, frame.CellVolumeCubicMetres, frame.Revision, entry.Key));
            Cells = new ReadOnlyDictionary<int, MixturePresentation>(cells);
            var containerViews = new Dictionary<string, MixturePresentation>(StringComparer.Ordinal);
            if (containers != null)
                foreach (var container in containers)
                {
                    if (container == null || container.Revision != Revision || containerViews.ContainsKey(container.Id))
                        throw new ArgumentException("Container samples must be non-null, unique and captured at the same revision as the colony.");
                    containerViews.Add(container.Id, model.Describe(container.Inventory, container.VolumeCubicMetres, Revision, container.Id));
                }
            Containers = new ReadOnlyDictionary<string, MixturePresentation>(containerViews);
            Vacuum = model.Describe(FluidInventory.Empty, frame.CellVolumeCubicMetres, Revision, "owned-empty-cell");
        }

        public bool TrySampleCell(int cell, out MixturePresentation? presentation)
        {
            presentation = null;
            if (!geometry.TryLocate(cell, out _, out var element) || element != geometry.VacuumHash) return false;
            presentation = Cells.TryGetValue(cell, out var occupied) ? occupied : Vacuum; return true;
        }

        public MixturePresentation SampleCell(int cell)
        {
            if (!TrySampleCell(cell, out var presentation)) throw new ArgumentOutOfRangeException(nameof(cell), "Cell is solid or outside the owned colony.");
            return presentation!;
        }

        public bool TrySampleContainer(string id, out MixturePresentation? presentation)
        { return Containers.TryGetValue(id, out presentation); }

        public MixturePresentation SampleContainer(string id)
        {
            if (!TrySampleContainer(id, out var presentation)) throw new KeyNotFoundException("No mixture inventory was captured for " + id);
            return presentation!;
        }
    }
}
