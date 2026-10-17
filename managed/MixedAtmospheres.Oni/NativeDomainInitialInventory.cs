using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool InitialInventoryMode => ColonyRuntimeValidation.Settings?.Mode == "native-import-domain" || LiquidInitialInventoryMode;
        private static bool LiquidInitialInventoryMode => ColonyRuntimeValidation.Settings?.Mode == "native-liquid-import-domain";
        private static InitialCell[] initialInventory = Array.Empty<InitialCell>();
        private static ushort[] initialInventoryElements = Array.Empty<ushort>();
        private static bool initialInventoryVerified;
        private static string initialInventoryHash = "";
        private static int initialInventoryNativeFrames;
        private static bool InitialInventoryAdvanced => initialInventoryVerified && latest.CompletedFrames > 0 &&
            Enumerable.Range(0, cells.Length).Any(index => cells[index].Temperature != initialInventory[index].Temperature ||
                Enumerable.Range(0, Ids.Length).Any(species => cells[index].Mass(species) != initialInventory[index].Mass(species)));
        private static int InitialInventorySpecies(ushort element)
        {
            for (var species = 0; species < Ids.Length; ++species)
                if ((species == 3) == Liquid && ElementLoader.FindElementByHash(SpeciesHash((uint)species)).idx == element)
                    return species;
            throw new NotSupportedException("Native initial inventory requires a supported, single-phase fluid material.");
        }
        private static void ImportNativeInventory(Sim.GameDataUpdate* source, ref Configuration configuration)
        {
            Require(InitialInventoryVersion() == 1 && Marshal.SizeOf<InitialCell>() == 56,
                "Unsupported native initial-inventory ABI.");
            Require(source->numFramesProcessed > 0, "Native initial inventory requires a completed native frame.");
            var sourceRows = new object[cells.Length];
            for (var index = 0; index < sourceRows.Length; ++index)
            {
                var global = Grid.XYToCell(x + index % Width, y + index / Width);
                sourceRows[index] = new { PublicCell = global, ElementIndex = source->elementIdx[global],
                    MassKg = source->mass[global], TemperatureK = source->temperature[global],
                    DiseaseCount = source->diseaseCount[global], Properties = source->properties[global] };
            }
            File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "native-initial-inventory-source.json"),
                ReportJson.Serialize(new { NativeFrames = source->numFramesProcessed, Cells = sourceRows }));
            var inventory = new InitialCell[cells.Length];
            var elements = new ushort[cells.Length];
            for (var index = 0; index < inventory.Length; ++index)
            {
                var global = Grid.XYToCell(x + index % Width, y + index / Width);
                Require(source->diseaseCount[global] == 0 && Grid.Radiation[global] == 0,
                    "Native initial inventory has unsupported disease or radiation metadata.");
                var mass = source->mass[global]; var temperature = source->temperature[global];
                Require(!float.IsNaN(mass) && !float.IsInfinity(mass) && mass > 0 &&
                    !float.IsNaN(temperature) && !float.IsInfinity(temperature) && temperature > 0,
                    $"Native initial inventory has invalid mass or temperature at cell {global}: element={source->elementIdx[global]}, mass={mass:R}, temperature={temperature:R}, completed native frames={source->numFramesProcessed}.");
                elements[index] = source->elementIdx[global];
                var species = InitialInventorySpecies(elements[index]);
                inventory[index] = new InitialCell { Size = 56, Version = 1, Temperature = temperature,
                    Nitrogen = species == 0 ? mass : 0, Oxygen = species == 1 ? mass : 0,
                    Helium = species == 2 ? mass : 0, Water = species == 3 ? mass : 0,
                    CarbonDioxide = species == 4 ? mass : 0 };
            }
            // The complete immutable packet is admitted before any custom
            // activation. No scalar ReplaceElement operation resets its source.
            Check(BeginFromCells(ref configuration, inventory, (uint)inventory.Length, ref latest));
            // A failed verification after native admission must stop the new
            // owner while retaining its exclusions, just like any later fault.
            active = true;
            var admitted = new Cell[cells.Length]; var status = Status.Empty;
            Check(Copy(ref status, admitted, (uint)admitted.Length));
            initialInventoryVerified = status.CompletedFrames == 0 && status.TimeSeconds == 0 &&
                Enumerable.Range(0, inventory.Length).All(index =>
                    Enumerable.Range(0, Ids.Length).All(species => admitted[index].Mass(species) == inventory[index].Mass(species)) &&
                    Math.Abs(admitted[index].Temperature - inventory[index].Temperature) <= 64 * 2.2204460492503131e-16 * inventory[index].Temperature &&
                    admitted[index].ResolvedKineticEnergy == 0 && admitted[index].MomentumX == 0 && admitted[index].MomentumY == 0);
            Require(initialInventoryVerified, "Native initial inventory changed constituents/temperature or invented motion before its first frame.");
            initialInventory = inventory; initialInventoryElements = elements; initialInventoryNativeFrames = source->numFramesProcessed;
            using var bytes = new MemoryStream();
            using (var writer = new BinaryWriter(bytes, System.Text.Encoding.UTF8, true))
                foreach (var cell in inventory)
                {
                    writer.Write(cell.Size); writer.Write(cell.Version);
                    for (var species = 0; species < Ids.Length; ++species) writer.Write(cell.Mass(species));
                    writer.Write(cell.Temperature);
                }
            initialInventoryHash = CheckpointHash(bytes.ToArray());
        }
        private static float ExpectedNativeMass(int index) => InitialInventoryMode
            ? (float)Enumerable.Range(0, Ids.Length).Sum(species => initialInventory[index].Mass(species)) : ProjectionMass(index % Width);
        private static float ExpectedNativeTemperature(int index) => InitialInventoryMode
            ? (float)initialInventory[index].Temperature : ProjectionTemperature(index % Width);
        private static ushort ExpectedNativeElement(int index) => InitialInventoryMode
            ? initialInventoryElements[index] : ElementLoader.FindElementByHash(Liquid ? SimHashes.Water : SimHashes.Oxygen).idx;
        private static object InitialInventoryReport() => new {
            Required = InitialInventoryMode, Verified = initialInventoryVerified, AdvancedFromSource = InitialInventoryAdvanced, AbiVersion = 1,
            NativeFramesInSource = initialInventoryNativeFrames,
            Source = "completed-native-GameDataUpdate", EnergyBasis = "declared-reference-EOS-at-native-temperature",
            InitialMomentum = "at-rest", SourceSha256 = initialInventoryHash,
            Cells = initialInventory.Select((cell, index) => new { PublicCell = Grid.XYToCell(x + index % Width, y + index / Width),
                NativeElementIndex = initialInventoryElements[index], MassesKg = Enumerable.Range(0, Ids.Length).Select(cell.Mass).ToArray(),
                TemperatureK = cell.Temperature }).ToArray() };
    }
}
