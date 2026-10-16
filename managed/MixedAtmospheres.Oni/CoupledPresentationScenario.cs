using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Oni
{
    // Original synthetic import, shared with the standalone control host.
    // It is never substituted for a generated colony or written to native ONI.
    internal static class CoupledPresentationScenario
    {
        internal static NativeColonyImport Import(int width, int height, int x, int y, int world)
        {
            const int rawWidth = 5, rawHeight = 4, count = rawWidth * rawHeight;
            var raw = new byte[29 + 36 * count]; new byte[] { 83, 73, 77, 83, 65, 86, 69, 0 }.CopyTo(raw, 0);
            void Int(int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(offset, 4), value);
            void Float(int offset, float value) => Int(offset, BitConverter.SingleToInt32Bits(value));
            Int(8, 15); Int(12, rawWidth); Int(16, rawHeight); Int(20, x); Int(24, y); raw[28] = 1;
            for (var i = 0; i < count; ++i)
            { Int(29 + i * 16, 4); Float(29 + i * 16 + 4, 300); Float(29 + i * 16 + 8, 1000); Int(29 + 24 * count + i * 12, 1); }
            var ids = new[] { "nitrogen", "oxygen", "carbon_dioxide", "steam", "helium", "chlorine" };
            var catalog = CoupledColonySimulation.ReadMaterialCatalog();
            var phases = new Dictionary<int, NativeMatterPhase> { [1] = NativeMatterPhase.Vacuum, [4] = NativeMatterPhase.Solid };
            var bindings = new Dictionary<int, NativeFluidBinding>();
            var n = 100000 / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300);
            for (var c = 0; c < ids.Length; ++c)
            {
                var hash = 10 + c; phases.Add(hash, NativeMatterPhase.Gas); bindings.Add(hash, new NativeFluidBinding(ids[c], NativeMatterPhase.Gas));
                var i = 1 + c % 3 + (1 + c / 3) * rawWidth; Int(29 + i * 16, hash); Float(29 + i * 16 + 4, 300);
                Float(29 + i * 16 + 8, (float)(n * catalog.Materials[ids[c]].MolarMassKgPerMol * (c == 4 ? 1e-6 : c == 5 ? 2e-6 : 1)));
            }
            var payload = new byte[raw.Length + 4]; BinaryPrimitives.WriteInt32LittleEndian(payload, raw.Length); raw.CopyTo(payload, 4);
            return NativeColonyImport.StageNewColony(width, height, new[] { new NativeWorldPlacement(world, 3, 2, x, y) },
                new[] { new NativeWorldSource(world, payload) }, phases, bindings, 1);
        }
        internal static CoupledWorldConfiguration Configuration(int width, int height) => new CoupledWorldConfiguration
        {
            Width = checked((uint)width), Height = checked((uint)height), Gravity = 0, Diffusivity = 10,
            Viscosity = 0, ThermalConduction = false, MaximumTimestepSeconds = .001
        };
        // Explicit synthetic external inlet/heat accounts for this zero-gravity
        // validator only. This does not represent a powered ONI machine.
        internal static CoupledWorldMutation InventoryAddition(CoupledColonyFrame source, int globalCell)
        {
            var sample = source.SampleCell(globalCell); var old = sample.PhysicalState;
            var placement = source.Projection.Worlds.Single(value => value.WorldId == sample.WorldId);
            var local = globalCell % source.Projection.Width - placement.OffsetX +
                (globalCell / source.Projection.Width - placement.OffsetY) * placement.Width;
            const double amount = .125, heat = 4096;
            var oxygen = source.Materials.Single(value => value.Id == "oxygen");
            var parcelU = amount * (oxygen.EnergyOffsetJoulesPerKg + oxygen.SpecificHeatCv * old.TemperatureKelvin);
            var masses = old.MassesKg.ToDictionary(entry => entry.Key, entry => entry.Value, StringComparer.Ordinal);
            masses.TryGetValue("oxygen", out var present); masses["oxygen"] = present + amount;
            var momentumSquared = old.MomentumX * old.MomentumX + old.MomentumY * old.MomentumY;
            var mixingHeat = momentumSquared / (2 * old.TotalMassKg) - momentumSquared / (2 * masses.Values.Sum());
            var value = new CoupledCellInitialization(masses, old.InternalEnergyJoules + parcelU + mixingHeat + heat,
                old.MomentumX, old.MomentumY, old.PressurePa).WithPassiveMetadata(old.DiseaseCounts, old.RadiationQuantity);
            return new CoupledWorldMutation(sample.WorldId, new[] { new CoupledCellEdit((ulong)local, value) },
                new CoupledMutationBudget(new Dictionary<string, double> { ["oxygen"] = amount }, materialEnergyJoules: parcelU, heatJoules: heat));
        }
    }
}
