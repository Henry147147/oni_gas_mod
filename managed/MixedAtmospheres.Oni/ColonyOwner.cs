using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Normal-game owner of every gas and liquid on the asteroid
    // (docs/colony-owner.md). It starts on a new base-game colony at the first
    // completed native frame, publishes the owner's completed frame into the
    // game's data copy and routes mass and heat messages for owned cells.
    internal static unsafe class ColonyOwner
    {
        private const int NewGameFrame = -775326397, PrepareGameData = 1078620451, MassConsumption = 1727657959, MassEmission = 797274363,
            ModifyCell = -1252920804, ModifyCellEnergy = 818320644, Dig = 833038498, CellProperties = -469311643;
        private const byte LiquidImpermeable = 2;
        // Calm asteroids take full steps of this length; flow and the pressure
        // solve shorten them where needed (docs/colony-owner.md#pacing).
        // Two steps exactly cover the native binary32 0.2 s frame.
        internal const double MaximumTimestepSeconds = .1f;
        // New colonies run the saved base-game accuracy-floor profile
        // (docs/base-game-floor.md) once its threshold is selected; until
        // then only a test run selects it (MA_COLONY_GAME_FLOOR=1). A restored
        // colony keeps the profile its save holds.
        internal static bool NewColoniesUseGameFloor => Environment.GetEnvironmentVariable("MA_COLONY_GAME_FLOOR") == "1";
        // Native messages seen while the owner runs, by hash (development census).
        internal static readonly Dictionary<int, long> Census = new Dictionary<int, long>();
        internal static void Count(int message, int count) { if (Active) Census[message] = (Census.TryGetValue(message, out var n) ? n : 0) + count; }
        // Game-thread milliseconds spent inside native frame messages (pacing data).
        internal static readonly Dictionary<int, double> BlockedMilliseconds = new Dictionary<int, double>();
        internal static readonly Dictionary<int, double> LongestBlockMilliseconds = new Dictionary<int, double>();
        internal static void RecordBlocked(int message, long ticks)
        {
            var ms = ticks * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
            BlockedMilliseconds[message] = (BlockedMilliseconds.TryGetValue(message, out var total) ? total : 0) + ms;
            LongestBlockMilliseconds[message] = Math.Max(LongestBlockMilliseconds.TryGetValue(message, out var longest) ? longest : 0, ms);
        }
        internal static bool Active { get; private set; }
        internal static OniNativeColony.Status Status => status;
        internal static string StartFailure { get; private set; } = "";
        internal static int OwnedCells { get; private set; }
        internal static long Publications { get; private set; }
        // Game-thread time of the last and longest publication (data copy,
        // views, events, receipts, actors, presentation hand-off).
        internal static double PublicationMilliseconds { get; private set; }
        internal static double MaximumPublicationMilliseconds { get; private set; }
        private static readonly bool publicationLog = Environment.GetEnvironmentVariable("MA_COLONY_PUBLICATION_LOG") == "1";
        private static long completionObservedAt;
        private static ulong completionObservedFrame = ulong.MaxValue;
        private static readonly ColonyFrameTiming frameTiming = new ColonyFrameTiming();
        private static OniNativeColony.Status status = OniNativeColony.Status.Empty;
        private static bool considered;
        private static int x0, y0, width, height;
        private static OniNativeColony.Config config;
        private static OniNativeColony.Material[] materials = Array.Empty<OniNativeColony.Material>();
        private static OniNativeColony.CellView[] views = Array.Empty<OniNativeColony.CellView>();
        private static OniNativeColony.Receipt[] receipts = new OniNativeColony.Receipt[4096];
        private static bool[] catalogued = Array.Empty<bool>();
        private static OwnedGameDataSnapshot? current;
        private static string loggedFault = "";

        // The owner needs the attached native hooks; development validation
        // scenes keep their own owners, except the colony-owner scene.
        internal static bool Available => NativeBackendObservation.Enabled &&
            (ColonyRuntimeValidation.Settings == null || ColonyRuntimeValidation.Settings.Mode.StartsWith("colony-owner", StringComparison.Ordinal));

        // Presentation is a reader: its failure leaves the vanilla projection.
        private static void ConfigurePresentation()
        {
            try { ColonyPresentation.Configure(); }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] Colony presentation unavailable: " + error.Message); }
        }

        internal static void ResetForNewSession()
        {
            ColonyPresentation.Reset(); ColonyActors.Reset(); ColonyViewCache.Reset();
            current?.Dispose(); current = null;
            if (Active) { var s = OniNativeColony.Status.Empty; _ = OniNativeColony.Stop(ref s); }
            ColonyPacing.Reset(); ColonyOwnerPersistence.Reset();
            Active = false; considered = false; StartFailure = ""; loggedFault = ""; Publications = 0; PublicationMilliseconds = MaximumPublicationMilliseconds = 0; Census.Clear(); molarMass = Array.Empty<double>();
            completionObservedAt = 0; completionObservedFrame = ulong.MaxValue; frameTiming.Reset();
        }

        private static bool InRect(int cell, out int index)
        {
            index = -1;
            if (cell < 0 || cell >= Grid.CellCount) return false;
            var x = cell % Grid.WidthInCells - x0; var y = cell / Grid.WidthInCells - y0;
            if (x < 0 || y < 0 || x >= width || y >= height) return false;
            index = x + y * width; return true;
        }
        internal static bool OwnsFluidCell(int cell) => OwnsFluid(cell);
        private static bool OwnsFluid(int cell) => Active && InRect(cell, out var index) && (views[index].Flags & OniNativeColony.ViewOwned) != 0;

        internal static OniNativeColony.Material[] BuildCatalogue()
        {
            var list = new List<OniNativeColony.Material>();
            foreach (var e in ElementLoader.elements)
            {
                if (e == null || e.disabled || !(e.IsGas || e.IsLiquid)) continue;
                if (!(e.molarMass > 0) || !(e.specificHeatCapacity > 0) || e.IsLiquid && !(e.maxMass > 0)) continue;
                list.Add(new OniNativeColony.Material
                {
                    Size = 168, Version = OniNativeColony.AbiVersion, ElementHash = (int)e.id, NativeIndex = (uint)e.idx, Phase = e.IsGas ? 1U : 2U,
                    MolarMassKgPerMol = e.molarMass / 1000.0, SpecificHeatJPerKgK = e.specificHeatCapacity * 1000.0,
                    ConductivityWPerMK = Math.Max(0, (double)e.thermalConductivity), DensityKgPerM3 = e.IsLiquid ? e.maxMass : 0,
                    LowTemperatureK = e.lowTemp, HighTemperatureK = e.highTemp,
                    LowTarget = (int)e.lowTempTransitionTarget, HighTarget = (int)e.highTempTransitionTarget,
                    LowByproduct = (int)e.lowTempTransitionOreID, HighByproduct = (int)e.highTempTransitionOreID,
                    LowByproductFraction = Finite(e.lowTempTransitionOreMassConversion), HighByproductFraction = Finite(e.highTempTransitionOreMassConversion),
                    Id = e.id.ToString()
                });
            }
            return list.ToArray();
        }
        private static double Finite(float value) => float.IsNaN(value) || float.IsInfinity(value) ? 0 : value;

        private static void TryStart(Sim.GameDataUpdate* source)
        {
            if (considered || Game.Instance == null || SaveLoader.Instance == null || source->numFramesProcessed <= 0) return;
            considered = true;
            try
            {
                if (SaveLoader.Instance.loadedFromSave) { StartFailure = "Loaded saves keep their saved owner"; return; }
                if (DlcManager.IsExpansion1Active()) { StartFailure = "Spaced Out! is not supported yet"; return; }
                if (ClusterManager.Instance == null || ClusterManager.Instance.WorldContainers.Count != 1)
                { StartFailure = "The colony owner needs exactly one asteroid"; return; }
                OniNativeColony.CheckLayouts();
                (config, materials) = Describe();
                x0 = (int)config.OffsetX; y0 = (int)config.OffsetY; width = (int)config.Width; height = (int)config.Height;
                catalogued = new bool[ElementLoader.elements.Count];
                foreach (var m in materials) catalogued[m.NativeIndex] = true;
                var vacuum = ElementLoader.FindElementByHash(SimHashes.Vacuum).idx;
                var cells = new OniNativeColony.Cell[width * height];
                var zones = World.Instance.zoneRenderData;
                for (var i = 0; i < cells.Length; ++i)
                {
                    var global = (x0 + i % width) + (y0 + i / width) * Grid.WidthInCells;
                    var element = ElementLoader.elements[source->elementIdx[global]];
                    uint kind = element.IsSolid ? OniNativeColony.Solid :
                        element.IsVacuum && zones != null && zones.GetSubWorldZoneType(global) == ProcGen.SubWorld.ZoneType.Space ? OniNativeColony.Space :
                        OniNativeColony.Fluid;
                    var germ = kind == OniNativeColony.Fluid && source->diseaseCount[global] > 0 ? OniNativeColony.Germ(source->diseaseIdx[global]) : 0u;
                    cells[i] = new OniNativeColony.Cell { NativeIndex = element.idx, Kind = kind,
                        MassKg = kind == OniNativeColony.Fluid ? source->mass[global] : 0, TemperatureK = kind == OniNativeColony.Fluid ? source->temperature[global] : 0,
                        Disease = germ, DiseaseCount = germ != 0 ? source->diseaseCount[global] : 0 };
                    if (kind == OniNativeColony.Fluid && element.idx != vacuum && !catalogued[element.idx])
                        throw new NotSupportedException("Fluid element " + element.id + " has no catalogue entry.");
                }
                var started = System.Diagnostics.Stopwatch.StartNew();
                var output = OniNativeColony.Status.Empty;
                OniNativeColony.Check(OniNativeColony.Start(ref config, materials, (uint)materials.Length, cells, (uint)cells.Length, ref output));
                status = output; views = new OniNativeColony.CellView[cells.Length]; Active = true; ColonyPacingDriver.Ensure(); ConfigurePresentation();
                OwnedCells = cells.Count(c => c.Kind != OniNativeColony.Solid);
                // Backwall cells are a native layer behind the cells; their transitions emit through AddRemoveSubstance.
                var backwalls = 0;
                for (var i = 0; i < cells.Length; ++i) if (BackwallManager.HasBackwall((x0 + i % width) + (y0 + i / width) * Grid.WidthInCells)) ++backwalls;
                Debug.Log($"[Mixed Atmospheres] Colony owner started: {width}x{height} cells, {OwnedCells} owned, {materials.Length} materials, " +
                    $"reservoir {status.ReservoirKg:G6} kg, {backwalls} backwall cells, {started.ElapsedMilliseconds} ms.");
            }
            catch (Exception error)
            {
                StartFailure = error.Message;
                Debug.LogWarning("[Mixed Atmospheres] Colony owner did not start; native physics remains: " + error.Message);
            }
        }

        internal static OniNativeColony.Config Config => config;
        internal static OniNativeColony.Material[] Materials => materials;
        internal static byte[] Checkpoint()
        {
            OniNativeColony.Check(OniNativeColony.Checkpoint(null, 0, out var bytes));
            var image = new byte[bytes];
            OniNativeColony.Check(OniNativeColony.Checkpoint(image, bytes, out var written));
            if (written != bytes) throw new InvalidOperationException("Colony checkpoint size changed while copying.");
            return image;
        }
        // The configuration and catalogue the running game would give this asteroid.
        internal static (OniNativeColony.Config config, OniNativeColony.Material[] materials) Describe()
        {
            if (ClusterManager.Instance == null || ClusterManager.Instance.WorldContainers.Count != 1)
                throw new NotSupportedException("The colony owner needs exactly one asteroid.");
            var world = ClusterManager.Instance.WorldContainers[0];
            var described = new OniNativeColony.Config
            {
                Size = 56, Version = OniNativeColony.AbiVersion,
                Flags = OniNativeColony.PhysicsConduction | (NewColoniesUseGameFloor ? OniNativeColony.GameFloor : 0),
                VacuumIndex = ElementLoader.FindElementByHash(SimHashes.Vacuum).idx,
                GridWidth = (uint)Grid.WidthInCells, GridHeight = (uint)Grid.HeightInCells,
                OffsetX = (uint)world.WorldOffset.x, OffsetY = (uint)world.WorldOffset.y, Width = (uint)world.WorldSize.x, Height = (uint)world.WorldSize.y,
                GravityMetresPerSecondSquared = 9.81, MaximumTimestepSeconds = MaximumTimestepSeconds
            };
            // A floor measurement under the owner owns only its arena, border
            // included; the rest of the asteroid stays native.
            if (FloorArenas.OwnerRectangle(out var ax, out var ay, out var aw, out var ah))
            {
                described.OffsetX = (uint)ax; described.OffsetY = (uint)ay; described.Width = (uint)aw; described.Height = (uint)ah;
            }
            return (described, BuildCatalogue());
        }
        // Resume a saved owner after the native simulator has started.
        internal static void Restore(OniNativeColony.Config saved, byte[] image)
        {
            if (Active) throw new InvalidOperationException("A colony owner is already active.");
            OniNativeColony.CheckLayouts();
            var (live, catalogue) = Describe();
            // New-colony defaults may change; the native image validates its saved timestep.
            live.MaximumTimestepSeconds = saved.MaximumTimestepSeconds;
            live.Flags = (live.Flags & ~OniNativeColony.GameFloor) | (saved.Flags & OniNativeColony.GameFloor);
            if (!live.Equals(saved)) throw new NotSupportedException("The saved colony geometry differs from the loaded asteroid.");
            config = saved; materials = catalogue;
            x0 = (int)config.OffsetX; y0 = (int)config.OffsetY; width = (int)config.Width; height = (int)config.Height;
            var output = OniNativeColony.Status.Empty;
            OniNativeColony.Check(OniNativeColony.Restore(ref config, materials, (uint)materials.Length, image, (uint)image.Length, ref output));
            status = output; views = new OniNativeColony.CellView[width * height]; considered = true; Active = true; ColonyPacingDriver.Ensure(); ConfigurePresentation();
            OniNativeColony.Check(OniNativeColony.Copy(ref status, views, (uint)views.Length));
            OwnedCells = views.Count(v => (v.Flags & OniNativeColony.ViewOwned) != 0);
            Debug.Log($"[Mixed Atmospheres] Colony owner restored at frame {status.CompletedFrames}: {OwnedCells} owned cells.");
        }

        // After every native message: start at the first completed frame, then
        // publish each completed owner frame into the game's data copy.
        internal static IntPtr AfterNativeMessage(int message, IntPtr pointer)
        {
            if (message != PrepareGameData || pointer == IntPtr.Zero || !Available) return pointer;
            var source = (Sim.GameDataUpdate*)pointer;
            ColonyActors.Observe(source);
            if (!Active && ClusterManager.Instance != null && ColonyOwnerPersistence.TakePending() is var (saved, image)) Restore(saved, image);
            if (!Active) { TryStart(source); if (!Active) return pointer; }
            var started = System.Diagnostics.Stopwatch.GetTimestamp();
            var lapStarted = started;
            var laps = publicationLog ? new System.Text.StringBuilder() : null;
            void Lap(string name)
            {
                if (laps == null) return;
                var now = System.Diagnostics.Stopwatch.GetTimestamp();
                laps.AppendFormat(System.Globalization.CultureInfo.InvariantCulture, " {0} {1:F3}", name,
                    (now - lapStarted) * 1000.0 / System.Diagnostics.Stopwatch.Frequency);
                lapStarted = now;
            }
            var output = OniNativeColony.Status.Empty;
            ColonyViewCache.Copy(ref output, views);
            Lap("views");
            status = output;
            if (publicationLog && status.CompletedFrames != completionObservedFrame)
            {
                var now = System.Diagnostics.Stopwatch.GetTimestamp();
                var matched = frameTiming.Observe(status.CompletedFrames, out var submittedAt);
                var elapsed = matched ? (now - submittedAt) * 1000.0 / System.Diagnostics.Stopwatch.Frequency : 0;
                Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] handoff frame {status.CompletedFrames} matched {(matched ? 1 : 0)} submit_to_observed_ms {elapsed:F3} owner_ms {status.LastAdvanceMilliseconds:F3} frame_s {status.LastFrameSeconds:R}"));
                completionObservedAt = now; completionObservedFrame = status.CompletedFrames;
            }
            if (status.State == 2 && loggedFault != status.Error)
            {
                loggedFault = status.Error;
                Debug.LogError("[Mixed Atmospheres] Colony owner faulted; fluids are frozen: " + status.Error);
            }
            var copy = OwnedGameDataSnapshot.Capture(source, Grid.WidthInCells, Grid.HeightInCells);
            Lap("snapshot");
            try
            {
                var masses = copy.CellBuffer<float>(nameof(Sim.GameDataUpdate.mass));
                var temperatures = copy.CellBuffer<float>(nameof(Sim.GameDataUpdate.temperature));
                var elements = copy.CellBuffer<ushort>(nameof(Sim.GameDataUpdate.elementIdx));
                var diseases = copy.CellBuffer<byte>(nameof(Sim.GameDataUpdate.diseaseIdx));
                var germs = copy.CellBuffer<int>(nameof(Sim.GameDataUpdate.diseaseCount));
                var changes = new List<Sim.SubstanceChangeInfo>();
                for (var i = 0; i < views.Length; ++i)
                {
                    var view = views[i];
                    var global = (x0 + i % width) + (y0 + i / width) * Grid.WidthInCells;
                    // A cell the owner does not publish shows native's cell, as
                    // does one a solid has just replaced (the owner displaces its
                    // fluid at its next step). ONI's Grid.Element follows only
                    // substance-change events, so a cell the owner left gets one.
                    if ((view.Flags & OniNativeColony.ViewOwned) == 0 || ElementLoader.elements[elements[global]].IsSolid)
                    {
                        if (Grid.Element[global] != null && Grid.Element[global].idx != elements[global])
                            changes.Add(new Sim.SubstanceChangeInfo { cellIdx = global, newElemIdx = elements[global] });
                        continue;
                    }
                    var element = (ushort)view.ElementIndex;
                    masses[global] = view.MassKg; temperatures[global] = view.MassKg > 0 ? view.TemperatureK : 0;
                    diseases[global] = OniNativeColony.DiseaseIdx(view.Disease); germs[global] = view.Disease != 0 ? view.DiseaseCount : 0;
                    if (elements[global] != element || Grid.Element[global].idx != element)
                        changes.Add(new Sim.SubstanceChangeInfo { cellIdx = global, newElemIdx = element });
                    elements[global] = element;
                }
                copy.AppendEvents<Sim.SubstanceChangeInfo>(SnapshotEventChannel.SubstanceChangeInfo, changes.ToArray());
                Lap("overlay");
                DeliverReceipts(copy);
                Lap("receipts");
                ColonyActors.Publish(copy);
                Lap("actors");
                var previous = current; current = copy; previous?.Dispose();
                Lap("retire");
                ++Publications;
                ColonyPresentation.Publish(status.CompletedFrames);
                Lap("presentation");
                PublicationMilliseconds = (System.Diagnostics.Stopwatch.GetTimestamp() - started) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
                MaximumPublicationMilliseconds = Math.Max(MaximumPublicationMilliseconds, PublicationMilliseconds);
                if (laps != null) Debug.Log($"[Mixed Atmospheres] publication frame {status.CompletedFrames} native {source->numFramesProcessed}:{laps}");
                return (IntPtr)copy.Pointer;
            }
            catch { copy.Dispose(); throw; }
        }

        // A conversion's solid byproduct (salt from boiling salt water) becomes debris.
        private static void SpawnByproduct(in OniNativeColony.Receipt r)
        {
            var element = ElementLoader.FindElementByHash((SimHashes)r.Callback);
            if (element == null || !(r.MassKg > 0)) return;
            element.substance.SpawnResource(Grid.CellToPosCCC((int)r.PublicCell, Grid.SceneLayer.Ore), r.MassKg, r.TemperatureK, byte.MaxValue, 0);
        }
        private static void DeliverReceipts(OwnedGameDataSnapshot copy)
        {
            OniNativeColony.Check(OniNativeColony.CopyReceipts(receipts, (uint)receipts.Length, out var count));
            if (count == 0) return;
            var consumed = new List<Sim.MassConsumedCallback>(); var emitted = new List<Sim.MassEmittedCallback>();
            var removed = new List<Sim.ConsumedMassInfo>();
            var through = 0UL; var delivered = (int)Math.Min(count, (uint)receipts.Length);
            for (var k = 0; k < delivered; ++k)
            {
                var r = receipts[k]; through = r.Sequence;
                if (ColonyPipePorts.Receipt(in r)) continue;
                if (ColonyActors.IsConsumerReceipt(r)) { if (r.MassKg > 0) removed.Add(ColonyActors.Event(r)); }
                else if (r.Kind == OniNativeColony.Byproduct) SpawnByproduct(r);
                else if (r.Kind == OniNativeColony.Extract)
                    consumed.Add(new Sim.MassConsumedCallback { callbackIdx = r.Callback, elemIdx = (ushort)r.NativeIndex, mass = r.MassKg,
                        temperature = r.MassKg > 0 ? r.TemperatureK : 0, diseaseIdx = OniNativeColony.DiseaseIdx(r.Disease), diseaseCount = r.DiseaseCount });
                else
                    emitted.Add(new Sim.MassEmittedCallback { callbackIdx = r.Callback, elemIdx = (ushort)r.NativeIndex, suceeded = (byte)(r.MassKg > 0 ? 1 : 0),
                        mass = r.MassKg, temperature = r.TemperatureK, diseaseIdx = OniNativeColony.DiseaseIdx(r.Disease), diseaseCount = r.DiseaseCount });
            }
            copy.AppendEvents<Sim.MassConsumedCallback>(SnapshotEventChannel.MassConsumedCallbacks, consumed.ToArray());
            copy.AppendEvents<Sim.MassEmittedCallback>(SnapshotEventChannel.MassEmittedCallbacks, emitted.ToArray());
            copy.AppendEvents<Sim.ConsumedMassInfo>(SnapshotEventChannel.RemovedMassEntries, removed.ToArray());
            OniNativeColony.Check(OniNativeColony.AckReceipts(through));
            ColonyPipePorts.Acknowledged(through);
        }

        // Mass and heat messages for owned fluid cells go to the owner. Returns
        // true when the whole batch was claimed.
        internal static bool TryHandle(int message, int length, int count, byte* payload)
        {
            if (count <= 0 || payload == null || !Available) return false;
            if (ColonyActors.TryHandle(message, length, count, payload, Active)) return true;
            if (!Active) return false;
            if (message == NewGameFrame && length >= 4)
            {
                var begun = publicationLog ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;
                ColonyActors.QueueFrame(*(float*)payload);
                if (publicationLog && *(float*)payload > 0)
                {
                    var now = System.Diagnostics.Stopwatch.GetTimestamp();
                    var factor = 1000.0 / System.Diagnostics.Stopwatch.Frequency;
                    var ready = completionObservedAt == 0 ? 0 : (now - completionObservedAt) * factor;
                    var submittedFrame = frameTiming.Submit(status.CompletedFrames, now);
                    Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] submit target_frame {submittedFrame} observed_frame {status.CompletedFrames} since_observed_ms {ready:F3} queue_actors_ms {(now - begun) * factor:F3}; busy {OniNativeColony.Busy()}"));
                }
                return false;
            }
            switch (message)
            {
                case MassEmission: return Claim(length, 24, count, payload, (data, list) =>
                {
                    var cell = BinaryPrimitives.ReadInt32LittleEndian(data);
                    // cell, callback, mass, temperature, germs, element, disease
                    var germs = BinaryPrimitives.ReadInt32LittleEndian(data.Slice(16)); var germ = germs > 0 ? OniNativeColony.Germ(data[22]) : 0u;
                    list.Add(new OniNativeColony.Transfer { PublicCell = (uint)cell, NativeIndex = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(20)),
                        Kind = OniNativeColony.Inject, Callback = BinaryPrimitives.ReadInt32LittleEndian(data.Slice(4)),
                        MassKg = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(8))),
                        TemperatureK = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(12))),
                        Disease = germ, DiseaseCount = germ != 0 ? germs : 0 });
                }, rectAnywhere: true);
                case MassConsumption: return Claim(length, 16, count, payload, (data, list) =>
                {
                    var cell = BinaryPrimitives.ReadInt32LittleEndian(data);
                    list.Add(new OniNativeColony.Transfer { PublicCell = (uint)cell, NativeIndex = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(12)),
                        Kind = OniNativeColony.Extract, Radius = Math.Max((uint)data[14], (uint)data[15]), Callback = BinaryPrimitives.ReadInt32LittleEndian(data.Slice(4)),
                        MassKg = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(8))) });
                }, rectAnywhere: true);
                case CellProperties when length == 12:
                {
                    // An airflow tile (liquid-impermeable, not solid) passes gas
                    // but no liquid; ONI still records the property natively.
                    for (var k = 0; k < count; ++k)
                    {
                        var d = payload + k * length;
                        var cell = *(int*)d;
                        if ((d[8] & LiquidImpermeable) != 0 && InRect(cell, out _))
                            QueueTerrain(cell, d[9] != 0 ? OniNativeColony.BlockLiquid : OniNativeColony.PassLiquid);
                    }
                    return false;
                }
                case Dig:
                {
                    // ONI digs the tile natively; the owner opens the cell.
                    if (length < 4) return false;
                    var cell = *(int*)payload;
                    if (InRect(cell, out _) && !OwnsFluid(cell)) QueueTerrain(cell, OniNativeColony.Open);
                    return false;
                }
                case ModifyCell when length == 28 && payload[22] != 0:
                {
                    // Element replacement (building, deconstruction, placement):
                    // ONI replaces natively; the owner closes or opens the cell
                    // and books any fluid the replacement places.
                    for (var k = 0; k < count; ++k)
                    {
                        var d = new ReadOnlySpan<byte>(payload + k * length, length);
                        var cell = BinaryPrimitives.ReadInt32LittleEndian(d);
                        if (!InRect(cell, out _)) continue;
                        var element = ElementLoader.elements[BinaryPrimitives.ReadUInt16LittleEndian(d.Slice(20))];
                        var mass = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(d.Slice(12)));
                        if (element.IsSolid) { if (OwnsFluid(cell)) QueueTerrain(cell, OniNativeColony.Close); continue; }
                        if (!OwnsFluid(cell)) QueueTerrain(cell, OniNativeColony.Open);
                        // A plain replacement replaces the cell's fluid; replace-and-displace adds to it.
                        else if (d[22] == 1) QueueTerrain(cell, OniNativeColony.Clear);
                        if ((element.IsGas || element.IsLiquid) && mass > 0)
                            OniNativeColony.Check(OniNativeColony.QueueTransfers(new[] { WithGerms(new OniNativeColony.Transfer { PublicCell = (uint)cell,
                                NativeIndex = element.idx, Kind = OniNativeColony.Inject, Callback = -1, MassKg = mass,
                                TemperatureK = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(d.Slice(8))) }, d) }, 1));
                    }
                    return false;
                }
                case ModifyCell:
                {
                    // Additive mass edits (no callback or replacement), with their germs.
                    if (length != 28) return false;
                    var cell = *(int*)payload;
                    // A solid added to an owned cell (falling sand or snow landing)
                    // becomes a tile there, as in ONI: the owner displaces the
                    // cell's fluid and closes it, and ONI places the tile as a
                    // plain replacement (its native addition never lands).
                    if (count == 1 && payload[22] == 0 && ElementLoader.elements[*(ushort*)(payload + 20)].IsSolid)
                    {
                        if (OwnsFluid(cell) && *(float*)(payload + 12) > 0) { QueueTerrain(cell, OniNativeColony.Close); payload[22] = 1; }
                        return false;
                    }
                    if (!OwnsFluid(cell)) return false;
                    var data = new ReadOnlySpan<byte>(payload, length * count);
                    var list = new List<OniNativeColony.Transfer>();
                    for (var k = 0; k < count; ++k)
                    {
                        var d = data.Slice(k * length, length);
                        // Byte 24 is ONI's vertical-solid-displacement flag, not
                        // the edit kind: landing water particles leave it clear.
                        if (!OwnsFluid(BinaryPrimitives.ReadInt32LittleEndian(d)) || d[22] != 0) return false;
                        // A solid in a batch stays ONI's.
                        var added = ElementLoader.elements[BinaryPrimitives.ReadUInt16LittleEndian(d.Slice(20))];
                        if (!(added.IsGas || added.IsLiquid)) return false;
                        var mass = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(d.Slice(12)));
                        if (mass < 0)
                        {
                            // A negative edit removes that element there.
                            list.Add(new OniNativeColony.Transfer { PublicCell = (uint)BinaryPrimitives.ReadInt32LittleEndian(d), NativeIndex = added.idx,
                                Kind = OniNativeColony.Extract, Callback = -1, MassKg = -mass });
                            continue;
                        }
                        if (!(mass > 0)) continue;
                        list.Add(WithGerms(new OniNativeColony.Transfer { PublicCell = (uint)BinaryPrimitives.ReadInt32LittleEndian(d),
                            NativeIndex = BinaryPrimitives.ReadUInt16LittleEndian(d.Slice(20)), Kind = OniNativeColony.Inject, Callback = -1, MassKg = mass,
                            TemperatureK = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(d.Slice(8))) }, d));
                    }
                    if (list.Count > 0) OniNativeColony.Check(OniNativeColony.QueueTransfers(list.ToArray(), (uint)list.Count));
                    return true;
                }
                case ModifyCellEnergy:
                {
                    if (length != 16) return false;
                    var list = new List<OniNativeColony.Heat>();
                    for (var k = 0; k < count; ++k)
                    {
                        var d = new ReadOnlySpan<byte>(payload + k * length, length);
                        var cell = BinaryPrimitives.ReadInt32LittleEndian(d);
                        if (!OwnsFluid(cell)) return k == 0 ? false : throw new NotSupportedException("A heat batch spans owned and native cells.");
                        list.Add(new OniNativeColony.Heat { PublicCell = (uint)cell,
                            Joules = 1000.0 * BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(d.Slice(4))),
                            MaximumTemperatureK = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(d.Slice(8))) });
                    }
                    OniNativeColony.Check(OniNativeColony.QueueHeat(list.ToArray(), (uint)list.Count));
                    return true;
                }
            }
            return false;
        }
        // A ModifyCell message's germs: count at 16, disease at 23.
        private static OniNativeColony.Transfer WithGerms(OniNativeColony.Transfer t, ReadOnlySpan<byte> d)
        {
            var germs = BinaryPrimitives.ReadInt32LittleEndian(d.Slice(16));
            if (germs > 0 && d[23] != byte.MaxValue) { t.Disease = OniNativeColony.Germ(d[23]); t.DiseaseCount = germs; }
            return t;
        }
        private static void QueueTerrain(int cell, uint kind) =>
            OniNativeColony.Check(OniNativeColony.QueueTerrain(new[] { new OniNativeColony.Terrain { PublicCell = (uint)cell, Kind = kind } }, 1));
        private delegate void TransferDecoder(ReadOnlySpan<byte> data, List<OniNativeColony.Transfer> list);
        private static bool Claim(int length, int expected, int count, byte* payload, TransferDecoder decode, bool rectAnywhere)
        {
            if (length != expected) return false;
            var list = new List<OniNativeColony.Transfer>();
            for (var k = 0; k < count; ++k)
            {
                var d = new ReadOnlySpan<byte>(payload + k * length, length);
                var cell = BinaryPrimitives.ReadInt32LittleEndian(d);
                var inside = rectAnywhere ? InRect(cell, out _) : OwnsFluid(cell);
                if (!inside) { if (k == 0) return false; throw new NotSupportedException("A transfer batch spans owned and native cells."); }
                decode(d, list);
            }
            OniNativeColony.Check(OniNativeColony.QueueTransfers(list.ToArray(), (uint)list.Count));
            return true;
        }

        // Owned pressure, oxygen and composition readers.
        internal static bool TryReadView(int cell, out OniNativeColony.CellView view)
        {
            view = default;
            if (!Active || !InRect(cell, out var index) || (views[index].Flags & OniNativeColony.ViewOwned) == 0) return false;
            view = views[index]; return true;
        }
        // Grid.Pressure's kPa scale.
        internal static bool TryReadPressure(int cell, out float kilopascals)
        {
            kilopascals = 0;
            if (!TryReadView(cell, out var view)) return false;
            kilopascals = view.PressurePa / 1000f; return true;
        }
        private static readonly uint[] partIndices = new uint[128];
        private static readonly double[] partMasses = new double[128];
        private static double[] molarMass = Array.Empty<double>();
        internal static int Composition(int cell, out uint[] indices, out double[] masses)
        {
            indices = partIndices; masses = partMasses;
            if (!Active || !InRect(cell, out _)) return 0;
            OniNativeColony.Check(OniNativeColony.Composition((uint)cell, partIndices, partMasses, (uint)partIndices.Length, out var count));
            return (int)Math.Min(count, (uint)partIndices.Length);
        }
        // Easy-mode breathing (docs/playtest-plan.md): at least 15% oxygen by
        // moles and 15 kPa oxygen partial pressure, carbon dioxide below 0.5%
        // and 500 Pa. Polluted oxygen counts as oxygen. A breathable cell offers
        // its oxygen of the more abundant kind; otherwise none.
        internal const double MinimumOxygenFraction = .15, MinimumOxygenPressurePa = 15000, MaximumCarbonDioxideFraction = .005, MaximumCarbonDioxidePressurePa = 500;
        internal static bool TryReadBreathingCell(int cell, out float mass, out SimHashes element)
        {
            mass = 0; element = SimHashes.Vacuum;
            if (!TryReadView(cell, out var view)) return false;
            if ((view.Flags & OniNativeColony.ViewGas) == 0 || status.State != 1) return true;
            if (molarMass.Length == 0)
            {
                molarMass = new double[ElementLoader.elements.Count];
                foreach (var m in materials) molarMass[m.NativeIndex] = m.MolarMassKgPerMol;
            }
            var count = Composition(cell, out var indices, out var masses);
            double moles = 0, oxygenMoles = 0, carbonMoles = 0, oxygen = 0, polluted = 0;
            for (var k = 0; k < count; ++k)
            {
                var e = ElementLoader.elements[(int)indices[k]]; if (!e.IsGas) continue;
                var n = masses[k] / molarMass[indices[k]]; moles += n;
                if (e.id == SimHashes.Oxygen) { oxygenMoles += n; oxygen += masses[k]; }
                else if (e.id == SimHashes.ContaminatedOxygen) { oxygenMoles += n; polluted += masses[k]; }
                else if (e.id == SimHashes.CarbonDioxide) carbonMoles += n;
            }
            if (!(moles > 0)) return true;
            var pressure = (double)view.PressurePa;
            var xo = oxygenMoles / moles; var xc = carbonMoles / moles;
            if (xo < MinimumOxygenFraction || xo * pressure < MinimumOxygenPressurePa || xc >= MaximumCarbonDioxideFraction ||
                xc * pressure >= MaximumCarbonDioxidePressurePa) return true;
            element = oxygen >= polluted ? SimHashes.Oxygen : SimHashes.ContaminatedOxygen;
            // Rounded down so the advertised amount never exceeds the cell.
            var offered = (float)(oxygen >= polluted ? oxygen : polluted);
            if (offered > (oxygen >= polluted ? oxygen : polluted)) offered = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(offered) - 1);
            mass = offered; return true;
        }
    }
}
