using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    // Engine-free geometry of one configured sealed room: its dimensions, the
    // row-major solid mask (0 fluid, 1 solid) and the complete initial
    // inventory, with every admission rule of the native ABI checked first.
    // Solids inside the rectangle stay native data; nothing here may project
    // fluid into them.
    internal sealed class ConfiguredDomainLayout
    {
        private readonly byte[] mask;
        internal int Width { get; }
        internal int Height { get; }
        internal int Count => mask.Length;
        internal int FluidCount { get; }
        internal int SolidCount => Count - FluidCount;
        // Status scalars sample this cell: the first fluid cell in row-major order.
        internal int FirstFluid { get; }
        internal IReadOnlyList<int> FluidIndices { get; }
        internal IReadOnlyList<int> SolidIndices { get; }

        internal ConfiguredDomainLayout(int width, int height, byte[] solidMask)
        {
            if (width < 2 || height < 2 || width > ConfiguredMaximumSide || height > ConfiguredMaximumSide ||
                (long)width * height > ConfiguredMaximumCells)
                throw new InvalidDataException("A configured room needs 2.." + ConfiguredMaximumSide + " cells per side and at most " +
                    ConfiguredMaximumCells + " cells.");
            if (solidMask == null || solidMask.Length != width * height || solidMask.Any(value => value > 1))
                throw new InvalidDataException("A configured solid mask needs one 0 (fluid) or 1 (solid) byte per row-major cell.");
            mask = (byte[])solidMask.Clone(); Width = width; Height = height;
            FluidIndices = Enumerable.Range(0, mask.Length).Where(index => mask[index] == 0).ToArray();
            SolidIndices = Enumerable.Range(0, mask.Length).Where(index => mask[index] != 0).ToArray();
            FluidCount = FluidIndices.Count;
            if (FluidCount == 0) throw new InvalidDataException("A configured room needs at least one fluid cell.");
            FirstFluid = FluidIndices[0];
        }

        // The in-game configured room, 7x5 with row 0 as the floor: a three-cell
        // pillar standing on the floor (x 3, y 0..2) and a one-cell ledge
        // (x 5, y 3). Fluid flows over the pillar and under the ledge.
        internal static ConfiguredDomainLayout PillaredRoom() =>
            FromSolids(7, 5, (dx, dy) => (dx == 3 && dy <= 2) || (dx == 5 && dy == 3));

        internal static ConfiguredDomainLayout FromSolids(int width, int height, Func<int, int, bool> solid)
        {
            if (width < 1 || height < 1 || (long)width * height > ConfiguredMaximumCells)
                throw new InvalidDataException("A configured room needs a bounded positive size.");
            var values = new byte[width * height];
            for (var dy = 0; dy < height; ++dy) for (var dx = 0; dx < width; ++dx) values[dx + dy * width] = solid(dx, dy) ? (byte)1 : (byte)0;
            return new ConfiguredDomainLayout(width, height, values);
        }

        // Physics of the in-game configured scenes: gravity 9.81 m/s2 and
        // conduction, oxygen gas or filled water (no mixture or viscosity).
        internal static Physics ScenePhysics(bool liquid) =>
            Physics.Create(PhysicsGravity | PhysicsConduction | (liquid ? PhysicsLiquid : 0U), 9.81);
        internal static Physics VacuumPhysics => Physics.Create(PhysicsVacuum | PhysicsMixture | PhysicsGravity, 9.81);
        internal static bool AdmitsSpecies(uint flags, uint species) => species < 5 &&
            ((flags & PhysicsMixture) != 0 || ((flags & PhysicsLiquid) != 0) == (species == 3));

        // A saved configuration has a restore adapter only when it is exactly a
        // scene this adapter builds: the pillared room with the gas or water
        // physics, compared bitwise. Anything else is rejected, never adapted.
        internal static bool IsSavedScene(NativeConfiguredDomainImage saved)
        {
            var room = PillaredRoom(); var liquid = (saved.Physics.Flags & PhysicsLiquid) != 0;
            return saved.Geometry.Width == room.Width && saved.Geometry.Height == room.Height && saved.SameMask(room.Mask) &&
                (NativeConfiguredDomainImage.SamePhysics(saved.Physics, ScenePhysics(liquid)) ||
                 NativeConfiguredDomainImage.SamePhysics(saved.Physics, VacuumPhysics));
        }

        // Native compatibility projection of the in-game configured scenes for a
        // fluid cell in column dx (never written into a solid). Saved native
        // worlds are admitted only with exactly these binary32 values.
        internal static float ProjectionMass(bool liquid, int column) => liquid ? 1000 + 20 * column : .6f + .3f * column;
        internal static float ProjectionTemperature(int column) => 280 + 5 * column;

        // A defensive copy for native calls and reports.
        internal byte[] Mask => (byte[])mask.Clone();
        internal bool IsSolid(int index) => mask[index] != 0;
        internal bool IsSolid(int dx, int dy) => dx >= 0 && dy >= 0 && dx < Width && dy < Height && mask[dx + dy * Width] != 0;
        internal bool IsFluid(int dx, int dy) => dx >= 0 && dy >= 0 && dx < Width && dy < Height && mask[dx + dy * Width] == 0;
        internal bool SameMask(byte[]? other) => other != null && other.AsSpan().SequenceEqual(mask);

        // Owned fluid cells next to each interior solid. A border solid touches
        // one owned cell; an interior solid may touch four. Each face is
        // impermeable and adiabatic in this slice.
        internal int FluidFaces(int index)
        {
            var dx = index % Width; var dy = index / Width;
            return (IsFluid(dx - 1, dy) ? 1 : 0) + (IsFluid(dx + 1, dy) ? 1 : 0) + (IsFluid(dx, dy - 1) ? 1 : 0) + (IsFluid(dx, dy + 1) ? 1 : 0);
        }

        // The complete row-major inventory: every solid is the all-zero record
        // (with its size and version), every fluid cell its supplied record.
        internal InitialCell[] Inventory(Func<int, InitialCell> fluid, bool allowVacuum = false)
        {
            var result = new InitialCell[Count];
            for (var index = 0; index < result.Length; ++index)
            {
                if (IsSolid(index)) { result[index] = new InitialCell { Size = 56, Version = 1 }; continue; }
                var value = fluid(index);
                var empty = Enumerable.Range(0, 5).All(species => value.Mass(species) == 0);
                if (value.Size != 56 || value.Version != 1 || !(value.Temperature >= 0) || double.IsInfinity(value.Temperature) ||
                    Enumerable.Range(0, 5).Any(species => !(value.Mass(species) >= 0) || double.IsInfinity(value.Mass(species))) ||
                    (empty ? !allowVacuum : !(value.Temperature > 0)))
                    throw new InvalidDataException("A configured fluid cell needs a finite inventory and a positive occupied temperature; vacuum requires its explicit opt-in.");
                result[index] = value;
            }
            return result;
        }

        // A configured copy publishes every solid as an all-zero cell; a fluid
        // cell always has positive mass and EOS temperature.
        internal static bool IsZero(Cell cell) => cell.Nitrogen == 0 && cell.Oxygen == 0 && cell.Helium == 0 && cell.Water == 0 &&
            cell.CarbonDioxide == 0 && cell.InternalEnergy == 0 && cell.Pressure == 0 && cell.Temperature == 0 && cell.LiquidFraction == 0 &&
            cell.ResolvedKineticEnergy == 0 && cell.MomentumX == 0 && cell.MomentumY == 0;
        internal bool CopyMatchesMask(Cell[] cells, bool allowVacuum = false) => cells.Length == Count && Enumerable.Range(0, Count).All(index =>
            IsSolid(index) ? IsZero(cells[index]) : allowVacuum && IsZero(cells[index]) || cells[index].TotalMass > 0 && cells[index].Temperature > 0);
    }
}
