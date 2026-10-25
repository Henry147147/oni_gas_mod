using System.Buffers.Binary;
using System.Runtime.InteropServices;

internal static unsafe partial class Program
{
    // Deliberately separate from the sealed-domain fixture: no permeability
    // messages, definition filtering or projection correction after a frame.
    // These are the colony ABI records, not private SimDLL layouts.
    private static class ColonyHooks
    {
        private const string Library = "mixed_atmospheres_oni";
        [StructLayout(LayoutKind.Sequential)]
        internal struct Config
        {
            public uint Size, Version, Flags, Vacuum, GridWidth, GridHeight, X, Y, Width, Height;
            public double Gravity, MaximumTimestep;
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        internal struct Material
        {
            public uint Size, Version; public int Hash; public uint Index, Phase, Reserved;
            public double MolarMass, SpecificHeat, Conductivity, Density, LowTemperature, HighTemperature;
            public int LowTarget, HighTarget, LowByproduct, HighByproduct;
            public double LowFraction, HighFraction;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string Id;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Cell { public uint Index, Kind; public float Mass, Temperature; public uint Disease; public int Germs; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct View
        {
            public uint Index, Flags; public float Mass, Temperature, Pressure, LiquidFraction;
            public uint Disease; public int Germs;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Transfer
        {
            public uint Cell, Index, Kind, Radius; public int Callback; public uint Disease;
            public double Mass, Temperature, Germs;
        }
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "ma_oni_colony_queue_transfers")]
        internal static extern int Transfers([In] Transfer[] transfers, uint count);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "ma_oni_colony_composition")]
        internal static extern int Composition(uint cell, [Out] uint[] indices, [Out] double[] masses, uint capacity, out uint count);
        // Only the diagnostic fields needed here; retain the complete ABI size.
        [StructLayout(LayoutKind.Explicit, Size = 600)]
        internal struct Status
        {
            [FieldOffset(0)] public uint Size;
            [FieldOffset(4)] public uint Version;
            [FieldOffset(8)] public uint State;
            [FieldOffset(24)] public ulong Frames;
            [FieldOffset(112)] public double Time;
            [FieldOffset(192)] public double NativeHeat;
            [FieldOffset(200)] public double NativeMassIn;
            [FieldOffset(208)] public double NativeMassOut;
            [FieldOffset(216)] public ulong NativeChanges;
            internal static Status Empty => new() { Size = 600, Version = 1 };
        }
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "ma_oni_colony_start")]
        internal static extern int Start(ref Config config, [In] Material[] materials, uint materialCount,
            [In] Cell[] cells, uint cellCount, ref Status status);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "ma_oni_colony_copy")]
        internal static extern int Copy(ref Status status, [Out] View[] views, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "ma_oni_colony_wait")]
        internal static extern int Wait();
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "ma_oni_colony_stop")]
        internal static extern int Stop(ref Status status);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "ma_oni_last_error")]
        internal static extern IntPtr LastError();
        internal static void Check(int result) => Require(result == 0,
            "Colony hook control call failed: " + Marshal.PtrToStringAnsi(LastError()));
    }

    private static void VerifyColonyHooks(NativeSim sim, Contract contract, Dictionary<string, object?> report, string mode)
    {
        var liquid = mode == "colony-water-hooks";
        var boundary = mode == "colony-gas-boundary-hooks";
        var density = mode == "colony-gas-density-hooks";
        var lateral = mode == "colony-gas-lateral-hooks";
        var openInterface = boundary || density || lateral;
        Require(Marshal.SizeOf<ColonyHooks.Config>() == 56 && Marshal.SizeOf<ColonyHooks.Material>() == 168 &&
            Marshal.SizeOf<ColonyHooks.Cell>() == 24 && Marshal.SizeOf<ColonyHooks.View>() == 32,
            "Colony hook control ABI differs.");
        void Replace(int cell, int element, float mass, float temperature) => sim.Send(-1252920804, WritePayload(writer =>
        {
            writer.Write(cell); writer.Write(-1); writer.Write(temperature); writer.Write(mass); writer.Write(0);
            writer.Write((ushort)element); writer.Write((byte)1); writer.Write((byte)255); writer.Write((byte)1); writer.Write(new byte[3]);
        }));
        // Independent unowned fluid controls, outside the owned room.
        if (!openInterface)
        {
            Replace(OxygenCell, contract.Oxygen, 2, 293.15f);
            Replace(WaterCell, contract.Water, 100, 293.15f);
        }
        if (openInterface)
        {
            // Uniform temperature isolates the fluid writers from ordinary
            // native heat coupling across the intentionally open interface.
            for (var i = 0; i < 6; ++i)
                Replace(DomainCell(i), contract.Oxygen, DomainProjectionMass(i % DomainWidth, false), 300);
            if (density)
            {
                for (var x = 0; x < DomainWidth; ++x)
                    Replace(DomainX + x + (DomainY + DomainHeight) * Width, contract.CarbonDioxide, 4, 300);
            }
            else if (lateral)
            {
                for (var y = 0; y < DomainHeight; ++y)
                    Replace(DomainX - 1 + (DomainY + y) * Width, contract.CarbonDioxide, 4, 300);
            }
            else
            {
                // The diagonal source has no owned cardinal neighbour.
                Replace(DomainX - 1 + (DomainY - 1) * Width, contract.Oxygen, 4, 300);
                Replace(DomainX + (DomainY - 1) * Width, contract.Vacuum, 0, 0);
                Replace(DomainX - 1 + DomainY * Width, contract.Vacuum, 0, 0);
            }
        }
        sim.Send(-775326397, Frame(0));
        sim.PrepareAfterFrame(contract);
        var saved = sim.Save();
        var start = sim.RestartAndLoad(contract, contract.Elements, saved);
        const int frames = 60;
        var original = new Snapshot[frames];
        for (var frame = 0; frame < frames; ++frame)
        {
            sim.Send(-775326397, Frame(.2f)); original[frame] = sim.PrepareAfterFrame(contract);
        }
        Require(Enumerable.Range(0, 6).Any(i => original[0].CellData[DomainCell(i)].Mass != start.CellData[DomainCell(i)].Mass),
            "Unowned pressure-gradient control did not flow.");
        Require(openInterface || (original[^1].CellData[OxygenCell].Mass != start.CellData[OxygenCell].Mass &&
            original[^1].CellData[WaterCell].Mass != start.CellData[WaterCell].Mass),
            "Remote unowned fluid controls did not flow.");
        Require(original[^1].ColdTemperature > start.ColdTemperature && original[^1].HotTemperature < start.HotTemperature,
            "Remote native solid heat control did not conduct.");
        start = sim.RestartAndLoad(contract, contract.Elements, saved);
        if (Environment.GetEnvironmentVariable("MA_COLONY_HOOK_REPLAY_CONTROL") == "1")
        {
            for (var frame = 0; frame < frames; ++frame)
            {
                sim.Send(-775326397, Frame(.2f)); var replay = sim.PrepareAfterFrame(contract);
                for (var cell = 0; cell < Cells; ++cell)
                    Require(replay.CellData[cell].Equals(original[frame].CellData[cell]),
                        $"Owner-free replay differs at frame {frame}, cell {cell}: {original[frame].CellData[cell]} vs {replay.CellData[cell]}");
            }
            report["ownerFreeReplayExact"] = true;
            return;
        }
        var index = liquid ? contract.Water : contract.Oxygen;
        var material = new ColonyHooks.Material
        {
            Size = 168, Version = 1, Hash = BinaryPrimitives.ReadInt32LittleEndian(contract.Elements.AsSpan(4 + index * 164)),
            Index = (uint)index, Phase = liquid ? 2U : 1U, Id = liquid ? "Water" : "Oxygen",
            MolarMass = liquid ? .01801528 : .0319988, SpecificHeat = liquid ? 4180 : 1005,
            Conductivity = 0, Density = liquid ? 1000 : 0, LowTemperature = 1, HighTemperature = 10000
        };
        var materials = new List<ColonyHooks.Material> { material };
        if (density || lateral)
        {
            var carbon = material;
            carbon.Hash = BinaryPrimitives.ReadInt32LittleEndian(contract.Elements.AsSpan(4 + contract.CarbonDioxide * 164));
            carbon.Index = (uint)contract.CarbonDioxide; carbon.Id = "CarbonDioxide";
            carbon.MolarMass = .0440095; carbon.SpecificHeat = 850;
            materials.Add(carbon);
        }
        var inventory = Enumerable.Range(0, 6).Select(i => new ColonyHooks.Cell
        {
            Index = (uint)index, Kind = 1, Mass = start.CellData[DomainCell(i)].Mass,
            Temperature = start.CellData[DomainCell(i)].Temperature
        }).ToArray();
        var config = new ColonyHooks.Config
        {
            Size = 56, Version = 1, Flags = 12, Vacuum = (uint)contract.Vacuum,
            GridWidth = Width, GridHeight = Height, X = (uint)DomainX, Y = DomainY,
            Width = DomainWidth, Height = DomainHeight, MaximumTimestep = .1
        };
        var status = ColonyHooks.Status.Empty;
        ColonyHooks.Check(ColonyHooks.Start(ref config, materials.ToArray(), (uint)materials.Count, inventory, 6, ref status));
        var views = new ColonyHooks.View[6];
        var comparedUnowned = 0;
        var changedByOwner = false;
        var unownedDifferences = 0;
        string? firstUnownedDifference = null;
        try
        {
            for (var frame = 0; frame < frames; ++frame)
            {
                ColonyHooks.Check(ColonyHooks.Copy(ref status, views, 6));
                sim.Send(-775326397, Frame(.2f));
                var snapshot = sim.PrepareAfterFrame(contract);
                // SimDLL sees the previous completed owner's projection. Any
                // native writer must be detected before the next owner step.
                for (var i = 0; i < 6; ++i)
                {
                    var cell = snapshot.CellData[DomainCell(i)];
                    Require((cell.Properties & 3) == 0, "A permeability fence masked native writer coverage.");
                    Require(cell.Disease == 255 && cell.DiseaseCount == 0 && views[i].Disease == 0 && views[i].Germs == 0,
                        "Native physics changed a germ-free owned projection.");
                    Require(cell.Element == views[i].Index && BitConverter.SingleToInt32Bits(cell.Mass) == BitConverter.SingleToInt32Bits(views[i].Mass) &&
                        BitConverter.SingleToInt32Bits(cell.Temperature) == BitConverter.SingleToInt32Bits(views[i].Temperature),
                        "Native physics changed an owned projection at frame " + frame + ", cell " + i +
                        $": expected {views[i].Index}/{views[i].Mass:R}/{views[i].Temperature:R}, actual {cell.Element}/{cell.Mass:R}/{cell.Temperature:R}.");
                    changedByOwner |= views[i].Mass != inventory[i].Mass;
                }
                for (var cell = 0; cell < Cells; ++cell)
                {
                    if (IsSealedArenaCell(cell % Width, cell / Width)) continue;
                    // The open interface intentionally changes unowned fluid
                    // next to the owner; retain the isolated solid comparison.
                    if (openInterface && cell != ColdCell && cell != HotCell) continue;
                    if (!snapshot.CellData[cell].Equals(original[frame].CellData[cell]))
                    {
                        ++unownedDifferences;
                        firstUnownedDifference ??= $"frame {frame}, cell {cell}: {original[frame].CellData[cell]} vs {snapshot.CellData[cell]}";
                    }
                    ++comparedUnowned;
                }
                if (frame == frames - 1)
                {
                    Require(snapshot.ColdTemperature > start.ColdTemperature && snapshot.HotTemperature < start.HotTemperature,
                        "Native solid heat stopped while the colony owned its fluid cells.");
                    Require(openInterface || (snapshot.CellData[OxygenCell].Mass != start.CellData[OxygenCell].Mass &&
                        snapshot.CellData[WaterCell].Mass != start.CellData[WaterCell].Mass),
                        "Remote native fluid stopped while the colony owned its room.");
                }
                ColonyHooks.Check(ColonyHooks.Wait());
                ColonyHooks.Check(ColonyHooks.Copy(ref status, views, 6));
                Require(status.State == 1 && status.Frames == (ulong)(frame + 1), "Colony did not complete exactly one frame.");
                Require(status.NativeMassIn == 0 && status.NativeMassOut == 0 && status.NativeHeat == 0 && status.NativeChanges == 0,
                    "Native physics was booked into the isolated owner as a boundary transfer.");
            }
            Require(changedByOwner, "Colony owner did not evolve the pressure gradient.");
            report["colonyHookOwnership"] = new
            {
                frames = status.Frames, timeSeconds = status.Time, permeabilityMask = 0, openDiagonalInterface = boundary, openDensityInterface = density, openLateralInterface = lateral,
                exactOwnedProjections = frames * 6, comparedUnownedCells = comparedUnowned,
                unownedDifferences, firstUnownedDifference,
                status.NativeHeat, status.NativeMassIn, status.NativeMassOut, status.NativeChanges,
                nativeFlowControl = true, nativeSolidHeatControl = true, ownerEvolved = changedByOwner,
                gameProfileFlags = config.Flags, materialParameters = "diagnostic catalogue, no phase transitions"
            };
            // Native postprocess shares its RNG/direction state across cells.
            // Skipping an owned fluid changes later unowned draws on Linux as
            // well. Retain exact differences as diagnostics, not an ownership
            // assertion (the owned endpoint and booking assertions stay exact).
        }
        finally { ColonyHooks.Check(ColonyHooks.Stop(ref status)); }
        Require(sim.Diagnostics.Count == 0, "Native diagnostics reported an ownership control failure.");
    }
}
