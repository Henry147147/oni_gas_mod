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
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool ConfiguredMode => ColonyRuntimeValidation.Settings?.Mode == "native-configured-domain" || ConfiguredLiquidMode;
        private static bool ConfiguredLiquidMode => ColonyRuntimeValidation.Settings?.Mode == "native-configured-water-domain";
        private static ConfiguredDomainLayout? configuredRoom;
        private static ConfiguredDomainLayout ConfiguredRoom => configuredRoom ??= ConfiguredDomainLayout.PillaredRoom();
        private static bool IsDomainSolid(int index) => ConfiguredMode && ConfiguredRoom.IsSolid(index);
        private static bool IsDomainSolidAt(int dx, int dy) => ConfiguredMode && ConfiguredRoom.IsSolid(dx, dy);
        private static int FirstDomainFluid => ConfiguredMode ? ConfiguredRoom.FirstFluid : 0;
        private static Physics ConfiguredPhysics => Physics.Create(PhysicsGravity | PhysicsConduction | (ConfiguredLiquidMode ? PhysicsLiquid : 0U), 9.81);
        private static float ConfiguredProjectionMass(int column) => ConfiguredLiquidMode ? 1000 + 20 * column : .6f + .3f * column;
        private static float ConfiguredProjectionTemperature(int column) => 280 + 5 * column;

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
                thermalStatus.CompletedFrame == latest.CompletedFrames && thermalStatus.Stages == 2 * latest.Substeps &&
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
