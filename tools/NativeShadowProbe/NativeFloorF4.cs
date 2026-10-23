using System.Diagnostics;

internal static unsafe partial class Program
{
    // Read the unchanged float inputs whose actual Unity admission was proved.
    // This is an isolated native measurement, not a Unity alignment claim.
    private static void VerifyNativeFloorF4(NativeSim sim, Contract contract, Dictionary<string, object?> report)
    {
        var input = Environment.GetEnvironmentVariable("MA_NATIVE_FLOOR_REQUEST")
            ?? throw new InvalidOperationException("MA_NATIVE_FLOOR_REQUEST must name the proved F4 request.");
        var fields = new CellState[28];
        using (var reader = new BinaryReader(File.OpenRead(input)))
        {
            Require(System.Text.Encoding.ASCII.GetString(reader.ReadBytes(8)) == "MAFLRQ01", "Wrong floor request magic.");
            var sourceWidth = reader.ReadUInt32(); var sourceHeight = reader.ReadUInt32();
            var x0 = reader.ReadUInt32(); var y0 = reader.ReadUInt32();
            Require(sourceWidth > 0 && sourceHeight > 0 && x0 + 7 <= sourceWidth && y0 + 4 <= sourceHeight &&
                reader.ReadUInt32() == 7 && reader.ReadUInt32() == 4 && reader.ReadUInt32() == 28, "Not the fixed F4 geometry.");
            for (var i = 0; i < 28; ++i)
            {
                Require(reader.ReadUInt32() == x0 + (uint)(i % 7) + (y0 + (uint)(i / 7)) * sourceWidth, "Wrong F4 cell order.");
                var element = reader.ReadUInt32(); var mass = reader.ReadSingle(); var temperature = reader.ReadSingle();
                var x = i % 7; var y = i / 7; var border = x == 0 || x == 6 || y == 0 || y == 3;
                Require(element == (uint)(border ? contract.Unobtanium : x == 3 ? contract.Granite : contract.Oxygen) &&
                    float.IsFinite(mass) && mass > 0 && (!border || mass == 20000) && temperature == (float)(border ? 293.15 : x == 3 ? 318.15 : x < 3 ? 353.15 : 283.15),
                    "F4 materials or temperatures differ from the frozen setup.");
                fields[i] = new CellState((ushort)element, temperature, mass, 0, 255, 0, 255, 0, (ushort)contract.Vacuum, 0, 0);
            }
            Require(reader.BaseStream.Position == reader.BaseStream.Length, "Trailing floor input bytes.");
        }
        const int left = 20, bottom = 20;
        int Index(int i) => left + i % 7 + (bottom + i / 7) * Width;
        sim.Initialize(); sim.Send(1108437482, contract.Elements); sim.Send(825301935, contract.Diseases);
        var world = CreateWorld(contract, "floor-f4");
        // Replace every initial cell, including the generic interface fixtures.
        for (var i = 0; i < Cells; ++i)
        {
            var offset = 14 + 16 * i;
            Array.Clear(world, offset, 16);
            BitConverter.GetBytes((ushort)contract.Vacuum).CopyTo(world, offset);
            world[offset + 3] = 255;
        }
        for (var i = 0; i < fields.Length; ++i)
        {
            var cell = fields[i]; var offset = 14 + 16 * Index(i);
            BitConverter.GetBytes(cell.Element).CopyTo(world, offset);
            BitConverter.GetBytes(cell.Temperature).CopyTo(world, offset + 8);
            BitConverter.GetBytes(cell.Mass).CopyTo(world, offset + 12);
        }
        sim.Send(2062421945, world);
        var initial = Snapshot.Read(sim.Send(-931446686, []), contract);
        Require(initial.FluidCells == 8 && Enumerable.Range(0, 28).All(i => initial.CellData[Index(i)].Element == fields[i].Element &&
            initial.CellData[Index(i)].Mass == fields[i].Mass && initial.CellData[Index(i)].Temperature == fields[i].Temperature),
            "Actual native F4 initial inventory/temperature differs from the proved inputs.");
        double Cv(ushort element) => 1000.0 * BitConverter.ToSingle(contract.Elements, 4 + element * 164 + 12);
        Require(Cv((ushort)contract.Unobtanium) == 0, "The frozen insulating boundary has nonzero heat capacity.");
        double Heat(Snapshot s) => Enumerable.Range(0, 28).Sum(i => { var c = s.CellData[Index(i)]; return c.Mass * Cv(c.Element) * c.Temperature; });
        double RoomTemperature(Snapshot s, bool hot)
        {
            var cells = Enumerable.Range(0, 28).Where(i => i / 7 is 1 or 2 && (hot ? i % 7 is 1 or 2 : i % 7 is 4 or 5)).Select(i => s.CellData[Index(i)]).ToArray();
            Require(cells.All(c => c.Element == contract.Oxygen), "Native F4 room changed material.");
            return cells.Sum(c => (double)c.Mass * c.Temperature) / cells.Sum(c => (double)c.Mass);
        }
        var initialHeat = Heat(initial); var capacity = fields.Sum(c => c.Mass * Cv(c.Element));
        var totalMass = fields.Sum(c => (double)c.Mass); var maxHeatError = 0.0; var maxMassError = 0.0;
        var hotMass = Enumerable.Range(0, 28).Where(i => i / 7 is 1 or 2 && i % 7 is 1 or 2).Sum(i => (double)fields[i].Mass);
        var coldMass = Enumerable.Range(0, 28).Where(i => i / 7 is 1 or 2 && i % 7 is 4 or 5).Sum(i => (double)fields[i].Mass);
        int crossing = -1, consecutive = 0; var tail = new Queue<(double hot, double cold, double heat)>();
        var clock = Stopwatch.StartNew();
        for (var frame = 1; frame <= 100000; ++frame)
        {
            sim.Send(-775326397, Frame(.2f)); var sample = sim.PrepareAfterFrame(contract);
            Require(sample.FramesProcessed == 1, "Native F4 did not publish exactly one frame.");
            var hot = RoomTemperature(sample, true); var cold = RoomTemperature(sample, false); var heat = Heat(sample);
            maxHeatError = Math.Max(maxHeatError, Math.Abs(heat - initialHeat));
            maxMassError = Math.Max(maxMassError, Math.Abs(Enumerable.Range(0, 28).Sum(i => (double)sample.CellData[Index(i)].Mass) - totalMass));
            if (Math.Abs(hot - cold) <= 35) { if (++consecutive >= 6 && crossing < 0) crossing = frame - 5; } else consecutive = 0;
            tail.Enqueue((hot, cold, heat)); if (tail.Count > 5) tail.Dequeue();
            if (frame % 10000 == 0) Stage("native-f4 sample " + frame);
        }
        Require(sim.Diagnostics.Count == 0, "Native F4 emitted diagnostics.");
        report["floorF4"] = new { ActualLicensedNativeSimulation = true, OwnerLoaded = false, UnityAlignmentEstablished = false,
            FixedArenaWidth = 7, FixedArenaHeight = 4, EmbeddingWidth = Width, EmbeddingHeight = Height,
            InputRequestSha256 = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(input))),
            InitialInputsMatchProvedUnityRequest = true, InitialMassKg = totalMass, InitialHeatJoules = initialHeat,
            InitialOxygenKg = fields.Where(c => c.Element == contract.Oxygen).Sum(c => (double)c.Mass),
            HeatCapacityJoulesPerKelvin = capacity, EquilibriumTemperatureK = initialHeat / capacity,
            Samples = 100001, NominalDurationSeconds = 20000, NativeTimestepSeconds = (double).2f,
            HalvingBracketSeconds = crossing < 0 ? null : new[] { (crossing - 1) * .2, crossing * .2 },
            Censored = crossing < 0, FinalHotK = tail.Average(c => c.hot), FinalColdK = tail.Average(c => c.cold),
            FinalFluidMassWeightedK = tail.Average(c => (c.hot * hotMass + c.cold * coldMass) / (hotMass + coldMass)),
            FinalFluidEquilibriumErrorK = Math.Abs(tail.Average(c => (c.hot * hotMass + c.cold * coldMass) / (hotMass + coldMass)) - initialHeat / capacity),
            FinalHeatJoules = tail.Average(c => c.heat), MaximumAbsoluteHeatErrorJoules = maxHeatError,
            MaximumAbsoluteMassErrorKg = maxMassError, WallSeconds = clock.Elapsed.TotalSeconds,
            Caveat = "Native-only numerical measurement; partition/coordinate and Unity frame alignment remain unproved. No floor acceptance." };
    }
}
