using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using MixedAtmospheres.Oni;

// ColonyOwner's overlay pass (ColonyOverlay) against the loop it replaced,
// kept here verbatim: a full pass, and the ownership-run pass a view-cache
// hit takes, must write the same planes and emit the same events in the
// same order for any copy and Grid.Element state.
internal static unsafe class OverlayEquivalenceTests
{
    private sealed class World
    {
        internal int GridWidth, GridHeight;
        internal int[] Map = Array.Empty<int>();
        internal OniNativeColony.CellView[] Views = Array.Empty<OniNativeColony.CellView>();
        internal float[] Masses = Array.Empty<float>(), Temperatures = Array.Empty<float>(), Radiation = Array.Empty<float>();
        internal ushort[] Elements = Array.Empty<ushort>();
        internal byte[] Diseases = Array.Empty<byte>();
        internal int[] Germs = Array.Empty<int>();
        internal Element?[] Shown = Array.Empty<Element?>();
        internal World Clone() => new World
        {
            GridWidth = GridWidth, GridHeight = GridHeight, Map = Map, Views = Views, Shown = Shown,
            Masses = (float[])Masses.Clone(), Temperatures = (float[])Temperatures.Clone(), Radiation = (float[])Radiation.Clone(),
            Elements = (ushort[])Elements.Clone(), Diseases = (byte[])Diseases.Clone(), Germs = (int[])Germs.Clone()
        };
    }
    private sealed class Result
    {
        internal int Owned;
        internal byte[] Changes = Array.Empty<byte>(), Irradiated = Array.Empty<byte>();
        internal World Planes = null!;
    }

    internal static void Run(Action<bool> check)
    {
        var catalogue = new List<Element>();
        for (var k = 0; k < 40; ++k)
        {
            var element = (Element)RuntimeHelpers.GetUninitializedObject(typeof(Element));
            element.idx = (ushort)k;
            element.state = k < 12 ? Element.State.Solid : k < 26 ? Element.State.Gas : Element.State.Liquid;
            catalogue.Add(element);
        }
        var solid = ColonyOverlay.SolidElements(catalogue)!;
        check(solid != null && solid.Length == 40 && solid[3] && !solid[20] && !solid[30]);
        Check(catalogue, check, seed: 1, radiation: true, x: 3, y: 2, width: 14, height: 8, gridWidth: 20, gridHeight: 12);
        Check(catalogue, check, seed: 2, radiation: false, x: 0, y: 0, width: 31, height: 17, gridWidth: 31, gridHeight: 17);
        Check(catalogue, check, seed: 3, radiation: true, x: 5, y: 0, width: 1, height: 9, gridWidth: 7, gridHeight: 9);
        // A missing entry keeps the original loop (no table).
        var broken = new List<Element>(catalogue) { null! };
        check(ColonyOverlay.SolidElements(broken) == null);
        ColonyOverlay.Reset();
    }

    private static void Check(List<Element> catalogue, Action<bool> check, int seed, bool radiation, int x, int y, int width, int height, int gridWidth, int gridHeight)
    {
        var random = new Random(seed);
        var world = Generate(random, catalogue, radiation, x, y, width, height, gridWidth, gridHeight);
        ColonyOverlay.Reset();
        var expected = Reference(world.Clone(), catalogue);
        var full = Overlay(world.Clone(), catalogue, unchanged: true); // no runs yet: a full pass
        check(ColonyOverlay.FullPasses == 1 && ColonyOverlay.RunPasses == 0 && ColonyOverlay.Runs > 0);
        Same(expected, full, check);
        for (var round = 0; round < 6; ++round)
        {
            // The same views (a cache hit) with a new copy and a new Grid.Element.
            Perturb(random, world, catalogue);
            expected = Reference(world.Clone(), catalogue);
            var runs = Overlay(world.Clone(), catalogue, unchanged: true);
            check(ColonyOverlay.RunPasses == round + 1 && ColonyOverlay.FullPasses == 1);
            Same(expected, runs, check);
        }
        // Views that were written again, or another views array, take a full pass.
        var fresh = Overlay(world.Clone(), catalogue, unchanged: false);
        check(ColonyOverlay.FullPasses == 2);
        Same(Reference(world.Clone(), catalogue), fresh, check);
        var moved = world.Clone(); moved.Views = (OniNativeColony.CellView[])world.Views.Clone();
        for (var i = 0; i < moved.Views.Length; i += 3) moved.Views[i].Flags ^= OniNativeColony.ViewOwned;
        for (var i = 0; i < moved.Shown.Length; ++i) if (moved.Shown[i] == null) moved.Shown[i] = catalogue[1];
        var reshaped = Overlay(moved.Clone(), catalogue, unchanged: true);
        check(ColonyOverlay.FullPasses == 3);
        Same(Reference(moved.Clone(), catalogue), reshaped, check);
        // Forgetting the runs (another overlay path ran) forces a full pass.
        ColonyOverlay.Forget();
        Overlay(moved.Clone(), catalogue, unchanged: true);
        check(ColonyOverlay.FullPasses == 4);
    }

