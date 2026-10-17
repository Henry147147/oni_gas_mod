using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;

internal static class BreathingCellReadTests
{
    internal static void Run(System.Action<string, System.Action> test, System.Action<bool> check, System.Action<System.Action> reject)
    {
        var catalog = new FluidMaterialCatalog("breathing-cell-tests-v1", new[] {
            new FluidMaterial("oxygen", NativeMatterPhase.Gas, .0319988, 658),
            new FluidMaterial("nitrogen", NativeMatterPhase.Gas, .0280134, 743),
            new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, .0440095, 655),
            new FluidMaterial("chlorine", NativeMatterPhase.Gas, .070906, 362),
            new FluidMaterial("water", NativeMatterPhase.Liquid, .01801528, 4184, 1000) });
        MixturePresentationModel Model(AtmosphereSettings? settings = null) => new MixturePresentationModel(catalog,
            catalog.Materials.Keys.Select(id => new MixtureSpeciesStyle(id, id, new MixtureSrgbColor(.2, .4, .6))),
            settings ?? new AtmosphereSettings(), "oxygen", "carbon_dioxide", new Dictionary<string, double> { ["chlorine"] = 1e-9 });
        FluidInventory Air(double oxygen = .21, double co2 = 0, double pressure = 101325, double chlorine = 0)
        {
            var moles = pressure / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300);
            return catalog.CreateInventory(new Dictionary<string, double> {
                ["oxygen"] = moles * oxygen * .0319988,
                ["nitrogen"] = moles * (1 - oxygen - co2 - chlorine) * .0280134,
                ["carbon_dioxide"] = moles * co2 * .0440095,
                ["chlorine"] = moles * chlorine * .070906 }, 300);
        }
        float Read(MixturePresentation reading) => MixtureBreathingCellRead.OxygenMass(reading, "oxygen");
        test("breathing cell advertises actual oxygen rather than total mixture mass", () => {
            var source = Air(); var reading = Model().Describe(source, 1, 47, "owned:cell:1");
            var mass = Read(reading);
            check(reading.IsBreathable && mass > 0 && mass <= source.MassesKg["oxygen"] && mass < reading.TotalMassKg * .3);
            check(reading.Revision == 47 && reading.SourceId == "owned:cell:1" && source.TotalMassKg == reading.TotalMassKg);
        });
        test("breathing scalar rounds down instead of creating unavailable oxygen", () => {
            var source = catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = .3, ["nitrogen"] = 1 }, 300);
            var reading = Model().Describe(source, 1);
            var mass = Read(reading); var next = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(mass) + 1);
            check(mass > 0 && mass <= .3 && next > .3 && source.MassesKg["oxygen"] == .3);
        });
        test("native breathing reader blocks every configured atmosphere failure", () => {
            foreach (var source in new[] { Air(.1), Air(.5), Air(pressure: 20000), Air(co2: .02), Air(co2: .004, pressure: 200000) }) {
                var reading = Model().Describe(source, 1);
                check(!reading.IsBreathable && reading.Failures != BreathingFailure.None && Read(reading) == 0);
            }
        });
        test("easy breathing changes only the configured upper oxygen rule", () => {
            var model = Model(new AtmosphereSettings { HardMode = false });
            check(Read(model.Describe(Air(.5), 1)) > 0);
            check(Read(model.Describe(Air(.1), 1)) == 0 && Read(model.Describe(Air(co2: .02), 1)) == 0);
        });
        test("hazardous trace remains unbreathable below visual trace cutoff", () => {
            var reading = Model().Describe(Air(chlorine: 1e-8), 1);
            check((reading.Failures & BreathingFailure.Contaminant) != 0 && Read(reading) == 0);
            check(reading.Constituents.Single(row => row.MaterialId == "chlorine").IsTrace);
        });
        test("vacuum and filled liquid never advertise scalar compatibility oxygen", () => {
            var vacuum = catalog.CreateInventory(new Dictionary<string, double>(), 300);
            var water = catalog.CreateInventory(new Dictionary<string, double> { ["water"] = 1000 }, 300);
            check(Read(Model().Describe(vacuum, 1)) == 0 && Read(Model().Describe(water, 1)) == 0);
            reject(() => MixtureBreathingCellRead.OxygenMass(null!, "oxygen"));
            reject(() => MixtureBreathingCellRead.OxygenMass(Model().Describe(Air(), 1), " "));
            reject(() => MixtureBreathingCellRead.OxygenMass(Model().Describe(Air(), 1), "unmapped-oxygen"));
        });
        test("actual native world breathing helper has the inspected scalar and out-element signature", () => {
            var method = typeof(GasBreatherFromWorldProvider).GetMethod("GetBreathableCellMass", BindingFlags.NonPublic | BindingFlags.Static);
            check(method != null && method.ReturnType == typeof(float) && method.GetMethodBody() != null);
            var parameters = method!.GetParameters();
            check(parameters.Length == 2 && parameters[0].ParameterType == typeof(int) && parameters[1].IsOut &&
                parameters[1].ParameterType == typeof(SimHashes).MakeByRefType());
        });
    }
}
