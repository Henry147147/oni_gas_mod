using System;
using System.Linq;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;

internal static class NativeDomainReadingTests
{
    private static FluidMaterialCatalog Catalog() => new FluidMaterialCatalog("native-reading-controls-v1", new[] {
        new FluidMaterial("nitrogen",NativeMatterPhase.Gas,.028,743),new FluidMaterial("oxygen",NativeMatterPhase.Gas,.032,658),
        new FluidMaterial("helium",NativeMatterPhase.Gas,.004,3100),new FluidMaterial("water",NativeMatterPhase.Liquid,.018,4184,1000),
        new FluidMaterial("carbon_dioxide",NativeMatterPhase.Gas,.044,650) });
    private static OniNativeDomain.Cell Mixed()
    {
        var moles=.8*100000/(FluidMaterialCatalog.GasConstantJoulesPerMolKelvin*300);
        return new OniNativeDomain.Cell {Nitrogen=.8*moles*.028,Oxygen=.2*moles*.032,
            Water=.2*1000*(1+100000/2.2e9),LiquidFraction=.2,Temperature=300,Pressure=100000,
            InternalEnergy=-3e9,ResolvedKineticEnergy=2,MomentumX=.5,MomentumY=-.25};
    }
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        test("completed native wet reading retains actual compressed liquid volume and gas headspace",()=> {
            var cell=Mixed();var reading=NativeDomainReading.Create(cell,Catalog(),12,650);
            check(reading.LiquidVolumeCubicMetres==.2 && Math.Abs(reading.GasVolumeCubicMetres-.8)<1e-14 &&
                reading.Constituents.Single(c=>c.MaterialId=="water").PartialVolumeCubicMetres==.2);
            check(reading.GasPressurePa==reading.MechanicalPressurePa && reading.GasPressurePa==100000 &&
                reading.GasMassKg<reading.TotalMassKg && reading.VacuumVolumeCubicMetres==0);
            check(reading.StoredInternalEnergyJoules==cell.InternalEnergy && reading.ResolvedKineticEnergyJoules==2 &&
                reading.MomentumX==.5 && reading.MomentumY==-.25 && reading.Revision==12 && reading.SourceId=="native-domain:cell:650");
        });
        test("native reading keeps separate single-phase gas and compressed-water behavior",()=> {
            var gas=Mixed();gas.Water=0;gas.LiquidFraction=0;gas.Nitrogen/=.8;gas.Oxygen/=.8;
            var reading=NativeDomainReading.Create(gas,Catalog(),1,1);
            check(reading.LiquidVolumeCubicMetres==0 && Math.Abs(reading.GasVolumeCubicMetres-1)<1e-14);
            var water=new OniNativeDomain.Cell {Water=1100,LiquidFraction=1,Temperature=300,Pressure=2.2e8};
            reading=NativeDomainReading.Create(water,Catalog(),1,1);
            check(reading.GasPressurePa==0 && reading.GasVolumeCubicMetres==0 && reading.LiquidVolumeCubicMetres==1 && reading.Constituents.Count==1);
        });
        test("trace gas headspace survives below the rounding unit of native liquid volume",()=> {
            var cell=new OniNativeDomain.Cell {Water=1100,LiquidFraction=1,Temperature=300,Pressure=2.2e8,
                Nitrogen=1e-28*2.2e8*.028/(FluidMaterialCatalog.GasConstantJoulesPerMolKelvin*300)};
            var reading=NativeDomainReading.Create(cell,Catalog(),2,2);
            check(reading.GasVolumeCubicMetres>0 && reading.GasVolumeCubicMetres<1e-27 && reading.GasMassKg==cell.Nitrogen &&
                reading.Constituents.Single(c=>c.MaterialId=="nitrogen").MassKg==cell.Nitrogen && reading.LiquidVolumeCubicMetres==1);
        });
        test("native void readings retain exact vacuum, partial liquid and resolved gas occupancy",()=> {
            var empty=new OniNativeDomain.Cell();
            var reading=NativeDomainReading.Create(empty,Catalog(),1,1,true,0);
            check(reading.TotalMassKg==0 && reading.TemperatureKelvin==0 && reading.VacuumVolumeCubicMetres==1);
            var water=new OniNativeDomain.Cell {Water=500,LiquidFraction=.5,Temperature=300};
            reading=NativeDomainReading.Create(water,Catalog(),1,1,true,.5);
            check(reading.LiquidVolumeCubicMetres==.5 && reading.VacuumVolumeCubicMetres==.5 && reading.MechanicalPressurePa==0);
            var gas=new OniNativeDomain.Cell {Oxygen=.032,Pressure=FluidMaterialCatalog.GasConstantJoulesPerMolKelvin*300/.25,Temperature=300};
            reading=NativeDomainReading.Create(gas,Catalog(),1,1,true,.25);
            check(Math.Abs(reading.GasVolumeCubicMetres-.25)<1e-14 && Math.Abs(reading.VacuumVolumeCubicMetres-.75)<1e-14);
            reject(()=>NativeDomainReading.Create(gas,Catalog(),1,1));
            water.Pressure=100;reject(()=>NativeDomainReading.Create(water,Catalog(),1,1,true,.5));
            empty.InternalEnergy=1;reject(()=>NativeDomainReading.Create(empty,Catalog(),1,1,true,0));
        });
        test("void reading uses native geometry instead of rounded pressure and retains sub-ulp gas headspace",()=> {
            var cell=Mixed();cell.Pressure*=1-6e-14;
            var reading=NativeDomainReading.Create(cell,Catalog(),1,1,true,1);
            check(reading.GasVolumeCubicMetres+reading.LiquidVolumeCubicMetres==1 && reading.VacuumVolumeCubicMetres==0);
            var trace=new OniNativeDomain.Cell {Water=1000,LiquidFraction=1,Oxygen=1e-20,Temperature=300,Pressure=100000};
            reading=NativeDomainReading.Create(trace,Catalog(),1,1,true,1);
            check(reading.GasVolumeCubicMetres>0 && reading.Constituents.Single(c=>c.MaterialId=="oxygen").PartialVolumeCubicMetres>0);
            trace.Water=500;trace.LiquidFraction=.5;trace.Oxygen=.032;trace.Pressure=FluidMaterialCatalog.GasConstantJoulesPerMolKelvin*300/1e-10;
            var occupied=.5+1e-10;reading=NativeDomainReading.Create(trace,Catalog(),1,1,true,occupied);
            check(reading.GasVolumeCubicMetres>0 && reading.GasVolumeCubicMetres!=occupied-.5 &&
                Math.Abs(reading.VolumeClosureResidualCubicMetres)<=128*2.2204460492503131e-16);
            foreach(var volume in new[]{double.NaN,double.PositiveInfinity,-1,1.01,0})
                reject(()=>NativeDomainReading.Create(cell,Catalog(),1,1,true,volume));
            reject(()=>NativeDomainReading.Create(cell,Catalog(),1,1,true));
        });
        test("native reading rejects nominal-density liquid volume and contradictory or nonfinite phase facts",()=> {
            var cell=Mixed();cell.LiquidFraction=cell.Water/1000;
            reject(()=>NativeDomainReading.Create(cell,Catalog(),1,1));
            foreach(var invalid in new[]{double.NaN,double.PositiveInfinity,-.1,1.2}) {
                cell=Mixed();cell.LiquidFraction=invalid;reject(()=>NativeDomainReading.Create(cell,Catalog(),1,1));
            }
            cell=Mixed();cell.Water=0;reject(()=>NativeDomainReading.Create(cell,Catalog(),1,1));
            cell=Mixed();cell.LiquidFraction=0;reject(()=>NativeDomainReading.Create(cell,Catalog(),1,1));
        });
    }
}
