using System.Buffers.Binary;
using System.Runtime.InteropServices;

internal static unsafe partial class Program
{
    private static void VerifyColonyTransitionHooks(NativeSim sim, Contract contract, Dictionary<string, object?> report)
    {
        void Replace(int cell, int element, float mass, float temperature) => sim.Send(-1252920804, WritePayload(w =>
        {
            w.Write(cell); w.Write(-1); w.Write(temperature); w.Write(mass); w.Write(0);
            w.Write((ushort)element); w.Write((byte)1); w.Write((byte)255); w.Write((byte)1); w.Write(new byte[3]);
        }));
        // Include a supported solid beside the owned mixed cell. It becomes
        // fluid while unowned; capture must open it and adopt the new phase.
        for (var i = 0; i < 6; ++i) Replace(DomainCell(i), i == 4 ? contract.Oxygen : contract.Unobtanium, i == 4 ? 1 : 20000, 300);
        sim.Send(-775326397, Frame(0)); sim.PrepareAfterFrame(contract);
        ColonyHooks.Material Material(int index, string id, double molar, double cp) => new()
        {
            Size = 168, Version = 1, Hash = BinaryPrimitives.ReadInt32LittleEndian(contract.Elements.AsSpan(4 + index * 164)),
            Index = (uint)index, Phase = 1, Id = id, MolarMass = molar, SpecificHeat = cp,
            LowTemperature = 1, HighTemperature = 10000
        };
        var liquid = Material(contract.LiquidCarbonDioxide, "LiquidCarbonDioxide", .0440095, 850);
        liquid.Phase = 2; liquid.Density = 1000;
        var materials = new[] { Material(contract.Oxygen, "Oxygen", .0319988, 1005), Material(contract.CarbonDioxide, "CarbonDioxide", .0440095, 850), liquid };
        var inventory = new ColonyHooks.Cell[9];
        inventory[4] = new() { Kind = 1, Index = (uint)contract.Oxygen, Mass = 1, Temperature = 300 };
        var config = new ColonyHooks.Config { Size = 56, Version = 1, Flags = 12, Vacuum = (uint)contract.Vacuum,
            GridWidth = Width, GridHeight = Height, X = (uint)DomainX, Y = DomainY, Width = 3, Height = 3, MaximumTimestep = .1 };
        var status = ColonyHooks.Status.Empty;
        ColonyHooks.Check(ColonyHooks.Start(ref config, materials, 3, inventory, 9, ref status));
        try
        {
            var transfer = new ColonyHooks.Transfer { Cell = (uint)DomainCell(4), Index = (uint)contract.CarbonDioxide,
                Kind = 1, Callback = -1, Mass = .2, Temperature = 300 };
            ColonyHooks.Check(ColonyHooks.Transfers([transfer], 1));
            sim.Send(-775326397, Frame(.2f)); sim.PrepareAfterFrame(contract); ColonyHooks.Check(ColonyHooks.Wait());
            var before = new ColonyHooks.View[9]; ColonyHooks.Check(ColonyHooks.Copy(ref status, before, 9));
            var ownedTemperatureBefore = before[4].Temperature;
            // Retain the separately identified native falling-solid limitation as
            // an explicit diagnostic; ordinary coverage uses a supported solid.
            var falling = Environment.GetEnvironmentVariable("MA_COLONY_TRANSITION_FALLING_CONTROL") == "1";
            var solidCell = falling ? DomainX + 1 + (DomainY + 2) * Width : DomainX + (DomainY + 1) * Width;
            Replace(solidCell, contract.SolidCarbonDioxide, 4, 300);
            sim.Send(-775326397, Frame(.2f)); var snapshot = sim.PrepareAfterFrame(contract);
            var owned = snapshot.CellData[DomainCell(4)];
            Require((owned.Properties & 3) == 0 && owned.Element == before[4].Index && owned.Mass == before[4].Mass && owned.Temperature == before[4].Temperature &&
                owned.Disease == 255 && owned.DiseaseCount == 0,
                $"Native transition/postprocess changed an owned mass/identity: {before[4].Index}/{before[4].Mass:R} -> {owned}.");
            var produced = snapshot.CellData[solidCell];
            Require((produced.Element == contract.CarbonDioxide || produced.Element == contract.LiquidCarbonDioxide) && produced.Mass == 4,
                "Native solid-to-fluid transition did not produce the expected four kg of CO2.");
            ColonyHooks.Check(ColonyHooks.Wait()); ColonyHooks.Check(ColonyHooks.Copy(ref status, before, 9));
            double oxygen = 0, carbon = 0;
            for (var i = 0; i < 9; ++i)
            {
                var indices = new uint[3]; var masses = new double[3];
                ColonyHooks.Check(ColonyHooks.Composition((uint)(DomainX + i % 3 + (DomainY + i / 3) * Width), indices, masses, 3, out var count));
                for (var k = 0; k < count; ++k)
                {
                    if (indices[k] == contract.Oxygen) oxygen += masses[k];
                    else if ((indices[k] == contract.CarbonDioxide || indices[k] == contract.LiquidCarbonDioxide)) carbon += masses[k];
                    else throw new InvalidDataException("Unexpected species in the transition owner.");
                }
            }
            Require(status.State == 1 && Math.Abs(oxygen - 1) < 1e-12 && Math.Abs(carbon - 4.2) < 1e-12 &&
                status.NativeMassIn == 4 && status.NativeMassOut == 0 && status.NativeHeat == 0,
                $"Native transition corrupted owned composition: O2={oxygen:R}, CO2={carbon:R}, state={status.State}, nativeIn={status.NativeMassIn:R}, nativeOut={status.NativeMassOut:R}.");
            report["colonyTransitionOwnership"] = new { oxygen, carbon, expectedOxygen = 1, expectedCarbon = 4.2,
                status.NativeMassIn, status.NativeMassOut, status.NativeHeat, status.State,
                exactOwnedProjections = 1, nativeFluidElement = produced.Element, ownedTemperatureBefore, ownedTemperatureAfter = owned.Temperature, permeabilityMask = 0, nativeSolidToFluid = true, ownerAdoptedNativeFluid = true };
        }
        finally { ColonyHooks.Check(ColonyHooks.Stop(ref status)); }
    }
}