    private static World Generate(Random random, List<Element> catalogue, bool radiation, int x, int y, int width, int height, int gridWidth, int gridHeight)
    {
        var cells = gridWidth * gridHeight;
        var world = new World
        {
            GridWidth = gridWidth, GridHeight = gridHeight, Map = ColonyPublicationMap.Create(gridWidth, gridHeight, x, y, width, height),
            Views = new OniNativeColony.CellView[width * height], Masses = new float[cells], Temperatures = new float[cells],
            Radiation = radiation ? new float[cells] : Array.Empty<float>(), Elements = new ushort[cells], Diseases = new byte[cells],
            Germs = new int[cells], Shown = new Element?[cells]
        };
        for (var i = 0; i < world.Views.Length; ++i)
        {
            // Ownership in stretches, as rooms are.
            var owned = (i / 3 + i / width) % 4 != 0 && random.Next(9) != 0;
            var disease = (uint)(random.Next(3) == 0 ? random.Next(1, 6) : 0);
            world.Views[i] = new OniNativeColony.CellView
            {
                ElementIndex = (uint)random.Next(12, 40), Flags = (owned ? OniNativeColony.ViewOwned : 0) | (uint)random.Next(4),
                MassKg = random.Next(5) == 0 ? 0 : (float)random.NextDouble() * 10, TemperatureK = 200 + (float)random.NextDouble() * 200,
                PressurePa = 101325, Disease = disease, DiseaseCount = random.Next(-5, 1000)
            };
        }
        for (var c = 0; c < cells; ++c)
        {
            world.Masses[c] = (float)random.NextDouble(); world.Temperatures[c] = 300; world.Diseases[c] = (byte)random.Next(256); world.Germs[c] = random.Next();
            if (radiation) world.Radiation[c] = random.Next(3) == 0 ? 0 : (float)random.NextDouble() * 5;
        }
        Perturb(random, world, catalogue);
        return world;
    }

    // A new native copy and a new Grid.Element over the same views.
    private static void Perturb(Random random, World world, List<Element> catalogue)
    {
        var owned = new HashSet<int>();
        for (var i = 0; i < world.Views.Length; ++i) if ((world.Views[i].Flags & OniNativeColony.ViewOwned) != 0) owned.Add(world.Map[i]);
        for (var c = 0; c < world.Elements.Length; ++c)
        {
            // Some owned cells have just become solid in the copy.
            world.Elements[c] = (ushort)(owned.Contains(c) ? (random.Next(10) == 0 ? random.Next(12) : random.Next(12, 40)) : random.Next(40));
            var shown = random.Next(4) == 0 ? random.Next(40) : world.Elements[c];
            // Grid.Element may hold no element outside owned fluid cells.
            world.Shown[c] = !owned.Contains(c) && random.Next(15) == 0 ? null : catalogue[shown];
            if (owned.Contains(c) && world.Elements[c] < 12 && random.Next(4) == 0) world.Shown[c] = null;
        }
        for (var i = 0; i < world.Views.Length; ++i)
            if ((world.Views[i].Flags & OniNativeColony.ViewOwned) != 0 && world.Elements[world.Map[i]] >= 12 && world.Shown[world.Map[i]] == null)
                world.Shown[world.Map[i]] = catalogue[0];
    }

    private static Result Overlay(World world, List<Element> catalogue, bool unchanged)
    {
        var changes = new Sim.SubstanceChangeInfo[2]; var changeCount = 0;
        var cells = new uint[2]; var rads = new float[2]; var irradiated = 0;
        int owned;
        fixed (float* masses = world.Masses) fixed (float* temperatures = world.Temperatures) fixed (float* radiation = world.Radiation)
        fixed (ushort* elements = world.Elements) fixed (byte* diseases = world.Diseases) fixed (int* germs = world.Germs)
        {
            var planes = new ColonyOverlay.Planes { Masses = masses, Temperatures = temperatures, Radiation = world.Radiation.Length != 0 ? radiation : null,
                Elements = elements, Diseases = diseases, Germs = germs };
            owned = ColonyOverlay.Apply(world.Views, world.Map, in planes, world.Shown!, ColonyOverlay.SolidElements(catalogue)!, unchanged,
                ref changes, ref changeCount, ref cells, ref rads, ref irradiated);
        }
        return new Result
        {
            Owned = owned, Planes = world, Changes = MemoryMarshal.AsBytes(changes.AsSpan(0, changeCount)).ToArray(),
            Irradiated = MemoryMarshal.AsBytes(cells.AsSpan(0, irradiated)).ToArray().Concat(MemoryMarshal.AsBytes(rads.AsSpan(0, irradiated)).ToArray()).ToArray()
        };
    }

