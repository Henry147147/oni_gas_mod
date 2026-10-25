using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // The fixed comparison arenas F1-F5 of docs/base-game-floor.md. One arena
    // is carved beside the printing pod of a new colony and sampled at every
    // 200 ms simulation tick from native data or retained owner frames, under ONI's
    // own simulation (mode colony-vanilla: the colony owner never starts) or
    // under the colony owner. Coordinates are local and include the Unobtanium
    // border. Numbers only: tools/floor_metrics.py computes the metrics.
    // Harness only: cells change through the sandbox tool's messages and the
    // F2 door is built instantly.
    internal static unsafe class FloorArenas
    {
        internal static readonly string[] Ids = FloorArenaIds.All;
        private const int PrepareGameData = 1078620451;
        private const double Atmosphere = 101325, GasConstant = 8.314462618, Room = 293.15;
        // Gas, door, levelling and layering runs last 120 s after release; F4 20000 s.
        private static int Duration => Id == "f4" ? 100000 : 600;

        internal static string Id { get; private set; } = "";
        private static long ticks;
        private static long cleared = -1, uncovered = -1, built = -1, filled = -1, locked = -1, commanded = -1, opened = -1, release = -1;
        private static double releasedAt = -1, simulatedAt = -1; // game clock and simulated time at release, s
        private static int width, height, x0, y0;
        private static Door? door;
        private static StreamWriter? samples;
        private static readonly SortedDictionary<int, Dictionary<string, object>> seen = new SortedDictionary<int, Dictionary<string, object>>();
        private static readonly Dictionary<string, double> placed = new Dictionary<string, double>();
        private static string output = "";
        private static string prefixDump = "";
        private static bool prefixHeaderWritten;
        private static long f4Cleared = -1, f4Reset = -1;
        private static string admissionPath = "";
        [StructLayout(LayoutKind.Sequential)]
        private struct SampleCell { internal uint PublicCell, FirstConstituent, Constituents, Flags; internal double TemperatureK; }
        [StructLayout(LayoutKind.Sequential)]
        private struct SamplePart { internal uint NativeIndex, Reserved; internal double MassKg; }
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeSampleCell { internal uint PublicCell, NativeIndex; internal double MassKg, TemperatureK; }
        [StructLayout(LayoutKind.Sequential)]
        private struct EnergySample
        {
            internal uint Size, Version;
            internal ulong Session, Frame;
            internal double TimeSeconds, Internal, Kinetic, Potential, Thermal, Formation, Capacity, Mass, ExternalHeat, TraceEnergy;
        }
        [DllImport("mixed_atmospheres_oni", EntryPoint = "ma_oni_colony_sample_history", CallingConvention = CallingConvention.Cdecl)]
        private static extern int RetainSamples(uint frames);
        [DllImport("mixed_atmospheres_oni", EntryPoint = "ma_oni_colony_sample_frame", CallingConvention = CallingConvention.Cdecl)]
        private static extern int SampleFrame(ulong frame, [In] uint[] cells, uint count, out OniNativeColony.Status source,
            [Out] SampleCell[] rows, [Out] SamplePart[] parts, uint capacity, out uint partCount, out double bookedMassKg, out double bookedHeatJ);
        [DllImport("mixed_atmospheres_oni", EntryPoint = "ma_oni_colony_sample_energy", CallingConvention = CallingConvention.Cdecl)]
        private static extern int SampleEnergy(ulong session, ulong frame, ref EnergySample energy);
        [DllImport("mixed_atmospheres_oni", EntryPoint = "ma_oni_colony_sample_native_frame", CallingConvention = CallingConvention.Cdecl)]
        private static extern int SampleNativeFrame(ulong session, ulong frame, [In] uint[] cells, uint count,
            out OniNativeColony.Status source, [Out] NativeSampleCell[] rows);
        private static uint[] ownerRequested = Array.Empty<uint>();
        private static SampleCell[] ownerRows = Array.Empty<SampleCell>();
        private static SamplePart[] ownerParts = Array.Empty<SamplePart>();
        private static NativeSampleCell[] ownerNativeRows = Array.Empty<NativeSampleCell>();
        private static readonly Dictionary<int, int> ownerCells = new Dictionary<int, int>();
        private static readonly List<ulong[]> ownerSampleFrames = new List<ulong[]>();
        private static readonly List<Dictionary<string, object>> ownerSampleAudits = new List<Dictionary<string, object>>();
        private static bool historyStarted;
        private static ulong nextOwnerFrame;
        private static long ownerTickOffset, lastOwnerSampleTick = -1;
        private static double lastOwnerSampleTime = -1;
        private static OniNativeColony.Status ownerSource;
        private static bool ownerReleaseStamped;

        internal static void Configure(string id, string directory)
        {
            if (Array.IndexOf(Ids, id) < 0) throw new ArgumentException("Unknown floor arena " + id);
            Id = id; output = directory; ticks = 0; cleared = uncovered = built = filled = locked = commanded = opened = release = -1; releasedAt = -1;
            prefixDump = id == "f4" ? Environment.GetEnvironmentVariable("MA_COLONY_FLOOR_PREFIX_DUMP") ?? "" : "";
            prefixHeaderWritten = false; f4Cleared = f4Reset = -1;
            admissionPath = id == "f4" ? Environment.GetEnvironmentVariable("MA_COLONY_FLOOR_ADMISSION_DUMP") ?? Path.Combine(output,"floor-f4-admission.bin") : "";
            if(id == "f4")
            {
                Environment.SetEnvironmentVariable("MA_COLONY_FLOOR_ADMISSION_DUMP",admissionPath);
                if(string.IsNullOrEmpty(Environment.GetEnvironmentVariable("MA_COLONY_RELEASE_DUMP")))
                    Environment.SetEnvironmentVariable("MA_COLONY_RELEASE_DUMP",Path.Combine(output,"floor-f4-initial-world.bin"));
            }
            if (prefixDump.Length != 0 && File.Exists(prefixDump)) throw new IOException("Floor prefix capture requires a fresh private path.");
            door = null; simulated = 0; seen.Clear(); placed.Clear(); samples?.Dispose(); samples = null;
            ownerRequested = Array.Empty<uint>(); ownerCells.Clear(); ownerSampleFrames.Clear(); ownerSampleAudits.Clear();
            historyStarted = false; nextOwnerFrame = 0; ownerTickOffset = 0; lastOwnerSampleTick = -1; lastOwnerSampleTime = -1;
            ownerReleaseStamped = false;
            (width, height) = id switch { "f1" or "f2" => (19, 10), "f3" => (22, 14), "f4" => (7, 4), _ => (10, 14) };
        }

        // Simulated time: every native frame the game posts (NewGameFrame),
        // before it is sent.
        private static double simulated;
        internal static void OnFrame(float seconds) { if (Id.Length != 0 && seconds > 0) simulated += seconds; }
        // Every native message, after owner publication. Native-only runs use
            // the submitted tick; owner arenas drain retained frames.
        internal static void OnNativeMessage(int message, IntPtr data)
        {
            if (Id.Length == 0 || message != PrepareGameData || data == IntPtr.Zero) return;
            var tick = (long)Math.Floor(simulated / .2 + 1e-9);
            var newTick = tick > ticks;
            if (newTick) ticks = tick;
            var update = (Sim.GameDataUpdate*)data;
            // The native submitted clock can be ahead of the owner. Retain
            // and drain every completed fluid frame rather than relabelling
            // whichever world happens to be latest when this message arrives.
            if (ColonyOwner.Active)
            {
                if (!historyStarted)
                {
                    OniNativeColony.Check(RetainSamples(64));
                    CaptureOwnerSample();
                    ownerTickOffset = ticks - checked((long)ownerSource.CompletedFrames);
                    nextOwnerFrame = ownerSource.CompletedFrames + 1; historyStarted = true;
                }
                var latest = OniNativeColony.Status.Empty;
                OniNativeColony.Check(OniNativeColony.CopyStatus(ref latest));
                if (filled < 0 || samples == null) { nextOwnerFrame = latest.CompletedFrames + 1; return; }
                while (nextOwnerFrame <= latest.CompletedFrames)
                {
                    var sampleTick = checked((long)nextOwnerFrame + ownerTickOffset);
                    if (ownerReleaseStamped && sampleTick > release + Duration) break;
                    CaptureOwnerSample(nextOwnerFrame, sampleTick);
                    if (release >= 0 && !ownerReleaseStamped && Id != "f2" && ReleaseIsOpen())
                    {
                        // Opening precedes this completed interval. Its prior
                        // retained row is the release's t=0 sample.
                        release = sampleTick - 1; ownerReleaseStamped = true;
                    }
                    if (lastOwnerSampleTime >= 0 && Math.Abs(ownerSource.TimeSeconds - lastOwnerSampleTime - .2) > 1e-6)
                        throw new InvalidDataException("Floor owner interval is not the frozen 0.2 seconds.");
                    WriteSample(update, sampleTick); lastOwnerSampleTime = ownerSource.TimeSeconds;
                    lastOwnerSampleTick = sampleTick; ++nextOwnerFrame;
                }
                return;
            }
            if (!newTick) return;
            if (filled < 0 || samples == null || release >= 0 && ticks > release + Duration) return;
            CaptureOwnerSample(ulong.MaxValue, ticks);
            WriteSample(update, ticks);
        }
        private static void WriteSample(Sim.GameDataUpdate* update, long sampleTick)
        {
            var line = new System.Text.StringBuilder(32 * width * height);
            line.Append(sampleTick.ToString(CultureInfo.InvariantCulture));
            var fluid = false;
            // Per cell: its constituents (count, then element and kg each) and
            // its temperature. ONI's cell holds one element; an owned cell
            // holds the owner's mixture (the published element is only its
            // dominant one).
            for (var y = 1; y < height - 1; ++y)
                for (var x = 1; x < width - 1; ++x)
                {
                    var cell = Cell(x, y);
                    double temperature = update->temperature[cell];
                    if (ownerCells.TryGetValue(cell, out var ownedIndex) && (ownerRows[ownedIndex].Flags & 1) != 0)
                    {
                        var reading = ownerRows[ownedIndex];
                        line.Append(' ').Append(reading.Constituents);
                        for (var k = 0; k < reading.Constituents; ++k)
                        {
                            var part = ownerParts[reading.FirstConstituent + k];
                            Part((int)part.NativeIndex, part.MassKg);
                        }
                        // Match the public binary32 projection, while mass and
                        // temperature come from the same completed owner frame.
                        temperature = reading.TemperatureK;
                    }
                    else if (historyStarted && Id == "f4")
                    {
                        if (!ownerCells.TryGetValue(cell, out var nativeIndex))
                            throw new InvalidDataException("F4 solid lacks its paired native observation.");
                        var native = ownerNativeRows[nativeIndex];
                        if (native.MassKg > 0 && (ElementLoader.elements[(int)native.NativeIndex].IsGas || ElementLoader.elements[(int)native.NativeIndex].IsLiquid))
                            throw new InvalidDataException("F4 native fluid has not reached its owner frame.");
                        line.Append(" 1"); Part((int)native.NativeIndex, native.MassKg); temperature = native.TemperatureK;
                    }
                    else { line.Append(" 1"); Part(update->elementIdx[cell], update->mass[cell]); }
                    // Widen before formatting: float R's short text round-trips
                    // through float, but the offline reader uses doubles.
                    line.Append(' ').Append(temperature.ToString("R", CultureInfo.InvariantCulture));
                }
            void Part(int element, double kg)
            {
                if (kg > 0 && (ElementLoader.elements[element].IsGas || ElementLoader.elements[element].IsLiquid)) fluid = true;
                if (!seen.ContainsKey(element)) seen[element] = Describe(ElementLoader.elements[element]);
                line.Append(' ').Append(element).Append(' ').Append(kg.ToString("R", CultureInfo.InvariantCulture));
            }
            samples!.WriteLine(line.ToString());
            // The first fluid row is a completed interval. Its preceding
            // endpoint is the frozen initial state, proved separately; the
            // conducted endpoint must never be relabelled as that state.
            if (Id == "f4" && release < 0 && fluid) { release = sampleTick - 1; ownerReleaseStamped = historyStarted; }
            // F2: t = 0 is the first sample after the door is fully open.
            if (commanded >= 0 && opened < 0 && !ElementLoader.elements[update->elementIdx[Cell(9, 4)]].IsSolid &&
                !ElementLoader.elements[update->elementIdx[Cell(9, 5)]].IsSolid &&
                (!historyStarted || OwnerOpen(9,4) && OwnerOpen(9,5)))
            { opened = release = sampleTick; ownerReleaseStamped = historyStarted; }
            if (prefixDump.Length != 0 && historyStarted && release >= 0)
            {
                // Explicit bounded diagnostic only. Full-run duration and
                // completion stay unchanged; a prefix never claims the floor.
                samples!.Flush();
                if (!prefixHeaderWritten)
                {
                    var header = new Dictionary<string, object> {
                        ["kind"] = "header", ["arena"] = Id, ["simulation"] = "colony owner", ["measurement_complete"] = false,
                        ["width"] = width, ["height"] = height, ["tick_seconds"] = .2, ["temperature_storage"] = "binary32",
                        ["owner_tick_offset"] = ownerTickOffset, ["owner_sample_history"] = true,
                        ["fluid_sampling"] = "retained completed owner frames",
                        ["solid_sampling"] = "retained native heat-input ticket paired with the same owner frame",
                        ["booked_inventory_scope"] = "fluid+reservoir",
                        ["cells"] = "per cell: constituent count, (element index, kg) each, temperature K",
                        ["elements"] = seen.ToDictionary(e => e.Key.ToString(CultureInfo.InvariantCulture), e => (object)e.Value),
                        ["placed"] = placed };
                    File.WriteAllText(prefixDump, ReportJson.SerializeLine(header) + "\n"); prefixHeaderWritten = true;
                }
                var sample = new Dictionary<string, object> { ["kind"] = "sample", ["row"] = line.ToString(),
                    ["audit"] = ownerSampleAudits[ownerSampleAudits.Count - 1], ["release_tick"] = release, ["filled_tick"] = filled };
                File.AppendAllText(prefixDump, ReportJson.SerializeLine(sample) + "\n");
            }
        }
        private static bool OwnerOpen(int x,int y) => ownerCells.TryGetValue(Cell(x,y),out var index) && (ownerRows[index].Flags & 1) != 0;
        private static bool ReleaseIsOpen() => Id switch
        {
            "f1" => Enumerable.Range(1,8).All(y => OwnerOpen(9,y)),
            "f3" => Enumerable.Range(1,8).All(y => OwnerOpen(6,y)),
            "f5" => Enumerable.Range(1,8).All(x => OwnerOpen(x,6)),
            _ => false
        };

        private static void CaptureOwnerSample(ulong frame = ulong.MaxValue, long sampleTick = -1)
        {
            ownerCells.Clear();
            if (!ColonyOwner.Active) return;
            // Floor samples measure fixed simulation intervals. Complete the
            // already submitted owner work before reading the native tick's row.
            if (frame == ulong.MaxValue) OniNativeColony.Check(OniNativeColony.Wait());
            if (ownerRequested.Length == 0)
            {
                if (Marshal.SizeOf<SampleCell>() != 24 || Marshal.SizeOf<SamplePart>() != 16 || Marshal.SizeOf<NativeSampleCell>() != 24)
                    throw new InvalidDataException("Floor sample ABI layout differs.");
                // The fixed fixture owns precisely this arena. Bound capacity
                // by its catalogue; one call then copies one immutable frame.
                var c = ColonyOwner.Config;
                if (c.Width != width || c.Height != height || c.OffsetX != x0 || c.OffsetY != y0)
                    throw new InvalidDataException("Floor owner geometry differs from its fixed arena.");
                var requested = new List<uint>();
                for (var y = 1; y < height - 1; ++y) for (var x = 1; x < width - 1; ++x) requested.Add((uint)Cell(x, y));
                ownerRequested = requested.ToArray(); ownerRows = new SampleCell[ownerRequested.Length];
                ownerParts = new SamplePart[checked(ownerRequested.Length * ColonyOwner.Materials.Length)];
                ownerNativeRows = new NativeSampleCell[ownerRequested.Length];
            }
            OniNativeColony.Check(SampleFrame(frame, ownerRequested, (uint)ownerRequested.Length, out var source,
                ownerRows, ownerParts, (uint)ownerParts.Length, out _, out var bookedMass, out var bookedHeat));
            ownerSource = source;
            var refused = new List<uint>();
            for (var k = 0; k < ownerRows.Length; ++k)
            {
                ownerCells.Add((int)ownerRows[k].PublicCell, k);
                if ((ownerRows[k].Flags & 2) != 0) refused.Add(ownerRows[k].PublicCell);
            }
            if (sampleTick < 0) return;
            ownerSampleFrames.Add(new[] { (ulong)sampleTick, source.CompletedFrames });
            var audit = new Dictionary<string, object> {
                ["tick"] = sampleTick, ["session"] = source.Session, ["frame"] = source.CompletedFrames, ["time_s"] = source.TimeSeconds,
                ["native_ticket"] = source.NativeTicket,
                ["projection_refused_cells"] = refused, ["native_mass_in_kg"] = source.NativeMassInKg,
                ["native_mass_out_kg"] = source.NativeMassOutKg, ["trace_sink_kg"] = source.TraceSinkKg,
                ["injected_kg"] = source.InjectedKg, ["extracted_kg"] = source.ExtractedKg, ["reservoir_kg"] = source.ReservoirKg,
                ["expected_fluid_and_reservoir_kg"] = bookedMass,
                ["accepted_external_heat_j"] = bookedHeat,
                ["space_sink_kg"] = source.SpaceSinkKg, ["native_heat_j"] = source.NativeHeatJoules, ["mass_error"] = source.MaximumRelativeMassError,
                ["energy_error"] = source.MaximumRelativeEnergyError };
            if (Id == "f4")
            {
                OniNativeColony.Check(SampleNativeFrame(source.Session, source.CompletedFrames, ownerRequested,
                    (uint)ownerRequested.Length, out var native, ownerNativeRows));
                if (native.Session != source.Session || native.CompletedFrames != source.CompletedFrames ||
                    native.TimeSeconds != source.TimeSeconds || native.NativeTicket != source.NativeTicket)
                    throw new InvalidDataException("F4 native solids and owned fluids differ in source frame.");
                for (var k = 0; k < ownerNativeRows.Length; ++k)
                    if (ownerNativeRows[k].PublicCell != ownerRequested[k]) throw new InvalidDataException("F4 native row order differs.");
                audit["native_sample_ticket"] = native.NativeTicket;
            }
            {
                if (Marshal.SizeOf<EnergySample>() != 104) throw new InvalidDataException("Floor energy ABI layout differs.");
                var energy = new EnergySample { Size = 104, Version = 1 };
                OniNativeColony.Check(SampleEnergy(source.Session, source.CompletedFrames, ref energy));
                if (energy.Session != source.Session || energy.Frame != source.CompletedFrames ||
                    energy.TimeSeconds != source.TimeSeconds || energy.ExternalHeat != bookedHeat)
                    throw new InvalidDataException("Floor energy and composition came from different frames.");
                audit["energy"] = new Dictionary<string, object> {
                    ["session"] = energy.Session, ["frame"] = energy.Frame, ["time_s"] = energy.TimeSeconds,
                    ["internal_energy_j"] = energy.Internal, ["kinetic_energy_j"] = energy.Kinetic,
                    ["potential_energy_j"] = energy.Potential, ["thermal_energy_j"] = energy.Thermal,
                    ["formation_energy_j"] = energy.Formation, ["heat_capacity_j_per_k"] = energy.Capacity,
                    ["fluid_mass_kg"] = energy.Mass, ["accepted_external_heat_j"] = energy.ExternalHeat,
                    ["trace_sink_energy_j"] = energy.TraceEnergy };
            }
            ownerSampleAudits.Add(audit);
        }

        // Every Unity frame: build, fill and release at their ticks. True
        // when the arena's run is complete and its record written.
        internal static bool Update()
        {
            if (Id.Length == 0) return false;
            // Before the arena is built its whole rectangle opens to vacuum and
            // the world's objects go, then what the open cells give up goes
            // too: a hatch burrowed in the ground (ONI keeps it out of the
            // world until its cell opens) walked out when the divider opened.
            // ONI removes destroyed objects at the end of a frame, so each
            // step waits two ticks.
            if (cleared < 0)
            {
                if (ticks < 2) return false;
                Anchor(); placed["removed_objects"] = Clear(x0, y0, width, height, out var duplicants, "floor arena cleared");
                placed["duplicants_inside"] = duplicants;
                for (var y = 0; y < height; ++y) for (var x = 0; x < width; ++x) Replace(x, y, SimHashes.Vacuum, 0, 0);
                cleared = ticks; return false;
            }
            if (uncovered < 0)
            {
                if (ticks < cleared + 2) return false;
                placed["removed_uncovered"] = Clear(x0, y0, width, height, out _, "floor arena uncovered"); uncovered = ticks; return false;
            }
            if (built < 0)
            {
                if (ticks < uncovered + 2) return false;
                Build(); built = ticks; return false;
            }
            if (filled < 0)
            {
                if (ticks < built + 2) return false;
                if(Id == "f4")
                {
                    // Closing borders can displace background fluid back into
                    // the arena. Remove it before resetting the divider.
                    if(f4Cleared < 0)
                    {
                        Clear(x0,y0,width,height,out _,"F4 quiescent preparation");
                        RequestAdmissionCapture(".vacuum",vacuum:true,fluidsOnly:true);
                        for(var y=1;y<height-1;++y)for(var x=1;x<width-1;++x)
                            if(!Solid(x,y))Replace(x,y,SimHashes.Vacuum,0,0);
                        f4Cleared=ticks;return false;
                    }
                    if(f4Reset < 0)
                    {
                        if(ticks < f4Cleared+2 || !F4CaptureReady(".vacuum",8,"vacuum"))return false;
                        RequestAdmissionCapture(".prepared",vacuum:true);
                        for(var y=1;y<=2;++y)Replace(3,y,SimHashes.Granite,placed["granite_tile_kg"],318.15);
                        f4Reset=ticks;return false;
                    }
                    if(!F4CaptureReady(".prepared",width*height,"prepared"))return false;
                    RequireNoLooseActors(x0,y0,width,height,allowInfrastructure:false);
                    placed["source_primary_element_census"]=0;
                }
                Fill(); filled = ticks;
                samples = new StreamWriter(Path.Combine(output, "floor-" + Id + ".txt"));
                return false;
            }
            switch (Id)
            {
                case "f2":
                    if (door == null && ticks >= filled + 1)
                        door = Assets.GetBuildingDef(ManualPressureDoorConfig.ID).Build(Cell(9, 4), Orientation.Neutral, null,
                            Assets.GetBuildingDef(ManualPressureDoorConfig.ID).DefaultElements(), (float)Room, playsound: false).GetComponent<Door>();
                    // The door takes control-state changes once it has spawned.
                    if (door != null && locked < 0 && ticks >= filled + 3) { SetDoor(Door.ControlState.Locked); locked = ticks; }
                    // Hold closed for 10 s (50 ticks), then open.
                    if (locked >= 0 && commanded < 0 && ticks >= locked + 50) { SetDoor(Door.ControlState.Opened); commanded = ticks; }
                    break;
                case "f1": case "f3": case "f5":
                    if (release < 0 && ticks >= filled + 5)
                    {
                        // The t = 0 sample is this tick's: the wall goes at the next.
                        var wall = Id == "f1" ? Enumerable.Range(1, 8).Select(y => (9, y)) : Id == "f3" ? Enumerable.Range(1, 8).Select(y => (6, y))
                            : Enumerable.Range(1, 8).Select(x => (x, 6));
                        var gates = wall.ToArray();
                        RequestReleaseCapture(gates);
                        foreach (var (x, y) in gates) Replace(x, y, SimHashes.Vacuum, 0, 0);
                        release = ticks;
                    }
                    break;
            }
            if (release >= 0 && releasedAt < 0) { releasedAt = GameClock.Instance.GetTime(); simulatedAt = simulated; }
            if (release < 0 || ticks < release + Duration) return false;
            if (historyStarted && !ownerReleaseStamped) return false;
            if (historyStarted && lastOwnerSampleTick < release + Duration) return false;
            Finish(); return true;
        }

        private static void RequestReleaseCapture((int x, int y)[] gates)
        {
            var path = Environment.GetEnvironmentVariable("MA_COLONY_RELEASE_DUMP");
            if (string.IsNullOrEmpty(path) || Id != "f1" && Id != "f3" && Id != "f4" && Id != "f5") return;
            if (!ColonyOwner.Active) throw new InvalidOperationException("Release world capture requires the colony owner.");
            if (File.Exists(path) || File.Exists(path + ".request")) throw new IOException("Release capture requires a fresh private output path.");
            var c = ColonyOwner.Config;
            using (var file = new BinaryWriter(File.Create(path + ".request.tmp")))
            {
                file.Write(System.Text.Encoding.ASCII.GetBytes(Id=="f4"?"MAREL002":"MAREL001"));
                foreach (var dimension in new[] { c.GridWidth, c.GridHeight, c.OffsetX, c.OffsetY, c.Width, c.Height }) file.Write(dimension);
                file.Write((uint)gates.Length);
                foreach (var (x, y) in gates)
                {
                    file.Write((uint)Cell(x,y));
                    if(Id=="f4")
                    {
                        file.Write((uint)ElementLoader.FindElementByHash(SimHashes.Oxygen).idx);
                        file.Write((float)placed[x<3?"hot_cell_kg":"cold_cell_kg"]);file.Write((float)(x<3?353.15:283.15));
                    }
                }
            }
            // Publishing precedes every gate removal message. A native worker
            // cannot miss the marker while applying the already queued edits.
            File.Move(path + ".request.tmp", path + ".request");
        }

        // The element's identity and the properties the metrics need, read
        // from the running game (never committed).
        private static Dictionary<string, object> Describe(Element e) => new Dictionary<string, object>
        {
            ["id"] = e.id.ToString(), ["phase"] = e.IsVacuum ? "vacuum" : e.IsGas ? "gas" : e.IsLiquid ? "liquid" : "solid",
            ["molar_mass_kg_per_mol"] = e.molarMass / 1000.0, ["specific_heat_j_per_kg_k"] = e.specificHeatCapacity * 1000.0,
            ["conductivity_w_per_m_k"] = e.thermalConductivity,
        };
        // The cells a floor run under the owner owns: the arena with its border.
        internal static bool OwnerRectangle(out int x, out int y, out int w, out int h)
        {
            x = y = w = h = 0;
            if (Id.Length == 0) return false;
            Anchor(); x = x0; y = y0; w = width; h = height;
            return true;
        }
        private static void Anchor()
        {
            var pad = GameUtil.GetActiveTelepad();
            var cell = pad != null ? Grid.PosToCell(pad) : Grid.XYToCell(Grid.WidthInCells / 2, Grid.HeightInCells / 2);
            Grid.CellToXY(cell, out var px, out var py);
            x0 = px + 9; y0 = py;
        }
        private static int Cell(int x, int y) => Grid.XYToCell(x0 + x, y0 + y);
        private static void Replace(int x, int y, SimHashes element, double mass, double temperature) =>
            SimMessages.ReplaceElement(Cell(x, y), element, CellEventLogger.Instance.SandBoxTool, (float)mass, (float)temperature);

        // The removable walls (F4's divider is granite, not a wall).
        private static bool Wall(int x, int y) => y >= 1 && y <= 8 && (Id == "f1" || Id == "f2") && x == 9 || Id == "f3" && x == 6 && y <= 8 ||
            Id == "f5" && y == 6 && x <= 8;
        private static bool Solid(int x, int y) => Wall(x, y) || Id == "f4" && x == 3;
        // Grid.Objects and Pickupables omit some spawned or burrowed
        // creatures. Enumerate their actual PrimaryElements too, including
        // inactive objects which still have a native thermal handle.
        private static IEnumerable<PrimaryElement> FixturePrimaryElements(int left,int bottom,int w,int h)
        {
            foreach(var primary in UnityEngine.Object.FindObjectsByType<PrimaryElement>(UnityEngine.FindObjectsInactive.Include,UnityEngine.FindObjectsSortMode.None))
            {
                if(primary==null || !primary.isSpawned && (primary.sttOptimizationHook==null || !Sim.IsValidHandle(primary.sttOptimizationHook.SimHandle)))continue;
                var cell=Grid.PosToCell(primary);if(!Grid.IsValidCell(cell))continue;
                Grid.CellToXY(cell,out var x,out var y);
                if(x>=left && x<left+w && y>=bottom && y<bottom+h)yield return primary;
            }
        }
        internal static int ClearLooseActors(int left,int bottom,int w,int h,out int duplicants,string? label=null)
        {
            var actors=FixturePrimaryElements(left,bottom,w,h).Where(primary=>primary.GetComponent<Building>()==null && !ColonyOwnedStorage.IsProjection(primary))
                .Select(primary=>primary.gameObject).Distinct().ToArray();
            duplicants=actors.Count(actor=>actor.GetComponent<MinionIdentity>()!=null);
            if(duplicants!=0)throw new InvalidOperationException("A duplicant occupies the closed fixture before admission.");
            if(label!=null && actors.Length!=0)Debug.Log($"[Mixed Atmospheres] {label}: "+string.Join(", ",actors.Select(actor=>actor.name)));
            foreach(var actor in actors)Util.KDestroyGameObject(actor);
            return actors.Length;
        }
        internal static void RequireNoLooseActors(int left,int bottom,int w,int h,bool allowInfrastructure=true)
        {
            var primary=FixturePrimaryElements(left,bottom,w,h).FirstOrDefault(item=>!allowInfrastructure || item.GetComponent<Building>()==null && !ColonyOwnedStorage.IsProjection(item));
            if(primary==null)return;
            var prefab=primary.GetComponent<KPrefabID>();var thermal=primary.sttOptimizationHook;
            throw new InvalidOperationException($"Unaccounted fixture source before admission: {primary.gameObject.name}, prefab {prefab?.PrefabTag.Name}, cell {Grid.PosToCell(primary)}, mass {primary.Mass:R}kg, spawned {primary.isSpawned}, active {primary.gameObject.activeInHierarchy}, thermal {(thermal==null?-1:thermal.SimHandle)}.");
        }
        // Removes every building, plant, critter and item in a rectangle
        // (duplicants excepted and counted); returns how many went, naming
        // them in the log under `label` when one is given.
        internal static int Clear(int left, int bottom, int w, int h, out int duplicants, string? label = null)
        {
            var found = new HashSet<UnityEngine.GameObject>();
            for (var y = bottom; y < bottom + h; ++y)
                for (var x = left; x < left + w; ++x)
                {
                    var cell = Grid.XYToCell(x, y);
                    if (!Grid.IsValidCell(cell)) continue;
                    for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer)
                        if (Grid.Objects[cell, layer] is UnityEngine.GameObject o && o != null) found.Add(o);
                }
            foreach (var p in Components.Pickupables.Items)
            {
                if (p == null) continue;
                Grid.CellToXY(Grid.PosToCell(p), out var px, out var py);
                if (px >= left && px < left + w && py >= bottom && py < bottom + h) found.Add(p.gameObject);
            }
            foreach(var primary in FixturePrimaryElements(left,bottom,w,h))found.Add(primary.gameObject);
            duplicants = found.Count(o => o.GetComponent<MinionIdentity>() != null);
            if (label != null && found.Count > 0) Debug.Log($"[Mixed Atmospheres] {label}: " + string.Join(", ", found.Select(o => o.name)));
            foreach (var o in found) if (o.GetComponent<MinionIdentity>() == null) Util.KDestroyGameObject(o);
            return found.Count - duplicants;
        }
        // The border, the solid walls and vacuum inside; F4's granite divider.
        // The protocol's arena holds no buildings, debris or creatures: the
        // plants, critters and seeds the world left there are removed first
        // (they kept their own temperature and exchanged heat with the gas).
        private static void Build()
        {
            for (var y = 0; y < height; ++y)
                for (var x = 0; x < width; ++x)
                {
                    var border = x == 0 || y == 0 || x == width - 1 || y == height - 1;
                    if (border || Wall(x, y)) Replace(x, y, SimHashes.Unobtanium, 20000, Room);
                    else if (Id == "f4" && x == 3)
                    {
                        var tile = Assets.GetBuildingDef(TileConfig.ID).Mass[0];
                        placed["granite_tile_kg"] = tile;
                        Replace(x, y, SimHashes.Granite, tile, 318.15);
                    }
                    else Replace(x, y, SimHashes.Vacuum, 0, 0);
                }
        }

        // Evidence only: the native hook waits for these exact binary32 inputs
        // before its first heat update, and records both native source arrays.
        private static bool F4CaptureReady(string suffix,int requestedCount,string label)
        {
            var path=admissionPath+suffix;
            if(!File.Exists(path))return false;
            byte[] bytes;
            try {bytes=File.ReadAllBytes(path);}catch(IOException){return false;}
            if(bytes.Length<56)return false;
            using(var reader=new BinaryReader(new MemoryStream(bytes)))
            {
                var magic=System.Text.Encoding.ASCII.GetString(reader.ReadBytes(8));
                if(magic!="MAFLPR01" && magic!="MAFLPR02")throw new InvalidDataException("F4 preparation capture framing.");
                foreach(var dimension in new[]{Grid.WidthInCells,Grid.HeightInCells,x0,y0,width,height})
                    if(reader.ReadUInt32()!=(uint)dimension)throw new InvalidDataException("F4 preparation geometry differs.");
                var ticket=reader.ReadUInt64();var seconds=reader.ReadDouble();var mask=reader.ReadUInt32();var count=reader.ReadUInt32();
                var expected=56+(magic=="MAFLPR02"?12:0)+28*requestedCount;
                if(bytes.Length<expected)return false;
                if(bytes.Length!=expected || mask!=3 || count!=requestedCount || ticket==0 || Math.Abs(seconds-.2)>1e-6)
                    throw new InvalidDataException("F4 preparation lacks its exact quiescent native endpoint.");
                placed[label+"_native_ticket"]=ticket;placed[label+"_step_seconds"]=seconds;
                placed[label+"_capture_phase"]=magic=="MAFLPR02"?reader.ReadUInt32():0;
            }
            return true;
        }
        private static void RequestAdmissionCapture(string suffix="",bool vacuum=false,bool solidsOnly=false,bool fluidsOnly=false)
        {
            var path=admissionPath+suffix;
            if(admissionPath.Length==0)return;
            if(File.Exists(path) || File.Exists(path+".request"))throw new IOException("Floor admission requires a fresh private output path.");
            var requested=new List<(int x,int y)>();
            for(var y=0;y<height;++y)for(var x=0;x<width;++x)
                if((!solidsOnly || x==3 && y>=1 && y<=2) && (!fluidsOnly || x>=1 && x<=5 && x!=3 && y>=1 && y<=2))requested.Add((x,y));
            using(var stream=new FileStream(path+".request.tmp",FileMode.CreateNew,FileAccess.Write))
            using(var writer=new BinaryWriter(stream))
            {
                writer.Write(System.Text.Encoding.ASCII.GetBytes("MAFLRQ01"));
                foreach(var dimension in new[]{Grid.WidthInCells,Grid.HeightInCells,x0,y0,width,height})writer.Write((uint)dimension);
                writer.Write((uint)requested.Count);
                foreach(var (x,y) in requested)
                {
                    var border=x==0 || y==0 || x==width-1 || y==height-1;
                    var empty=vacuum && !border && x!=3;
                    var element=border?SimHashes.Unobtanium:x==3?SimHashes.Granite:empty?SimHashes.Vacuum:SimHashes.Oxygen;
                    var mass=border?20000:x==3?placed["granite_tile_kg"]:empty?0:placed[x<3?"hot_cell_kg":"cold_cell_kg"];
                    var temperature=border?Room:x==3?318.15:empty?0:x<3?353.15:283.15;
                    writer.Write((uint)Cell(x,y));writer.Write((uint)ElementLoader.FindElementByHash(element).idx);
                    writer.Write((float)mass);writer.Write((float)temperature);
                }
            }
            File.Move(path+".request.tmp",path+".request");
        }

        // Gas from the ideal-gas law at the stated pressure (1 m^3 cells);
        // liquid as full cells at 1 atm in the colony catalogue's reference EOS.
        // Every other open cell is emptied again: under the owner, closing the
        // border pushed its fluid into the arena.
        private static void Fill()
        {
            // What removing the plants dropped (seeds, harvests) goes too.
            placed["removed_at_fill"] = Clear(x0, y0, width, height, out _, "floor arena cleared at fill");
            for (var y = 1; y < height - 1; ++y)
                for (var x = 1; x < width - 1; ++x)
                    if (!Solid(x, y)) Replace(x, y, SimHashes.Vacuum, 0, 0);
            var oxygen = ElementLoader.FindElementByHash(SimHashes.Oxygen);
            double Gas(double pa, double kelvin) => pa * oxygen.molarMass / 1000 / (GasConstant * kelvin);
            placed["oxygen_molar_mass_kg_per_mol"] = oxygen.molarMass / 1000;
            switch (Id)
            {
                case "f1": case "f2":
                    var left = Gas(Id == "f1" ? Atmosphere : 2 * Atmosphere, Room);
                    var right = Id == "f1" ? 0 : Gas(.5 * Atmosphere, Room);
                    placed["left_cell_kg"] = left; placed["right_cell_kg"] = right;
                    for (var y = 1; y <= 8; ++y)
                        for (var x = 1; x <= 17; ++x)
                            if (x < 9) Replace(x, y, SimHashes.Oxygen, left, Room);
                            else if (x > 9 && right > 0) Replace(x, y, SimHashes.Oxygen, right, Room);
                    break;
                case "f3":
                    var water = FullCell(SimHashes.Water);
                    for (var y = 1; y <= 8; ++y) for (var x = 1; x <= 5; ++x) Replace(x, y, SimHashes.Water, water, Room);
                    break;
                case "f4":
                    placed["hot_cell_kg"] = Gas(Atmosphere, 353.15); placed["cold_cell_kg"] = Gas(Atmosphere, 283.15);
                    RequestAdmissionCapture();
                    RequestAdmissionCapture(".solids",solidsOnly:true);
                    // Publish the exact-state predicate before any worker can
                    // consume a fill message. It rejects vacuum/partial state.
                    if(ColonyOwner.Active)RequestReleaseCapture(Enumerable.Range(1,2).SelectMany(y=>new[]{(1,y),(2,y),(4,y),(5,y)}).ToArray());
                    for (var y = 1; y <= 2; ++y)
                    {
                        for (var x = 1; x <= 2; ++x) Replace(x, y, SimHashes.Oxygen, placed["hot_cell_kg"], 353.15);
                        for (var x = 4; x <= 5; ++x) Replace(x, y, SimHashes.Oxygen, placed["cold_cell_kg"], 283.15);
                    }
                    break;
                case "f5":
                    var w = FullCell(SimHashes.Water); var oil = FullCell(SimHashes.CrudeOil);
                    for (var x = 1; x <= 8; ++x)
                    {
                        for (var y = 1; y <= 4; ++y) Replace(x, y, SimHashes.Water, w, Room);
                        for (var y = 7; y <= 8; ++y) Replace(x, y, SimHashes.CrudeOil, oil, Room);
                    }
                    break;
            }
        }
        private static double FullCell(SimHashes liquid)
        {
            var materials = ColonyOwner.BuildCatalogue();
            OniNativeColony.Check(OniNativeColony.LiquidDensity(materials, (uint)materials.Length,
                (uint)ElementLoader.FindElementByHash(liquid).idx, Atmosphere, out var density));
            placed[liquid + "_full_cell_kg"] = density;
            return density;
        }
        // The game changes a door's state through an errand unless instant
        // building is on; the arena applies the request at once.
        private static void SetDoor(Door.ControlState state)
        {
            door!.QueueStateChange(state);
            AccessTools.Method(typeof(Door), "ApplyRequestedControlState").Invoke(door, Array.Empty<object>());
        }

        private static void Finish()
        {
            samples?.Dispose(); samples = null;
            var record = new Dictionary<string, object>
            {
                ["arena"] = Id, ["width"] = width, ["height"] = height, ["tick_seconds"] = .2,
                ["origin_x"] = x0, ["origin_y"] = y0, ["grid_width"] = Grid.WidthInCells, ["grid_height"] = Grid.HeightInCells,
                ["temperature_storage"] = "binary32",
                ["fluid_sampling"] = historyStarted ? "retained completed owner frames" : ColonyOwner.Active ? "single completed owner frame" : "native published update",
                ["initial_time_origin"] = Id=="f4"?"before first completed fluid interval; separate exact admission proofs required":"release sample",
                ["solid_sampling"] = historyStarted && Id == "f4" ? "retained native heat-input ticket paired with the same owner frame" : "native published update",
                ["owner_sample_history"] = historyStarted, ["owner_tick_offset"] = ownerTickOffset,
                ["owner_sample_frames"] = ownerSampleFrames,
                ["owner_sample_audits"] = ownerSampleAudits,
                ["booked_inventory_scope"] = "fluid+reservoir",
                ["simulation"] = ColonyOwner.Active ? "colony owner" : "ONI native",
                ["cells"] = "interior, rows y=1..height-2, x=1..width-2; per cell: constituent count, (element index, kg) each, temperature K",
                ["elements"] = seen.ToDictionary(e => e.Key.ToString(CultureInfo.InvariantCulture), e => (object)e.Value),
                ["placed"] = placed,
                ["ticks"] = new Dictionary<string, long> { ["cleared"] = cleared, ["uncovered"] = uncovered, ["built"] = built, ["filled"] = filled, ["locked"] = locked, ["commanded"] = commanded,
                    ["opened"] = opened, ["release"] = release, ["last"] = release + Duration },
                // Under the owner: its ledgers and the profile's trace sink, whole colony.
                ["owner"] = ColonyOwner.Active ? (object)new Dictionary<string, object>
                {
                    ["maximum_relative_mass_error"] = ColonyOwner.Status.MaximumRelativeMassError,
                    ["maximum_relative_energy_error"] = ColonyOwner.Status.MaximumRelativeEnergyError,
                    ["trace_sink_kg"] = ColonyOwner.Status.TraceSinkKg, ["trace_sink_j"] = ColonyOwner.Status.TraceSinkJoules,
                    ["skipped_relaxations"] = ColonyOwner.Status.SkippedRelaxations, ["error"] = ColonyOwner.Status.Error,
                } : "none",
                // A check on the tick length: game seconds from release to the end.
                ["game_seconds_after_release"] = GameClock.Instance.GetTime() - releasedAt,
                ["simulated_seconds_after_release"] = simulated - simulatedAt,
            };
            File.WriteAllText(Path.Combine(output, "floor-" + Id + ".json"), ReportJson.Serialize(record));
        }
    }
}
