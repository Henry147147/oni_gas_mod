using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

internal static class ColonyStateTests
{
    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<byte[]> nativeFixture, Dictionary<int, NativeMatterPhase> phases)
    {
        const int width = 32;
        const int gas = 11 + 7 * width, water = 12 + 7 * width, empty = 11 + 8 * width, solid = 12 + 8 * width;
        const int otherGas = 20 + 7 * width;
        var bindings = new Dictionary<int, NativeFluidBinding>
        {
            [2] = new NativeFluidBinding("oxygen", NativeMatterPhase.Gas),
            [3] = new NativeFluidBinding("water", NativeMatterPhase.Liquid)
        };
        var placements = new[] { new NativeWorldPlacement(7, 2, 2, 11, 7), new NativeWorldPlacement(9, 2, 2, 20, 7) };
        FluidMaterialCatalog Catalog(bool reversed = false, double oxygenCv = 658) => new FluidMaterialCatalog("test-properties-v1",
            reversed ? Materials(oxygenCv).Reverse() : Materials(oxygenCv));
        FluidMaterial[] Materials(double oxygenCv) => new[]
        {
            new FluidMaterial("oxygen", NativeMatterPhase.Gas, 0.0319988, oxygenCv),
            new FluidMaterial("nitrogen", NativeMatterPhase.Gas, 0.0280134, 743),
            new FluidMaterial("water", NativeMatterPhase.Liquid, 0.01801528, 4184, 1000, -100000)
        };
        byte[] Envelope(int x, bool metadataOnly = false)
        {
            var raw = nativeFixture(); Int(raw, 20, x);
            var cell = 29 + 9 * 16; Int(raw, cell, 1); raw.AsSpan(cell + 4, 12).Clear();
            if (metadataOnly) Int(raw, 29 + 5 * 16 + 8, 0);
            var result = new byte[4 + raw.Length]; Int(result, 0, raw.Length); raw.CopyTo(result, 4); return result;
        }
        NativeColonyImport Import(bool metadataOnly = false) => NativeColonyImport.StageNewColony(width, 16, placements,
            new[] { new NativeWorldSource(7, Envelope(11, metadataOnly)), new NativeWorldSource(9, Envelope(20, metadataOnly)) }, phases, bindings, 1);
        ColonyFluidState Create() => ColonyFluidState.Import(Import(), Catalog());
        FluidInventory Parcel(string species, double mass, double temperature = 300, double vx = 0, double vy = 0) =>
            Catalog().CreateInventory(new Dictionary<string, double> { [species] = mass }, temperature, vx, vy);
        void Near(double actual, double expected, double relative = 1e-11)
        { check(double.IsFinite(actual) && Math.Abs(actual - expected) <= Math.Max(double.Epsilon, Math.Abs(expected) * relative)); }
        void SameInventory(FluidInventory actual, FluidInventory expected, double relative = 1e-11)
        {
            check(actual.MassesKg.Count == expected.MassesKg.Count && actual.DiseaseCounts.Count == expected.DiseaseCounts.Count);
            foreach (var entry in expected.MassesKg) Near(actual.MassesKg[entry.Key], entry.Value, relative);
            foreach (var entry in expected.DiseaseCounts) Near(actual.DiseaseCounts[entry.Key], entry.Value, relative);
            Near(actual.TotalEnergyJoules, expected.TotalEnergyJoules, relative); Near(actual.MomentumX, expected.MomentumX, relative);
            Near(actual.MomentumY, expected.MomentumY, relative); Near(actual.RadiationQuantity, expected.RadiationQuantity, relative);
        }

        test("colony authority imports both worlds and resolves fluids vacuum solids and outside geometry", () =>
        {
            var import = Import(); var state = ColonyFluidState.Import(import, Catalog()); var snapshot = state.CaptureSnapshot();
            check(import.VacuumHash == 1 && snapshot.Width == width && snapshot.Height == 16 && snapshot.Worlds.Count == 2);
            check(snapshot.Revision == 0 && snapshot.CompletedFrame == 0 && snapshot.SimulationTimeSeconds == 0);
            var sample = snapshot.SampleCell(gas);
            check(sample.WorldId == 7 && sample.X == 11 && sample.Y == 7 && !sample.IsSolid && sample.NativeElementHash == 1);
            check(sample.DominantMaterialId == "oxygen" && sample.DominantPhase == NativeMatterPhase.Gas);
            Near(sample.TemperatureKelvin, 300); Near(sample.Inventory.TotalMassKg, 2);
            Near(sample.GasPressurePa, 2 / 0.0319988 * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300);
            Near(snapshot.SampleCell(water).LiquidVolumeFraction, 0.1);
            Near(snapshot.SampleCell(water).GasPressurePa, 0);
            check(snapshot.SampleCell(empty).Inventory.TotalMassKg == 0 && snapshot.SampleCell(empty).DominantPhase == NativeMatterPhase.Vacuum);
            check(snapshot.SampleCell(solid).IsSolid && snapshot.SampleCell(solid).NativeElementHash == 4);
            check(snapshot.SampleCell(otherGas).WorldId == 9 && !snapshot.TrySampleCell(0, out _) && !snapshot.TrySampleCell(-1, out _));
            reject(() => snapshot.SampleCell(width * 16));
            Near(snapshot.Totals().TotalMassKg, 204); SameInventory(snapshot.InitialInventory, snapshot.Totals());
            foreach (var world in import.Worlds) check(NativeWorldBlob.Split(world.Migration.NativeData, phases, 1).Fluids.Count == 0);
        });

        test("colony import rejects missing phase volume and temperature models without changing staged blobs", () =>
        {
            var import = Import(); var bytes = import.Worlds.Select(world => world.LengthPrefixedData.ToArray()).ToArray();
            reject(() => ColonyFluidState.Import(import, new FluidMaterialCatalog("incomplete", Materials(658).Take(2))));
            var wrong = new[] { Materials(658)[0], new FluidMaterial("water", NativeMatterPhase.Gas, 0.018, 1000) };
            reject(() => ColonyFluidState.Import(import, new FluidMaterialCatalog("phase-mismatch", wrong)));
            reject(() => ColonyFluidState.Import(import, Catalog(), 0.05));
            var cold = new[] { new FluidMaterial("oxygen", NativeMatterPhase.Gas, 0.032, 658, minTemperatureKelvin: 400), Materials(658)[2] };
            reject(() => ColonyFluidState.Import(import, new FluidMaterialCatalog("unsupported-temperature", cold)));
            for (var i = 0; i < bytes.Length; ++i) check(import.Worlds[i].LengthPrefixedData.SequenceEqual(bytes[i]));
        });

        test("bulk transfers conserve energy momentum constituents disease and radiation in one immutable publication", () =>
        {
            var state = Create(); var before = state.CaptureSnapshot();
            using var transaction = state.BeginTransaction(); transaction.Transfer(gas, empty, 0.5);
            check(ReferenceEquals(before, state.CaptureSnapshot()));
            var after = transaction.Commit(); check(after.Revision == 1 && ReferenceEquals(after, state.CaptureSnapshot()));
            Near(before.SampleCell(gas).Inventory.TotalMassKg, 2); Near(before.SampleCell(empty).Inventory.TotalMassKg, 0);
            Near(after.SampleCell(gas).Inventory.TotalMassKg, 1.5); Near(after.SampleCell(empty).Inventory.TotalMassKg, 0.5);
            Near(after.SampleCell(empty).Inventory.DiseaseCounts[123], 114); Near(after.SampleCell(empty).Inventory.RadiationQuantity, 4.25);
            Near(after.SampleCell(empty).TemperatureKelvin, 300); SameInventory(after.Totals(), before.Totals());
            reject(() => transaction.Commit());
        });

        test("failed output capacity leaves source and staged operations intact", () =>
        {
            var state = Create(); var before = state.CaptureSnapshot();
            using var transaction = state.BeginTransaction();
            transaction.Inject(empty, Parcel("water", 1000), "test-fill");
            reject(() => transaction.Transfer(gas, empty, 0.1));
            reject(() => transaction.Transfer(water, empty, 1));
            var after = transaction.Commit(); Near(after.SampleCell(gas).Inventory.TotalMassKg, 2);
            Near(after.SampleCell(water).Inventory.TotalMassKg, 100); Near(after.SampleCell(empty).LiquidVolumeFraction, 1);
            check(after.SampleCell(empty).Inventory.MassesKg.Count == 1);
            Near(after.SuppliedInventory.MassesKg["water"], 1000);
            Near(before.SampleCell(empty).LiquidVolumeFraction, 0);
        });

        test("invalid transfers and impossible heat cannot consume an input or publish partial changes", () =>
        {
            var state = Create(); var saved = state.Save(); using var transaction = state.BeginTransaction();
            reject(() => transaction.Transfer(gas, solid, 0.5)); reject(() => transaction.Transfer(gas, 0, 0.5));
            reject(() => transaction.Transfer(gas, empty, 3)); reject(() => transaction.Transfer(gas, empty, -1));
            reject(() => transaction.Transfer(gas, empty, double.NaN)); reject(() => transaction.Transfer(gas, gas, 1));
            reject(() => transaction.ApplyHeat(gas, -1e9, "impossible-cooling"));
            reject(() => transaction.Inject(water, Parcel("water", 1000), "overfill"));
            reject(() => transaction.Inject(empty, new FluidInventory(new Dictionary<string, double> { ["unknown"] = 1 }, 10), "unknown-model"));
            check(saved.SequenceEqual(state.Save())); transaction.Commit(); SameInventory(state.CaptureSnapshot().Totals(), state.CaptureSnapshot().InitialInventory);
        });

        test("selective transfer retains trace species and transfers its actual thermal energy", () =>
        {
            var state = Create(); using (var transaction = state.BeginTransaction())
            { transaction.Inject(gas, Parcel("nitrogen", 1e-16), "trace-source"); transaction.Commit(); }
            var before = state.CaptureSnapshot(); using (var transaction = state.BeginTransaction())
            { transaction.TransferSpecies(gas, empty, "nitrogen", 1e-16); transaction.Commit(); }
            var after = state.CaptureSnapshot();
            check(!after.SampleCell(gas).Inventory.MassesKg.ContainsKey("nitrogen"));
            Near(after.SampleCell(empty).Inventory.MassesKg["nitrogen"], 1e-16);
            Near(after.SampleCell(empty).TemperatureKelvin, 300); SameInventory(after.Totals(), before.Totals());
            // Depletion of the last selected species must leave exactly no orphan energy.
            using (var transaction = state.BeginTransaction())
            { transaction.TransferSpecies(empty, gas, "nitrogen", 1e-16); transaction.Commit(); }
            check(state.CaptureSnapshot().SampleCell(empty).Inventory.TotalEnergyJoules == 0);
        });

        test("counterflow mixing conserves total energy and converts unresolved velocity to heat", () =>
        {
            var state = Create(); using var transaction = state.BeginTransaction();
            transaction.Inject(empty, Parcel("oxygen", 1, vx: 20), "east-input");
            transaction.Inject(empty, Parcel("oxygen", 1, vx: -20), "west-input");
            transaction.ApplyHeat(empty, 100, "test-heater"); var snapshot = transaction.Commit(); var sample = snapshot.SampleCell(empty);
            Near(sample.Inventory.MomentumX, 0); Near(sample.Inventory.KineticEnergyJoules, 0);
            Near(sample.TemperatureKelvin, 300 + 500.0 / (2 * 658)); Near(snapshot.ExternalHeatJoules, 100);
            Near(snapshot.Totals().TotalEnergyJoules, snapshot.InitialInventory.TotalEnergyJoules + snapshot.SuppliedInventory.TotalEnergyJoules + 100);
            check(state.Save().SequenceEqual(ColonyFluidState.Restore(state.Save(), Catalog()).Save()));
        });

        test("external removals retain constituent energy metadata and explicit source sink totals", () =>
        {
            var state = Create(); var original = state.CaptureSnapshot().SampleCell(gas).Inventory;
            using var transaction = state.BeginTransaction(); var extracted = transaction.Extract(gas, 2, "oxygen-sink");
            var after = transaction.Commit(); SameInventory(extracted, original); SameInventory(after.RemovedInventory, original);
            check(after.SampleCell(gas).Inventory.TotalMassKg == 0 && after.SampleCell(gas).Inventory.TotalEnergyJoules == 0);
            Near(after.Totals().MassesKg["oxygen"], 2); Near(after.Totals().DiseaseCounts[123], 456);
            check(state.Save().SequenceEqual(ColonyFluidState.Restore(state.Save(), Catalog()).Save()));
        });

        test("selective external debits conserve metadata and read staged mutations before publication", () =>
        {
            var state = Create(); using var transaction = state.BeginTransaction();
            transaction.Inject(gas, Parcel("nitrogen", 0.5), "selective-test-input");
            check(transaction.SampleCell(gas).Inventory.MassesKg.ContainsKey("nitrogen"));
            check(!state.CaptureSnapshot().SampleCell(gas).Inventory.MassesKg.ContainsKey("nitrogen"));
            var parcel = transaction.ExtractSpecies(gas, "nitrogen", 0.5, "selective-nitrogen-sink");
            Near(parcel.MassesKg["nitrogen"], 0.5); Near(parcel.DiseaseCounts[123], 456 * 0.5 / 2.5);
            check(!transaction.SampleCell(gas).Inventory.MassesKg.ContainsKey("nitrogen"));
            reject(() => transaction.ExtractSpecies(gas, "nitrogen", 0.1, "missing-input"));
            reject(() => transaction.ExtractSpecies(gas, "oxygen", 0, "zero-input"));
            var oxygen = transaction.ExtractSpecies(gas, "oxygen", 2, "deplete-input");
            Near(oxygen.MassesKg["oxygen"], 2); check(transaction.SampleCell(gas).Inventory.TotalEnergyJoules == 0);
            var after = transaction.Commit();
            check(after.MaterialModelVersion == Catalog().Version && after.MaterialModelFingerprint == Catalog().Fingerprint);
            Near(after.RemovedInventory.MassesKg["oxygen"], 2); Near(after.RemovedInventory.MassesKg["nitrogen"], 0.5);
            check(state.Save().SequenceEqual(ColonyFluidState.Restore(state.Save(), Catalog()).Save()));
        });

        test("stale competing transactions reject their entire publication and disposed changes roll back", () =>
        {
            var state = Create(); using var first = state.BeginTransaction(); using var second = state.BeginTransaction();
            first.Transfer(gas, empty, 0.25); second.Extract(otherGas, 1, "stale-sink"); first.Commit();
            var before = state.Save(); reject(() => second.Commit()); check(before.SequenceEqual(state.Save()));
            using (var abandoned = state.BeginTransaction()) abandoned.Transfer(gas, empty, 0.25);
            check(before.SequenceEqual(state.Save()));
            var disposed = state.BeginTransaction(); disposed.Dispose(); reject(() => disposed.Transfer(gas, empty, 0.1));
        });

        test("frame publication enforces monotonic completed frame and simulation time", () =>
        {
            var state = Create(); using (var transaction = state.BeginTransaction()) transaction.CommitFrame(12, 0.2);
            var before = state.Save();
            using (var transaction = state.BeginTransaction())
            {
                reject(() => transaction.CommitFrame(11, 0.3)); reject(() => transaction.CommitFrame(13, 0.1));
                reject(() => transaction.CommitFrame(13, double.NaN));
            }
            check(before.SequenceEqual(state.Save())); var restored = ColonyFluidState.Restore(before, Catalog()).CaptureSnapshot();
            check(restored.CompletedFrame == 12); Near(restored.SimulationTimeSeconds, 0.2);
        });

        test("colony checkpoint is deterministic culture independent and continues identically after restore", () =>
        {
            var state = Create(); using (var transaction = state.BeginTransaction())
            { transaction.Transfer(gas, empty, 0.5); transaction.ApplyHeat(empty, 250, "heater"); transaction.CommitFrame(30, 3); }
            var bytes = state.Save(); check(Catalog().Fingerprint == Catalog(true).Fingerprint);
            var previous = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("de-DE");
                var restored = ColonyFluidState.Restore(bytes, Catalog(true)); check(bytes.SequenceEqual(restored.Save()));
                using (var transaction = state.BeginTransaction()) { transaction.Transfer(empty, gas, 0.1); transaction.CommitFrame(31, 3.1); }
                using (var transaction = restored.BeginTransaction()) { transaction.Transfer(empty, gas, 0.1); transaction.CommitFrame(31, 3.1); }
                check(state.Save().SequenceEqual(restored.Save()));
                bytes.AsSpan().Clear(); check(state.CaptureSnapshot().SampleCell(empty).Inventory.TotalMassKg > 0);
            }
            finally { CultureInfo.CurrentCulture = previous; }
        });

        test("colony checkpoint rejects corruption unsupported versions catalog drift truncation and trailing bytes", () =>
        {
            var bytes = Create().Save();
            var damaged = (byte[])bytes.Clone(); damaged[damaged.Length / 2] ^= 1; reject(() => ColonyFluidState.Restore(damaged, Catalog()));
            reject(() => ColonyFluidState.Restore(bytes.AsSpan(0, bytes.Length - 1), Catalog()));
            reject(() => ColonyFluidState.Restore(Array.Empty<byte>(), Catalog()));
            var trailing = new byte[bytes.Length + 1]; bytes.CopyTo(trailing, 0); reject(() => ColonyFluidState.Restore(trailing, Catalog()));
            var version = (byte[])bytes.Clone(); Int(version, 8, 999); Rehash(version); reject(() => ColonyFluidState.Restore(version, Catalog()));
            reject(() => ColonyFluidState.Restore(bytes, Catalog(oxygenCv: 659)));
            reject(() => ColonyFluidState.Restore(bytes, new FluidMaterialCatalog("test-properties-v2", Materials(658))));
        });

        test("checksum-valid checkpoints still reject nonfinite fields impossible geometry and unbalanced ledgers", () =>
        {
            var bytes = Create().Save();
            using var reader = new BinaryReader(new MemoryStream(bytes)); reader.BaseStream.Position = 12;
            var versionBytes = reader.ReadInt32(); reader.BaseStream.Position += versionBytes + 32;
            var widthOffset = (int)reader.BaseStream.Position;
            var badGeometry = (byte[])bytes.Clone(); Int(badGeometry, widthOffset, int.MaxValue); Rehash(badGeometry);
            reject(() => ColonyFluidState.Restore(badGeometry, Catalog()));
            var badVolume = (byte[])bytes.Clone(); BinaryPrimitives.WriteInt64LittleEndian(badVolume.AsSpan(widthOffset + 12), BitConverter.DoubleToInt64Bits(double.NaN)); Rehash(badVolume);
            reject(() => ColonyFluidState.Restore(badVolume, Catalog()));
            // First initial oxygen mass follows geometry, constituent count, and UTF8 identity.
            reader.BaseStream.Position = widthOffset + 12 + 8 + 8 + 8 + 8;
            var solids = reader.ReadInt32(); reader.BaseStream.Position += solids * 4;
            var worlds = reader.ReadInt32();
            for (var i = 0; i < worlds; ++i)
            {
                reader.ReadInt32(); var worldWidth = reader.ReadInt32(); var worldHeight = reader.ReadInt32();
                reader.BaseStream.Position += 8 + worldWidth * worldHeight * 4;
            }
            check(reader.ReadInt32() == 2); var idBytes = reader.ReadInt32(); reader.BaseStream.Position += idBytes;
            var massOffset = (int)reader.BaseStream.Position; var badLedger = (byte[])bytes.Clone();
            BinaryPrimitives.WriteInt64LittleEndian(badLedger.AsSpan(massOffset), BitConverter.DoubleToInt64Bits(999)); Rehash(badLedger);
            reject(() => ColonyFluidState.Restore(badLedger, Catalog()));
        });

        test("zero-mass native metadata is preserved across ownership and checkpoint roundtrip", () =>
        {
            var state = ColonyFluidState.Import(Import(true), Catalog()); var sample = state.CaptureSnapshot().SampleCell(gas);
            check(sample.Inventory.TotalMassKg == 0 && sample.TemperatureKelvin == 0);
            Near(sample.Inventory.DiseaseCounts[123], 456); Near(sample.Inventory.RadiationQuantity, 17);
            var restored = ColonyFluidState.Restore(state.Save(), Catalog()); SameInventory(sample.Inventory, restored.CaptureSnapshot().SampleCell(gas).Inventory);
            using var transaction = state.BeginTransaction(); reject(() => transaction.Extract(gas, 0.01, "no-carrier-mass"));
        });

        test("atomic geometry clearing opens a solid for fluid while previous snapshots remain unchanged", () =>
        {
            var state = Create(); var before = state.CaptureSnapshot(); using var transaction = state.BeginTransaction();
            transaction.ClearSolid(solid); transaction.Transfer(water, solid, 10); var after = transaction.Commit();
            check(before.SampleCell(solid).IsSolid && !after.SampleCell(solid).IsSolid);
            Near(after.SampleCell(solid).Inventory.MassesKg["water"], 10); Near(after.SampleCell(water).Inventory.TotalMassKg, 90);
            SameInventory(after.Totals(), before.Totals());
            var restored = ColonyFluidState.Restore(state.Save(), Catalog()); check(state.Save().SequenceEqual(restored.Save()));
            check(!restored.CaptureSnapshot().SampleCell(solid).IsSolid);
        });

        test("solid placement atomically displaces the complete mixture and metadata before geometry publication", () =>
        {
            var state = Create(); var before = state.CaptureSnapshot(); using var transaction = state.BeginTransaction();
            reject(() => transaction.SetSolid(gas, 4));
            transaction.DisplaceAndSetSolid(gas, 4, empty); var after = transaction.Commit();
            check(after.SampleCell(gas).IsSolid && after.SampleCell(gas).Inventory.TotalMassKg == 0);
            SameInventory(after.SampleCell(empty).Inventory, before.SampleCell(gas).Inventory);
            SameInventory(after.Totals(), before.Totals()); check(!before.SampleCell(gas).IsSolid);
            var restored = ColonyFluidState.Restore(state.Save(), Catalog()); check(restored.CaptureSnapshot().SampleCell(gas).IsSolid);
            using (var reopen = restored.BeginTransaction())
            { reopen.ClearSolid(gas); reopen.Transfer(empty, gas, 2); reopen.SetSolid(empty, 4); reopen.Commit(); }
            check(!restored.CaptureSnapshot().SampleCell(gas).IsSolid && restored.CaptureSnapshot().SampleCell(empty).IsSolid);
            SameInventory(restored.CaptureSnapshot().SampleCell(gas).Inventory, before.SampleCell(gas).Inventory);
        });

        test("blocked displacement and invalid solid identities change neither geometry nor inventory", () =>
        {
            var state = Create(); using (var fill = state.BeginTransaction())
            { fill.Inject(empty, Parcel("water", 1000), "fill-blocked-destination"); fill.Commit(); }
            var before = state.CaptureSnapshot(); using var transaction = state.BeginTransaction();
            reject(() => transaction.DisplaceAndSetSolid(water, 4, empty));
            reject(() => transaction.DisplaceAndSetSolid(gas, 4, empty));
            reject(() => transaction.DisplaceAndSetSolid(gas, 2, water));
            reject(() => transaction.SetSolid(solid, 2)); reject(() => transaction.SetSolid(solid, 999));
            reject(() => transaction.ClearSolid(0)); reject(() => transaction.DisplaceAndSetSolid(gas, 4, gas));
            var after = transaction.Commit(); SameInventory(after.Totals(), before.Totals());
            check(!after.SampleCell(gas).IsSolid && !after.SampleCell(water).IsSolid);
            Near(after.SampleCell(empty).LiquidVolumeFraction, 1);
            check(state.Save().SequenceEqual(ColonyFluidState.Restore(state.Save(), Catalog()).Save()));
        });

        test("metadata-only cells also require displacement before solid placement", () =>
        {
            var state = ColonyFluidState.Import(Import(true), Catalog()); var before = state.CaptureSnapshot();
            using var transaction = state.BeginTransaction(); reject(() => transaction.SetSolid(gas, 4));
            transaction.DisplaceAndSetSolid(gas, 4, empty); var after = transaction.Commit();
            check(after.SampleCell(gas).IsSolid); SameInventory(after.SampleCell(empty).Inventory, before.SampleCell(gas).Inventory);
            SameInventory(after.Totals(), before.Totals());
        });

        test("inventory snapshots own caller inputs and reject invalid material definitions", () =>
        {
            var masses = new Dictionary<string, double> { ["oxygen"] = 1 }; var disease = new Dictionary<int, double> { [123] = 12 };
            var inventory = Catalog().CreateInventory(masses, 300, diseaseCounts: disease); masses["oxygen"] = 100; disease[123] = 100;
            Near(inventory.TotalMassKg, 1); Near(inventory.DiseaseCounts[123], 12);
            reject(() => new FluidMaterial("bad", NativeMatterPhase.Gas, 0, 100));
            reject(() => new FluidMaterial("bad", NativeMatterPhase.Liquid, 0.03, 100));
            reject(() => new FluidMaterial("bad", NativeMatterPhase.Gas, 0.03, double.NaN));
            reject(() => new FluidMaterial("\ud800", NativeMatterPhase.Gas, 0.03, 100));
            reject(() => new FluidMaterialCatalog("duplicates", new[] { Materials(658)[0], Materials(658)[0] }));
            reject(() => new FluidInventory(new Dictionary<string, double>(), 1));
        });

        test("ten thousand closed transfers retain every trace constituent and balance the restored ledger", () =>
        {
            var state = Create(); using (var transaction = state.BeginTransaction())
            { transaction.Inject(gas, Parcel("nitrogen", 1e-16), "trace-seed"); transaction.Commit(); }
            var before = state.CaptureSnapshot().Totals();
            for (var i = 0; i < 10000; ++i)
            {
                using var transaction = state.BeginTransaction(); transaction.Transfer(gas, empty, 0.01); transaction.Transfer(empty, gas, 0.01);
                transaction.Commit();
            }
            SameInventory(state.CaptureSnapshot().Totals(), before, 1e-8);
            Near(state.CaptureSnapshot().Totals().MassesKg["nitrogen"], 1e-16, 1e-8);
            check(state.Save().SequenceEqual(ColonyFluidState.Restore(state.Save(), Catalog()).Save()));
        });
    }

    private static void Int(byte[] data, int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(offset, 4), value);
    private static void Rehash(byte[] data)
    {
        using var sha = SHA256.Create(); var checksum = sha.ComputeHash(data, 0, data.Length - 32); checksum.CopyTo(data, data.Length - 32);
    }
}