    // The loop ColonyOwner.Publish ran before ColonyOverlay (mapped case).
    private static Result Reference(World world, List<Element> catalogue)
    {
        var views = world.Views; var cellMap = world.Map; var gridElements = world.Shown;
        Span<float> masses = world.Masses, temperatures = world.Temperatures, radiation = world.Radiation;
        Span<ushort> elements = world.Elements; Span<byte> diseases = world.Diseases; Span<int> germs = world.Germs;
        var substanceChanges = new Sim.SubstanceChangeInfo[2]; var changeCount = 0;
        var irradiatedCells = new uint[2]; var irradiatedRads = new float[2]; var irradiatedCount = 0;
        var ownedNow = 0;
        for (var i = 0; i < views.Length; ++i)
        {
            ref readonly var view = ref views[i];
            var owned=(view.Flags&OniNativeColony.ViewOwned)!=0;
            if(owned)++ownedNow;
            int global=cellMap[i];
            var published=elements[global];
            if (!owned || catalogue[published].IsSolid)
            {
                var shown=gridElements[global];
                if (shown != null && shown.idx != published)
                {
                    if(changeCount==substanceChanges.Length)Array.Resize(ref substanceChanges,changeCount*2);
                    substanceChanges[changeCount++]=new Sim.SubstanceChangeInfo { cellIdx = global, newElemIdx = published };
                }
                continue;
            }
            var mass=view.MassKg;
            if (radiation.Length != 0 && radiation[global] > 0 && mass > 0)
            {
                if(irradiatedCount==irradiatedCells.Length){Array.Resize(ref irradiatedCells,irradiatedCount*2);Array.Resize(ref irradiatedRads,irradiatedCount*2);}
                irradiatedCells[irradiatedCount]=(uint)global;irradiatedRads[irradiatedCount++]=radiation[global];
            }
            var element = (ushort)view.ElementIndex;
            masses[global] = mass; temperatures[global] = mass > 0 ? view.TemperatureK : 0;
            diseases[global] = OniNativeColony.DiseaseIdx(view.Disease); germs[global] = view.Disease != 0 ? view.DiseaseCount : 0;
            if (published != element || gridElements[global]!.idx != element)
            {
                if(changeCount==substanceChanges.Length)Array.Resize(ref substanceChanges,changeCount*2);
                substanceChanges[changeCount++]=new Sim.SubstanceChangeInfo { cellIdx = global, newElemIdx = element };
            }
            elements[global] = element;
        }
        return new Result
        {
            Owned = ownedNow, Planes = world, Changes = MemoryMarshal.AsBytes(substanceChanges.AsSpan(0, changeCount)).ToArray(),
            Irradiated = MemoryMarshal.AsBytes(irradiatedCells.AsSpan(0, irradiatedCount)).ToArray().Concat(MemoryMarshal.AsBytes(irradiatedRads.AsSpan(0, irradiatedCount)).ToArray()).ToArray()
        };
    }

    private static void Same(Result expected, Result actual, Action<bool> check)
    {
        check(expected.Owned == actual.Owned && expected.Owned > 0);
        check(expected.Changes.Length > 0 && expected.Changes.AsSpan().SequenceEqual(actual.Changes));
        check(expected.Irradiated.AsSpan().SequenceEqual(actual.Irradiated));
        check(MemoryMarshal.AsBytes(expected.Planes.Masses.AsSpan()).SequenceEqual(MemoryMarshal.AsBytes(actual.Planes.Masses.AsSpan())));
        check(MemoryMarshal.AsBytes(expected.Planes.Temperatures.AsSpan()).SequenceEqual(MemoryMarshal.AsBytes(actual.Planes.Temperatures.AsSpan())));
        check(expected.Planes.Elements.AsSpan().SequenceEqual(actual.Planes.Elements));
        check(expected.Planes.Diseases.AsSpan().SequenceEqual(actual.Planes.Diseases));
        check(expected.Planes.Germs.AsSpan().SequenceEqual(actual.Planes.Germs));
    }
}
