using System.Buffers.Binary;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using MixedAtmospheres.Core.Interop;

// Original interface harness for the user's local licensed Windows/Linux ONI build.
// It runs in a separate OS process and never shares Unity's native singleton.
// Game data and native binaries are supplied externally, never distributed here.
internal static unsafe class Program
{
    private const int Width = 64, Height = 64, Cells = Width * Height;
    private const int OxygenCell = 20 + 20 * Width, WaterCell = 30 + 20 * Width;
    private const int ColdCell = 15 + 15 * Width, HotCell = 16 + 15 * Width;
    private const int TransitionCell = 40 + 20 * Width;
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    private static int Main(string[] args)
    {
        if (args.Length != 4 || args[2] is not ("baseline" or "sanitize" or "rebuild-shadow" or "load-shadow" or "transition" or "filtered-transition"))
        {
            Console.Error.WriteLine("Usage: dotnet NativeShadowProbe.dll <SimDLL.dll|libSimDLL.so> <game-contract-local> <baseline|sanitize|rebuild-shadow|load-shadow|transition|filtered-transition> <report.json>");
            return 2;
        }
        var report = new Dictionary<string, object?>
        {
            ["kind"] = "isolated-native-interface-experiment", ["oniBuild"] = "744825",
            ["authoritativeColonySimulationReady"] = false, ["mode"] = args[2],
            ["platform"] = RuntimeInformation.OSDescription,
            ["processId"] = Environment.ProcessId, ["startedUtc"] = DateTime.UtcNow
        };
        try
        {
            if ((!OperatingSystem.IsWindows() && !OperatingSystem.IsLinux()) || IntPtr.Size != 8 ||
                RuntimeInformation.ProcessArchitecture != Architecture.X64)
                throw new PlatformNotSupportedException("The inspected interface requires Windows or Linux x64.");
            var nativePath = Path.GetFullPath(args[0]);
            var contract = Contract.Read(args[1]);
            var nativeHash = Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(nativePath)));
            var expectedNativeHash = OperatingSystem.IsWindows()
                ? "031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727"
                : "85111cd0b1fc14c79c785581d6c055280b9d48ba705776f3d14eb0a7b239c12d";
            Require(nativeHash == expectedNativeHash, "Native library differs from the inspected 744825 platform build.");
            report["simDllSha256"] = nativeHash;
            report["elementsSha256"] = Convert.ToHexStringLower(SHA256.HashData(contract.Elements));
            report["diseasesSha256"] = Convert.ToHexStringLower(SHA256.HashData(contract.Diseases));
            report["elementCount"] = contract.Count;
            using (var sim = new NativeSim(nativePath))
            {
                report["nativeDiagnostics"] = sim.Diagnostics;
                Stage("initialize");
                sim.Initialize();
                var table = (byte[])contract.Elements.Clone();
                if (args[2] == "filtered-transition") report["filteredDefinitions"] = contract.FilterFluidProduction(table);
                sim.Send(1108437482, table);
                sim.Send(825301935, contract.Diseases);
                Stage("initialize-cells");
                sim.Send(2062421945, CreateWorld(contract, args[2]));
                Stage("start");
                var initial = Snapshot.Read(sim.Send(-931446686, []), contract);
                report["initial"] = initial;
                Require(initial.Elements[ColdCell] == contract.Granite && initial.Elements[HotCell] == contract.Granite,
                    "Native solid initialization differs from input.");
                if (args[2] is "baseline" or "sanitize" or "rebuild-shadow" or "load-shadow")
                {
                    Require(initial.Elements[OxygenCell] == contract.Oxygen && initial.Elements[WaterCell] == contract.Water,
                        "Native fluid initialization differs from input.");
                    Require(initial.FluidCells == 2, "Unexpected initial native fluids.");
                }
                if (args[2] == "baseline")
                {
                    Stage("export-initial-native-save");
                    var saved = sim.Save();
                    var savePath = Path.ChangeExtension(Path.GetFullPath(args[3]), ".native.bin");
                    Directory.CreateDirectory(Path.GetDirectoryName(savePath)!);
                    File.WriteAllBytes(savePath, saved);
                    File.WriteAllText(Path.ChangeExtension(savePath, ".cells.json"), JsonSerializer.Serialize(initial.CellData, Json));
                    report["initialNativeSaveBytes"] = saved.Length;
                    report["initialNativeSaveSha256"] = Convert.ToHexStringLower(SHA256.HashData(saved));
                }
                if (args[2] == "sanitize")
                {
                    Stage("replace-fluids-with-vacuum");
                    sim.Send(-1252920804, ModifyCell(OxygenCell, contract.Vacuum));
                    sim.Send(-1252920804, ModifyCell(WaterCell, contract.Vacuum));
                    // The core ownership question: publish after edits, with NO
                    // NewGameFrame between initialization and this observation.
                    Stage("prepare-without-frame");
                    var sanitized = sim.PrepareWithoutFrame(contract);
                    report["afterSanitationWithoutFrame"] = sanitized;
                    report["sanitationCommittedBeforeFrame"] = sanitized.FluidCells == 0;
                    Require(sanitized.FramesProcessed == 0, "Prepare advanced a frame unexpectedly.");
                    Require(sanitized.FluidCells == 0, "Sanitation did not commit before a native frame.");
                }
                if (args[2] is "rebuild-shadow" or "load-shadow")
                {
                    Stage("capture-fluids-and-rebuild-before-any-frame");
                    report["capturedFluidMassKg"] = initial.FluidMass;
                    NativeFluidMigration? migration = null;
                    var comparison = initial;
                    if (args[2] == "load-shadow")
                    {
                        var phases = Enumerable.Range(0, contract.Count).ToDictionary(
                            index => BinaryPrimitives.ReadInt32LittleEndian(contract.Elements.AsSpan(4 + index * 164)),
                            index => (NativeMatterPhase)contract.Phases[index]);
                        var vacuumHash = BinaryPrimitives.ReadInt32LittleEndian(contract.Elements.AsSpan(4 + contract.Vacuum * 164));
                        var savedNative = sim.Save();
                        var framed = new byte[4 + savedNative.Length];
                        BinaryPrimitives.WriteInt32LittleEndian(framed, savedNative.Length);
                        savedNative.CopyTo(framed, 4);
                        // Explicit bindings here describe captured harness
                        // inventory, not production thermodynamic support.
                        var bindings = phases.Where(pair => pair.Value is NativeMatterPhase.Gas or NativeMatterPhase.Liquid)
                            .ToDictionary(pair => pair.Key, pair => new NativeFluidBinding("harness-capture:" + pair.Key, pair.Value));
                        var staged = NativeColonyImport.StageNewColony(Width, Height,
                            new[] { new NativeWorldPlacement(0, Width, Height, 0, 0) },
                            new[] { new NativeWorldSource(0, framed) }, phases, bindings, vacuumHash);
                        migration = staged.Worlds.Single().Migration;
                        report["allWorldsStagedBeforeNativeLoad"] = true;
                        report["capturedFluidCells"] = migration.Fluids;
                        Require(migration.Width == Width && migration.Height == Height && migration.OffsetX == 0 && migration.OffsetY == 0,
                            "Native save geometry differs from public grid.");
                        Require(migration.Fluids.Sum(cell => (double)cell.MassKg) == initial.FluidMass, "Captured native-blob fluid mass differs.");
                        foreach (var cell in migration.Fluids)
                        {
                            var expected = initial.CellData[cell.X + cell.Y * Width];
                            Require(cell.MassKg == expected.Mass && cell.TemperatureKelvin == expected.Temperature &&
                                phases[cell.ElementHash] == (NativeMatterPhase)contract.Phases[expected.Element], "Captured fluid inventory differs.");
                        }
                        // Native saves deliberately omit some flags. Compare
                        // transformed load with an actual unmodified load, so
                        // both follow the game's normal reconstruction rules.
                        comparison = sim.RestartAndLoad(contract, contract.Elements, savedNative);
                        Require(comparison.FluidMass == initial.FluidMass && comparison.TotalMass == initial.TotalMass,
                            "Unmodified native save/load changed the mass ledger.");
                        report["unmodifiedLoadControl"] = comparison;
                        report["normalLoadCellMetadataDifferences"] = initial.CellData.Zip(comparison.CellData).Count(pair => pair.First != pair.Second);
                    }
                    // Only a fresh cell-only world is eligible. Real saves can
                    // contain native services not represented by these arrays.
                    // Destroy the first owner before initializing its successor.
                    report["filteredDefinitions"] = contract.FilterFluidProduction(table);
                    Snapshot rebuilt;
                    if (migration == null)
                    {
                        sim.Restart();
                        sim.Send(1108437482, table);
                        sim.Send(825301935, contract.Diseases);
                        sim.Send(2062421945, RebuildWorld(contract, initial));
                        rebuilt = Snapshot.Read(sim.Send(-931446686, []), contract);
                    }
                    else rebuilt = sim.RestartAndLoad(contract, table, migration.NativeData.ToArray());
                    report["rebuiltBeforeAnyFrame"] = rebuilt;
                    Require(rebuilt.FramesProcessed == 0 && rebuilt.FluidCells == 0, "Fresh native shadow was not fluid-free.");
                    Require(rebuilt.TotalMass + initial.FluidMass == initial.TotalMass, "Rebuild mass ledger differs.");
                    for (var cell = 0; cell < Cells; ++cell)
                        if (!contract.IsFluid(comparison.Elements[cell]))
                            Require(comparison.CellData[cell] == rebuilt.CellData[cell], "Nonfluid cell " + cell + " changed: "
                                + comparison.CellData[cell] + " -> " + rebuilt.CellData[cell]);
                    report["nonfluidCellsPreserved"] = true;
                    report["nonfluidComparison"] = migration == null ? "initial cells" : "unmodified native load";
                }
                Stage("advance-frames");
                var trace = new List<Snapshot>();
                report["frames"] = trace;
                for (var i = 0; i < 12; ++i)
                {
                    sim.Send(-775326397, Frame(0.2f));
                    var snapshot = sim.PrepareAfterFrame(contract);
                    trace.Add(snapshot);
                    Require(snapshot.FramesProcessed == 1, "Expected one processed native frame; observed " + snapshot.FramesProcessed + ".");
                    if (args[2] is "sanitize" or "rebuild-shadow" or "load-shadow" or "filtered-transition")
                        Require(snapshot.FluidCells == 0, "Native fluid appeared in the vacuum shadow.");
                }
                report["solidHeatTransferObserved"] = trace[^1].ColdTemperature > initial.ColdTemperature
                    && trace[^1].HotTemperature < initial.HotTemperature;
                Require((bool)report["solidHeatTransferObserved"]!, "Native solid heat transfer was not observed.");
                if (args[2] == "transition") Require(trace.Any(item => item.FluidCells > 0), "Unfiltered melting control did not produce fluid.");
                if (args[2] == "filtered-transition") Require(trace.All(item => item.TransitionElement == contract.Ice), "Filtered ice did not remain solid.");
                Require(sim.Diagnostics.Count == 0, "Native simulator reported diagnostic errors.");
                Stage("shutdown");
            }
            report["passed"] = true;
        }
        catch (Exception error)
        {
            report["passed"] = false;
            report["error"] = error.ToString();
            Console.Error.WriteLine(error);
        }
        report["completedUtc"] = DateTime.UtcNow;
        var destination = Path.GetFullPath(args[3]);
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        File.WriteAllText(destination, JsonSerializer.Serialize(report, Json));
        Console.WriteLine(destination);
        return report["passed"] is true ? 0 : 1;
    }

    private static void Stage(string value) { Console.WriteLine(value); Console.Out.Flush(); }
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidDataException(message); }
    private static byte[] WritePayload(System.Action<BinaryWriter> write)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        write(writer);
        return stream.ToArray();
    }
    private static byte[] CreateWorld(Contract contract, string mode) => WritePayload(writer =>
    {
        writer.Write(Width); writer.Write(Height); writer.Write(19425u);
        writer.Write(false); writer.Write(true); // radiation disabled, headless
        for (var cell = 0; cell < Cells; ++cell)
        {
            var x = cell % Width; var y = cell / Width;
            var border = x < 2 || y < 2 || x >= Width - 2 || y >= Height - 2;
            var element = border ? contract.Unobtanium : contract.Vacuum;
            float mass = border ? 20000 : 0, temperature = border ? 293.15f : 0;
            if (cell == ColdCell || cell == HotCell)
            { element = contract.Granite; mass = 1000; temperature = cell == ColdCell ? 300 : 600; }
            if (mode is "baseline" or "sanitize" or "rebuild-shadow" or "load-shadow")
            {
                if (cell == OxygenCell) { element = contract.Oxygen; mass = 2; temperature = 293.15f; }
                if (cell == WaterCell) { element = contract.Water; mass = 100; temperature = 293.15f; }
            }
            else if (cell == TransitionCell) { element = contract.Ice; mass = 1000; temperature = 400; }
            writer.Write((ushort)element);
            writer.Write((byte)0); writer.Write((byte)255); // properties, full thermal conductance
            writer.Write(new byte[4]); // strength and padding
            writer.Write(temperature); writer.Write(mass);
        }
        for (var cell = 0; cell < Cells; ++cell)
        {
            writer.Write((byte)255); writer.Write(new byte[3]);
            writer.Write(0); writer.Write(0f); // 12-byte DiseaseCell
        }
        for (var cell = 0; cell < Cells; ++cell)
        {
            writer.Write((ushort)contract.Vacuum); writer.Write((ushort)0);
            writer.Write(0f); writer.Write(0f); // 12-byte SimBackwall
        }
    });
    private static byte[] RebuildWorld(Contract contract, Snapshot original) => WritePayload(writer =>
    {
        writer.Write(Width); writer.Write(Height); writer.Write(19425u);
        writer.Write(false); writer.Write(true);
        foreach (var cell in original.CellData)
        {
            var fluid = contract.IsFluid(cell.Element);
            writer.Write(fluid ? (ushort)contract.Vacuum : cell.Element);
            writer.Write(cell.Properties); writer.Write(cell.Insulation); writer.Write(cell.Strength);
            writer.Write(new byte[3]);
            writer.Write(fluid ? 0f : cell.Temperature); writer.Write(fluid ? 0f : cell.Mass);
        }
        foreach (var cell in original.CellData)
        {
            var fluid = contract.IsFluid(cell.Element);
            writer.Write(fluid ? (byte)255 : cell.Disease); writer.Write(new byte[3]);
            writer.Write(fluid ? 0 : cell.DiseaseCount); writer.Write(0f);
        }
        foreach (var cell in original.CellData)
        {
            writer.Write(cell.Backwall); writer.Write((ushort)0);
            writer.Write(cell.BackwallMass); writer.Write(cell.BackwallTemperature);
        }
    });
    private static byte[] ModifyCell(int cell, int vacuum) => WritePayload(writer =>
    {
        writer.Write(cell); writer.Write(-1); writer.Write(0f); writer.Write(0f); writer.Write(0);
        writer.Write((ushort)vacuum); writer.Write((byte)1); writer.Write((byte)255);
        writer.Write((byte)1); writer.Write(new byte[3]);
    });
    private static byte[] Frame(float dt) => WritePayload(writer =>
    {
        writer.Write(dt); writer.Write(0); writer.Write(0); writer.Write(Width); writer.Write(Height);
        writer.Write(0f); writer.Write(0f);
    });

    private sealed class Contract
    {
        public required byte[] Elements { get; init; }
        public required byte[] Diseases { get; init; }
        public required int[] Phases { get; init; }
        public int Count => Phases.Length;
        public int Vacuum, Oxygen, Water, Granite, Unobtanium, Ice;
        public static Contract Read(string directory)
        {
            using var json = JsonDocument.Parse(File.ReadAllBytes(Path.Combine(directory, "contract.json")));
            var root = json.RootElement;
            Require(root.GetProperty("OniBuild").GetString() == "744825" && root.GetProperty("ElementRecordBytes").GetInt32() == 164,
                "Uninspected game contract.");
            var elements = File.ReadAllBytes(Path.Combine(directory, "elements.bin"));
            var diseases = File.ReadAllBytes(Path.Combine(directory, "diseases.bin"));
            var count = BinaryPrimitives.ReadInt32LittleEndian(elements);
            Require(count > 0 && count <= 65535 && count == root.GetProperty("ElementCount").GetInt32()
                && elements.Length >= 4 + count * 164 && diseases.Length >= 8, "Invalid captured tables.");
            var phases = new int[count];
            for (var i = 0; i < count; ++i)
            {
                Require(BinaryPrimitives.ReadUInt16LittleEndian(elements.AsSpan(4 + i * 164 + 8)) == i, "Element indices are not stable.");
                phases[i] = elements[4 + i * 164 + 10] & 3;
            }
            var result = new Contract
            {
                Elements = elements, Diseases = diseases, Phases = phases,
                Vacuum = root.GetProperty("VacuumIndex").GetInt32(), Oxygen = root.GetProperty("OxygenIndex").GetInt32(),
                Water = root.GetProperty("WaterIndex").GetInt32(), Granite = root.GetProperty("GraniteIndex").GetInt32(),
                Unobtanium = root.GetProperty("UnobtaniumIndex").GetInt32(),
                Ice = root.GetProperty("Elements").EnumerateArray().Single(item => item.GetProperty("Name").GetString() == "Ice").GetProperty("Index").GetInt32()
            };
            foreach (var i in new[] { result.Vacuum, result.Oxygen, result.Water, result.Granite, result.Unobtanium, result.Ice })
                Require(i >= 0 && i < count, "Required material absent.");
            return result;
        }
        public bool IsFluid(int index) => Phases[index] is 1 or 2;
        public int FilterFluidProduction(byte[] table)
        {
            var phases = Enumerable.Range(0, Count).ToDictionary(
                index => BinaryPrimitives.ReadInt32LittleEndian(Elements.AsSpan(4 + index * 164)),
                index => (NativeMatterPhase)Phases[index]);
            var result = NativeElementTable.SuppressFluidProduction(table, phases);
            result.Data.CopyTo(table);
            return result.Suppressed.Count;
        }
    }

    private readonly record struct CellState(ushort Element, float Temperature, float Mass, byte Properties,
        byte Insulation, byte Strength, byte Disease, int DiseaseCount, ushort Backwall, float BackwallMass, float BackwallTemperature);
    private sealed class Snapshot
    {
        public int FramesProcessed { get; init; }
        public int FluidCells { get; init; }
        public double FluidMass { get; init; }
        public double TotalMass { get; init; }
        public float ColdTemperature { get; init; }
        public float HotTemperature { get; init; }
        public int TransitionElement { get; init; }
        public int SubstanceChangeEvents { get; init; }
        public int FallingLiquidEvents { get; init; }
        public int OreEvents { get; init; }
        [System.Text.Json.Serialization.JsonIgnore] public ushort[] Elements { get; init; } = [];
        [System.Text.Json.Serialization.JsonIgnore] public CellState[] CellData { get; init; } = [];
        public static Snapshot Read(IntPtr pointer, Contract contract)
        {
            Require(pointer != IntPtr.Zero, "No native snapshot returned.");
            // Both inspected managed Sim declarations use Pack4 on x64.
            var indices = (ushort*)Marshal.ReadIntPtr(pointer, 4);
            var temperatures = (float*)Marshal.ReadIntPtr(pointer, 12);
            var masses = (float*)Marshal.ReadIntPtr(pointer, 20);
            Require(indices != null && temperatures != null && masses != null, "Missing native cell buffers.");
            var elements = new ushort[Cells];
            var states = new CellState[Cells];
            var properties = (byte*)Marshal.ReadIntPtr(pointer, 28);
            var insulation = (byte*)Marshal.ReadIntPtr(pointer, 36);
            var strength = (byte*)Marshal.ReadIntPtr(pointer, 44);
            var disease = (byte*)Marshal.ReadIntPtr(pointer, 60);
            var diseaseCount = (int*)Marshal.ReadIntPtr(pointer, 68);
            var backwall = (ushort*)Marshal.ReadIntPtr(pointer, 76);
            var backwallMass = (float*)Marshal.ReadIntPtr(pointer, 84);
            var backwallTemperature = (float*)Marshal.ReadIntPtr(pointer, 92);
            Require(properties != null && insulation != null && strength != null && disease != null && diseaseCount != null
                && backwall != null && backwallMass != null && backwallTemperature != null, "Missing cell metadata arrays.");
            var fluids = 0; double fluidMass = 0, totalMass = 0;
            for (var cell = 0; cell < Cells; ++cell)
            {
                var index = indices[cell]; var mass = masses[cell]; var temperature = temperatures[cell];
                Require(index < contract.Count && float.IsFinite(mass) && mass >= 0 && float.IsFinite(temperature), "Invalid native cell data.");
                elements[cell] = index; totalMass += mass;
                states[cell] = new CellState(index, temperature, mass, properties[cell], insulation[cell], strength[cell],
                    disease[cell], diseaseCount[cell], backwall[cell], backwallMass[cell], backwallTemperature[cell]);
                if (contract.IsFluid(index)) { ++fluids; fluidMass += mass; }
            }
            return new Snapshot
            {
                Elements = elements, CellData = states, FramesProcessed = Marshal.ReadInt32(pointer, 0), FluidCells = fluids,
                FluidMass = fluidMass, TotalMass = totalMass, ColdTemperature = temperatures[ColdCell], HotTemperature = temperatures[HotCell],
                TransitionElement = indices[TransitionCell], SubstanceChangeEvents = Marshal.ReadInt32(pointer, 136),
                FallingLiquidEvents = Marshal.ReadInt32(pointer, 160), OreEvents = Marshal.ReadInt32(pointer, 184)
            };
        }
    }

    private sealed class NativeSim : IDisposable
    {
        [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate int Callback(int id, IntPtr pointer);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void InitializeFunction(Callback callback);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void ShutdownFunction();
        [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate IntPtr MessageFunction(int id, int size, byte* data);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate byte* BeginSaveFunction(int* size, int x, int y);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void EndSaveFunction();
        private readonly IntPtr library;
        private readonly InitializeFunction initialize;
        private readonly ShutdownFunction shutdown;
        private readonly MessageFunction message;
        private readonly BeginSaveFunction beginSave;
        private readonly EndSaveFunction endSave;
        private readonly Callback callback;
        private readonly byte[] visibility = Enumerable.Repeat((byte)255, Cells).ToArray();
        private bool started;
        public List<string> Diagnostics { get; } = [];
        public NativeSim(string file)
        {
            library = NativeLibrary.Load(file);
            initialize = Marshal.GetDelegateForFunctionPointer<InitializeFunction>(NativeLibrary.GetExport(library, "SIM_Initialize"));
            shutdown = Marshal.GetDelegateForFunctionPointer<ShutdownFunction>(NativeLibrary.GetExport(library, "SIM_Shutdown"));
            message = Marshal.GetDelegateForFunctionPointer<MessageFunction>(NativeLibrary.GetExport(library, "SIM_HandleMessage"));
            beginSave = Marshal.GetDelegateForFunctionPointer<BeginSaveFunction>(NativeLibrary.GetExport(library, "SIM_BeginSave"));
            endSave = Marshal.GetDelegateForFunctionPointer<EndSaveFunction>(NativeLibrary.GetExport(library, "SIM_EndSave"));
            callback = OnDiagnostic;
        }
        private int OnDiagnostic(int id, IntPtr pointer)
        {
            try
            {
                var detail = "Native callback " + id;
                if (pointer != IntPtr.Zero && id == 1)
                    detail += ": " + Marshal.PtrToStringAnsi(Marshal.ReadIntPtr(pointer, 8));
                else if (pointer != IntPtr.Zero && id == 0)
                    detail += ": " + Marshal.PtrToStringAnsi(Marshal.ReadIntPtr(pointer, 0));
                lock (Diagnostics) Diagnostics.Add(detail);
                Console.Error.WriteLine(detail);
            }
            catch { /* Never unwind a managed exception through the native callback. */ }
            return id is 0 or 1 ? 0 : -1;
        }
        public void Initialize() { initialize(callback); started = true; }
        public void Restart()
        {
            Require(started, "No initialized native owner to replace.");
            shutdown(); started = false;
            Initialize();
        }
        public Snapshot RestartAndLoad(Contract contract, byte[] table, byte[] saved)
        {
            Restart();
            Send(1108437482, table);
            Send(825301935, contract.Diseases);
            Send(1092408308, WritePayload(writer =>
            { writer.Write(Width); writer.Write(Height); writer.Write(false); writer.Write(true); }));
            Send(-895846551, WritePayload(writer =>
            { writer.Write(1); writer.Write(0); writer.Write(0); writer.Write(Width); writer.Write(Height); }));
            Require(Send(-672538170, saved) != IntPtr.Zero, "Native loader rejected blob.");
            return Snapshot.Read(Send(-931446686, []), contract);
        }
        public IntPtr Send(int id, byte[] bytes) { fixed (byte* data = bytes) return message(id, bytes.Length, data); }
        public byte[] Save()
        {
            var size = 0;
            var pointer = beginSave(&size, 0, 0);
            try
            {
                Require(pointer != null && size > 0 && size <= 256 * 1024 * 1024, "Invalid native save buffer.");
                return new ReadOnlySpan<byte>(pointer, size).ToArray();
            }
            finally { endSave(); }
        }
        public Snapshot Prepare(Contract contract) => Snapshot.Read(Send(1078620451, visibility), contract);
        public Snapshot PrepareWithoutFrame(Contract contract)
        {
            var clock = Stopwatch.StartNew();
            Snapshot result;
            do
            {
                result = Prepare(contract);
                if (result.FluidCells == 0 || result.FramesProcessed != 0) return result;
                Thread.Sleep(1);
            } while (clock.ElapsedMilliseconds < 250);
            return result;
        }
        public Snapshot PrepareAfterFrame(Contract contract)
        {
            var clock = Stopwatch.StartNew();
            do
            {
                var result = Prepare(contract);
                if (result.FramesProcessed != 0) return result;
                Thread.Sleep(1);
            } while (clock.ElapsedMilliseconds < 3000);
            throw new TimeoutException("No completed frame published within three seconds.");
        }
        public void Dispose()
        {
            if (started) { shutdown(); started = false; }
            GC.KeepAlive(callback);
            NativeLibrary.Free(library);
        }
    }
}
