using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using static MixedAtmospheres.Oni.OniNativeDomain;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Configured sealed scene (native scenario 7): a room larger than the
    // legacy 3x2 rectangle with interior insulating solids. The owner holds
    // only the fluid cells; every solid inside the rectangle stays native data
    // and is verified exactly like the border: static, stable, non-transitioning,
    // insulating and empty. Interior fluid/solid faces are impermeable and
    // adiabatic. This is development evidence, not whole-colony ownership.
    // A paired save carries the session's exact geometry, physics and mask in
    // its MADCFG01 wrapper; a load restores exactly that configuration.
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool ConfiguredMode => ColonyRuntimeValidation.Settings?.Mode == "native-configured-domain" || ConfiguredLiquidMode || RestoredConfigured;
        private static bool ConfiguredLiquidMode => ColonyRuntimeValidation.Settings?.Mode == "native-configured-water-domain" ||
            RestoredConfigured && (restoredConfiguredPhysics.Flags & PhysicsLiquid) != 0;
        private static bool RestoredConfigured => restoredScenarioSelected && restoredScenario == ConfiguredScenario;
        private static ConfiguredDomainLayout? configuredRoom;
        private static ConfiguredDomainLayout ConfiguredRoom => configuredRoom ??= ConfiguredDomainLayout.PillaredRoom();
        private static bool IsDomainSolid(int index) => ConfiguredMode && ConfiguredRoom.IsSolid(index);
        private static bool IsDomainSolidAt(int dx, int dy) => ConfiguredMode && ConfiguredRoom.IsSolid(dx, dy);
        private static int FirstDomainFluid => ConfiguredMode ? ConfiguredRoom.FirstFluid : 0;
        // Cells the custom owner holds: every fluid cell of a configured room.
        private static int OwnedCellCount => ConfiguredMode ? ConfiguredRoom.FluidCount : cells.Length;
        private static Physics ConfiguredPhysics => RestoredConfigured ? restoredConfiguredPhysics : ConfiguredDomainLayout.ScenePhysics(ConfiguredLiquidMode);
        private static Physics restoredConfiguredPhysics;
        private static bool configuredRestoredGridVerified;
        private static byte[] configuredRestoredMask = Array.Empty<byte>();
        private static object[] configuredRestoredSolidEvidence = Array.Empty<object>();
        private static int configuredSavedFormatVersion;
        private static float ConfiguredProjectionMass(int column) => ConfiguredDomainLayout.ProjectionMass(ConfiguredLiquidMode, column);
        private static float ConfiguredProjectionTemperature(int column) => ConfiguredDomainLayout.ProjectionTemperature(column);

        private static bool configuredInventoryVerified, configuredConfigurationVerified, configuredSolidsVerified;
        private static bool configuredThermalVerified, configuredGravityVerified, configuredCopiesVerified, configuredMotionObserved;
        private static string configuredInventoryHash = "";
        private static InitialCell[] configuredInventory = Array.Empty<InitialCell>();
        private static ushort[] configuredInventoryElements = Array.Empty<ushort>();
        private static object[] configuredSolidEvidence = Array.Empty<object>();
        private static Configuration configuredGeometry, configuredCopiedGeometry;
        private static Physics configuredCopiedPhysics;
        private static byte[] configuredCopiedMask = Array.Empty<byte>();

        private static void RequireConfiguredCapability()
        {
            Require(ConfiguredVersion() == ConfiguredAbiVersion && Marshal.SizeOf<Physics>() == 40 &&
                Marshal.OffsetOf<Physics>(nameof(Physics.GravityMetresPerSecondSquared)).ToInt32() == 16 &&
                InitialInventoryVersion() == 1 && Marshal.SizeOf<InitialCell>() == 56, "Unsupported native configured-domain ABI.");
            var room = ConfiguredRoom;
            Require(room.Width > 3 && room.Height > 2 && room.SolidCount > 0 && room.SolidIndices.All(index => room.FluidFaces(index) > 0),
                "The configured room must be larger than 3x2 and contain interior solids with fluid faces.");
        }

        // Every solid inside the rectangle is verified exactly as the border:
        // Unobtanium with zero conductivity and heat capacity, no unstable
        // tag, its own high-temperature target, far below that threshold.
        private static bool InsulatingStaticSolid(int cell)
        {
            var element = Grid.Element[cell];
            return element != null && element.IsSolid && element.id == SimHashes.Unobtanium && element.thermalConductivity == 0 &&
                element.specificHeatCapacity == 0 && !element.IsUnstable &&
                (element.highTempTransition == null || element.highTempTransition.id == element.id) &&
                Grid.Temperature[cell] < element.highTemp && Grid.Mass[cell] > 0;
        }

        // A loaded configured save: only the rooms this adapter itself builds
        // have a compatibility and validation adapter, so the saved wrapper must
        // name the pillared room with the gas or water scene physics exactly.
        private static void AdmitSavedConfiguredRoom(NativeDomainCheckpointPayload payload)
        {
            var saved = payload.Configured ?? throw new InvalidDataException("A configured colony save has no configuration wrapper.");
            RequireConfiguredCapability();
            if (!ConfiguredDomainLayout.IsSavedScene(saved))
                throw new InvalidDataException("Saved configured room has no managed restore adapter; only the pillared 7x5 gas or water scene is admitted.");
        }

        // Native admission of a saved image against its own saved configuration.
        private static void ValidateSavedCheckpoint(NativeDomainCheckpointPayload payload, byte[] physical)
        {
            var config = payload.Geometry;
            if (payload.Configured == null) { Check(ValidateCheckpoint(ref config, physical, (uint)physical.Length, payload.PhysicalTime)); return; }
            var physics = payload.Configured.Physics; var mask = payload.Configured.Mask;
            Check(ValidateConfiguredCheckpoint(ref config, ref physics, mask, (uint)mask.Length, physical, (uint)physical.Length, payload.PhysicalTime));
        }

        // A paired save of a configured session must carry exactly the live
        // session's geometry, physics and mask, read back from the native owner.
        private static void RequireLiveConfiguredSave(NativeDomainCheckpointPayload payload)
        {
            Require(ConfiguredMode == (payload.Configured != null), "Paired save configuration differs from the live domain kind.");
            if (payload.Configured == null) return;
            VerifyConfigurationCopy(latest.Session, configuredGeometry, ConfiguredPhysics);
            Require(NativeConfiguredDomainImage.SameGeometry(payload.Configured.Geometry, configuredGeometry) &&
                NativeConfiguredDomainImage.SamePhysics(payload.Configured.Physics, ConfiguredPhysics) && ConfiguredRoom.SameMask(payload.Configured.Mask),
                "Paired configured save differs from the live session's geometry, physics or solid mask.");
        }

        // Before restored activation. The saved owner is selected only from the
        // admitted payload; the loaded grid must then reproduce it.
        private static void SelectRestoredConfiguredRoom(NativeDomainCheckpointPayload payload)
        {
            var saved = payload.Configured!;
            configuredRoom = new ConfiguredDomainLayout((int)saved.Geometry.Width, (int)saved.Geometry.Height, saved.Mask);
            restoredConfiguredPhysics = saved.Physics; configuredGeometry = payload.Geometry; configuredSavedFormatVersion = payload.FormatVersion;
        }

        // The actual loaded game grid (Sim.Start has set its native pointers and
        // element table) must reproduce the saved solid mask exactly, and every
        // interior solid must again be native, static, insulating Unobtanium
        // with cleared metadata, as at activation, before restore_configured.
        private static void VerifyRestoredConfiguredGrid()
        {
            var room = ConfiguredRoom; var loaded = new byte[room.Count];
            for (var index = 0; index < room.Count; ++index)
            {
                var element = Grid.Element[Grid.XYToCell(x + index % Width, y + index / Width)];
                loaded[index] = element != null && element.IsSolid ? (byte)1 : (byte)0;
            }
            configuredRestoredMask = loaded;
            Require(room.SameMask(loaded), "The loaded game grid's solids differ from the saved configured solid mask.");
            foreach (var index in room.SolidIndices)
            {
                var global = Grid.XYToCell(x + index % Width, y + index / Width);
                Require(Grid.Element[global].id == SimHashes.Unobtanium && InsulatingStaticSolid(global) && Grid.Solid[global] &&
                    Grid.DiseaseCount[global] == 0 && Grid.Radiation[global] == 0,
                    "A loaded configured interior solid is not native, static, insulating Unobtanium at cell " + global + ".");
            }
            configuredRestoredSolidEvidence = room.SolidIndices.Select(SolidEvidence).ToArray();
            Require(BoundaryReady(false), "The loaded configured boundary or interior solids differ from the admitted room.");
            configuredRestoredGridVerified = true;
        }

        private static object SolidEvidence(int index)
        {
            var global = Grid.XYToCell(x + index % Width, y + index / Width); var element = Grid.Element[global];
            var occupied = Enumerable.Range(0, (int)ObjectLayer.NumLayers).Any(layer => Grid.Objects[global, layer] != null);
            return new
            {
                Index = index, PublicCell = global, DomainX = index % Width, DomainY = index / Width,
                Element = element.id.ToString(), Solid = Grid.Solid[global], MassKg = Grid.Mass[global], TemperatureK = Grid.Temperature[global],
                element.thermalConductivity, element.specificHeatCapacity, element.highTemp,
                HighTempTransition = element.highTempTransition?.id.ToString(), element.IsUnstable,
                FluidFaces = ConfiguredRoom.FluidFaces(index), Occupied = occupied,
                Insulating = InsulatingStaticSolid(global) && !occupied
            };
        }

        private static void ImportConfiguredInventory(Sim.GameDataUpdate* source, ref Configuration configuration)
        {
            var nativeFrames = source->numFramesProcessed;
            Require(nativeFrames > 0 && !nativeFlowFenceVerified, "Configured inventory requires a completed native frame before the flow fence.");
            var room = ConfiguredRoom;
            Require(room.Count == cells.Length && configuration.Width == room.Width && configuration.Height == room.Height,
                "Configured geometry differs from its layout.");
            var unobtanium = ElementLoader.FindElementByHash(SimHashes.Unobtanium).idx;
            var elements = new ushort[room.Count];
            for (var index = 0; index < room.Count; ++index)
            {
                var global = Grid.XYToCell(x + index % Width, y + index / Width);
                elements[index] = source->elementIdx[global];
                if (room.IsSolid(index))
                    Require(elements[index] == unobtanium && InsulatingStaticSolid(global) && source->diseaseCount[global] == 0,
                        "A configured interior solid is not native, static, insulating Unobtanium at cell " + global + ".");
            }
            var inventory = room.Inventory(index =>
            {
                var global = Grid.XYToCell(x + index % Width, y + index / Width);
                var mass = source->mass[global]; var temperature = source->temperature[global];
                Require(source->diseaseCount[global] == 0 && Grid.Radiation[global] == 0,
                    "Configured inventory has unsupported disease or radiation metadata at cell " + global + ".");
                Require(!float.IsNaN(mass) && !float.IsInfinity(mass) && mass > 0 && !float.IsNaN(temperature) && !float.IsInfinity(temperature) && temperature > 0,
                    $"Configured inventory has invalid mass or temperature at cell {global}: element={elements[index]}, mass={mass:R}, temperature={temperature:R}.");
                var species = InitialInventorySpecies(elements[index]);
                return new InitialCell { Size = 56, Version = 1, Temperature = temperature,
                    Nitrogen = species == 0 ? mass : 0, Oxygen = species == 1 ? mass : 0, Helium = species == 2 ? mass : 0,
                    Water = species == 3 ? mass : 0, CarbonDioxide = species == 4 ? mass : 0 };
            });
            var physics = ConfiguredPhysics; var mask = room.Mask;
            // The complete immutable packet is admitted before any custom
            // activation; native admission validates every record again.
            Check(BeginConfigured(ref configuration, ref physics, mask, (uint)mask.Length, inventory, (uint)inventory.Length, ref latest));
            active = true;
            var admitted = new Cell[room.Count]; var status = Status.Empty;
            Check(Copy(ref status, admitted, (uint)admitted.Length));
            var first = room.FirstFluid;
            configuredInventoryVerified = status.CompletedFrames == 0 && status.TimeSeconds == 0 && status.Cells == room.Count &&
                room.CopyMatchesMask(admitted) && status.InitialOxygenKg == inventory[first].Oxygen &&
                room.FluidIndices.All(index =>
                    Enumerable.Range(0, Ids.Length).All(species => admitted[index].Mass(species) == inventory[index].Mass(species)) &&
                    Math.Abs(admitted[index].Temperature - inventory[index].Temperature) <= 64 * 2.2204460492503131e-16 * inventory[index].Temperature &&
                    admitted[index].ResolvedKineticEnergy == 0 && admitted[index].MomentumX == 0 && admitted[index].MomentumY == 0);
            Require(configuredInventoryVerified, "Configured admission changed constituents/temperature, invented motion or published a solid as fluid.");
            configuredGeometry = configuration;
            VerifyConfigurationCopy(status.Session, configuration, physics);
            configuredInventory = inventory; configuredInventoryElements = elements;
            using var bytes = new MemoryStream();
            using (var writer = new BinaryWriter(bytes, System.Text.Encoding.UTF8, true))
            {
                writer.Write(mask);
                foreach (var cell in inventory)
                {
                    writer.Write(cell.Size); writer.Write(cell.Version);
                    for (var species = 0; species < Ids.Length; ++species) writer.Write(cell.Mass(species));
                    writer.Write(cell.Temperature);
                }
            }
            configuredInventoryHash = CheckpointHash(bytes.ToArray());
        }

        // The active session's exact geometry, physics bits and mask.
        private static void VerifyConfigurationCopy(ulong session, Configuration expected, Physics physics)
        {
            var geometry = new Configuration(); var copied = new Physics();
            Check(CopyConfiguration(session, ref geometry, ref copied, null, 0, out var count));
            Require(count == ConfiguredRoom.Count, "Configured mask count query differs from the admitted room.");
            var mask = new byte[count];
            Check(CopyConfiguration(session, ref geometry, ref copied, mask, count, out var written));
            configuredCopiedGeometry = geometry; configuredCopiedPhysics = copied; configuredCopiedMask = mask;
            configuredConfigurationVerified = written == count && geometry.Equals(expected) && copied.Size == physics.Size &&
                copied.Version == physics.Version && copied.Flags == physics.Flags && copied.Reserved == 0 &&
                BitConverter.DoubleToInt64Bits(copied.GravityMetresPerSecondSquared) == BitConverter.DoubleToInt64Bits(physics.GravityMetresPerSecondSquared) &&
                BitConverter.DoubleToInt64Bits(copied.ShearViscosityPaSeconds) == BitConverter.DoubleToInt64Bits(physics.ShearViscosityPaSeconds) &&
                BitConverter.DoubleToInt64Bits(copied.BulkViscosityPaSeconds) == BitConverter.DoubleToInt64Bits(physics.BulkViscosityPaSeconds) &&
                ConfiguredRoom.SameMask(mask);
            Require(configuredConfigurationVerified, "Configured session copy differs from its admitted geometry, physics or mask.");
        }

        private static bool ConfiguredPhysicsExercised => configuredInventoryVerified && latest.PressurePa > 0 &&
            ConfiguredRoom.FluidIndices.Any(index => cells[index].Temperature != configuredInventory[index].Temperature ||
                Enumerable.Range(0, Ids.Length).Any(species => cells[index].Mass(species) != configuredInventory[index].Mass(species)));

        private static void VerifyConfiguredCompletion()
        {
            var room = ConfiguredRoom;
            VerifyConfigurationCopy(latest.Session, configuredGeometry, ConfiguredPhysics);
            configuredCopiesVerified = latest.Cells == room.Count && room.CopyMatchesMask(cells) &&
                room.FluidIndices.All(index => cells[index].Pressure > 0 && publishedReadings!.ContainsKey(Grid.XYToCell(x + index % Width, y + index / Width))) &&
                room.SolidIndices.All(index => !publishedReadings!.ContainsKey(Grid.XYToCell(x + index % Width, y + index / Width)));
            Require(configuredCopiesVerified, "Configured copies or readings treated a solid as fluid.");
            configuredSolidEvidence = room.SolidIndices.Select(SolidEvidence).ToArray();
            configuredSolidsVerified = room.SolidIndices.All(index =>
            {
                var global = Grid.XYToCell(x + index % Width, y + index / Width);
                return InsulatingStaticSolid(global) && Grid.Solid[global] && !OwnsPublicCell(global) &&
                    Enumerable.Range(0, (int)ObjectLayer.NumLayers).All(layer => Grid.Objects[global, layer] == null) &&
                    nativeBaseline.TryGetValue(global, out var held) && held.element == ElementLoader.FindElementByHash(SimHashes.Unobtanium).idx;
            });
            Require(configuredSolidsVerified, "A configured interior solid changed, became owned or stopped being static insulating Unobtanium.");
            configuredThermalVerified = thermalStatus.Enabled == 1 && thermalStatus.State == 1 && thermalStatus.Session == latest.Session &&
                thermalStatus.CompletedFrame == latest.CompletedFrames && thermalStatus.ExactEquilibriumIntervals <= latest.Substeps &&
                thermalStatus.Stages == 2 * (latest.Substeps - thermalStatus.ExactEquilibriumIntervals) && thermalStatus.Stages > 0 &&
                thermalStatus.Contacts > 0 && thermalStatus.Iterations > 0 && thermalStatus.RedistributedEnergy > 0 && thermalStatus.EntropyChange > 0;
            Require(configuredThermalVerified, "Configured conduction did not redistribute heat around the interior solids.");
            configuredGravityVerified = gravityStatus.Enabled == 1 && gravityStatus.State == 1 && gravityStatus.Gravity == 9.81 &&
                gravityStatus.Session == latest.Session && gravityStatus.CompletedFrame == latest.CompletedFrames &&
                gravityStatus.BodyImpulseY < 0 && gravityStatus.Potential != gravityStatus.InitialPotential &&
                gravityStatus.PendingBoundaryPotential == 0 && gravityStatus.CommittedBoundaryPotential == 0;
            Require(configuredGravityVerified, "Configured gravity did not act on the owned fluid cells.");
            configuredMotionObserved = room.FluidIndices.Any(index => cells[index].ResolvedKineticEnergy > 0 || cells[index].MomentumY != 0);
            Require(configuredMotionObserved, "Configured walls suppressed every fluid motion.");
            Require(viscosityStatus.Enabled == 0 && viscosityStatus.Stages == 0, "Disabled configured viscosity reported work.");
        }

        private static object? ConfiguredReport()
        {
            if (!ConfiguredMode) return null;
            var room = ConfiguredRoom; var physics = ConfiguredPhysics;
            return new
            {
                Required = true, AbiVersion = ConfiguredAbiVersion, NativeScenario = ConfiguredScenario,
                Restored = RestoredConfigured, SavedPayloadFormatVersion = configuredSavedFormatVersion,
                RestoredGridVerified = configuredRestoredGridVerified,
                RestoredMaskFromLoadedGrid = configuredRestoredMask.Select(value => (int)value).ToArray(),
                RestoredInteriorSolids = configuredRestoredSolidEvidence,
                Variant = ConfiguredLiquidMode ? "water" : "gas", Width = room.Width, Height = room.Height,
                Mask = room.Mask.Select(value => (int)value).ToArray(), FluidCells = room.FluidCount, SolidCells = room.SolidCount,
                FirstFluidCell = room.FirstFluid,
                Physics = new { physics.Size, physics.Version, physics.Flags, physics.Reserved, physics.GravityMetresPerSecondSquared,
                    physics.ShearViscosityPaSeconds, physics.BulkViscosityPaSeconds },
                CopiedConfiguration = configuredCopiedGeometry, CopiedPhysics = new { configuredCopiedPhysics.Size, configuredCopiedPhysics.Version,
                    configuredCopiedPhysics.Flags, configuredCopiedPhysics.Reserved, configuredCopiedPhysics.GravityMetresPerSecondSquared,
                    configuredCopiedPhysics.ShearViscosityPaSeconds, configuredCopiedPhysics.BulkViscosityPaSeconds },
                CopiedMask = configuredCopiedMask.Select(value => (int)value).ToArray(),
                InventoryVerified = configuredInventoryVerified, ConfigurationCopyVerified = configuredConfigurationVerified,
                CopiesAndReadingsVerified = configuredCopiesVerified, InteriorSolidsVerified = configuredSolidsVerified,
                ThermalVerified = configuredThermalVerified, GravityVerified = configuredGravityVerified, MotionObserved = configuredMotionObserved,
                InventorySha256 = configuredInventoryHash, InventorySource = "completed-native-GameDataUpdate-before-flow-fence",
                Inventory = configuredInventory.Select((cell, index) => new { Index = index, Solid = room.IsSolid(index),
                    NativeElementIndex = configuredInventoryElements.Length > index ? configuredInventoryElements[index] : 0,
                    MassesKg = Enumerable.Range(0, Ids.Length).Select(cell.Mass).ToArray(), TemperatureK = cell.Temperature }).ToArray(),
                InteriorSolids = configuredSolidEvidence,
                InterfaceModel = "impermeable-adiabatic-interior-faces", SolidsAreNativeData = true,
                CompatibilityProjectionInSolids = false
            };
        }
    }
}
