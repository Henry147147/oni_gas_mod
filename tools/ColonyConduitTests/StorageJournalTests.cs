using System;using System.IO;using System.Linq;using MixedAtmospheres.Oni;using MixedAtmospheres.Core;
internal static class StorageJournalTests
{
static PipeMaterial M(int id)=>new PipeMaterial(id,id==2?1e-10:1000,0);
static void Check(bool v,string why){if(!v)throw new Exception(why);}
static byte[] Encode(OwnedFluidStore s){using var m=new MemoryStream();using var w=new BinaryWriter(m);s.Write(w);return m.ToArray();}
static OwnedFluidStore Cold(OwnedFluidStore s){var bytes=Encode(s);using var m=new MemoryStream(bytes);using var r=new BinaryReader(m);var c=OwnedFluidStore.Read(r);Check(c.Audit()&&Encode(c).SequenceEqual(bytes),"wide journal cold reencoding changed");return c;}
internal static void Run()
{
var parcel=PipeInventory.Single(M(1),1,300,0,1e308);var tank=new OwnedFluidStore(new StoredFluidAddress(1,0),2,PipeInventory.Empty);
for(var i=0;i<10;++i){tank.Commit(tank.PrepareAdmission(parcel,1,M));Check(tank.Inventory.Germs[0].Mass==1e308&&tank.Audit(),"finite large culture admission refused or changed");tank=Cold(tank);tank.Commit(tank.PrepareWithdrawal(double.MaxValue,M));Check(tank.Inventory.Count==0&&tank.Audit(),"wide culture withdrawal changed");}
Check(tank.Admitted.Germs[0].Units==CoupledAccountQuantity.FromDouble(1e308).Binary64Units*10&&tank.Withdrawn.Germs[0].Units==tank.Admitted.Germs[0].Units,"wide cumulative germ journals lost exact units");
Console.WriteLine("PASS exact wide storage journals: ten finite1e308 germ admissions/withdrawals, cumulative1e309 journals and exact cold continuation");
var culture=new OwnedFluidStore(new StoredFluidAddress(2,0),2,parcel);var flux=new PipeAmount(5e307);
for(var i=0;i<10;++i){var before=culture.Inventory.Germs[0].Amount;culture.CommitCulture(0,new GermPopulationStep(before.Plus(flux),flux,new GermPopulationClock(true,0)));culture=Cold(culture);before=culture.Inventory.Germs[0].Amount;culture.CommitCulture(0,new GermPopulationStep(before.Minus(flux),flux.Negated(),new GermPopulationClock(true,0)));Check(culture.Audit(),"wide birth/death accounting failed");}
Check(culture.Inventory.Germs[0].Mass==1e308&&culture.Births[0].Units==CoupledAccountQuantity.FromDouble(5e307).Binary64Units*10&&culture.Deaths[0].Units==culture.Births[0].Units,"wide birth/death journals lost physical counts");
Console.WriteLine("PASS exact wide storage journals: ten finite culture birth/death cycles, cumulative5e308 journals and exact cold continuation");
var massive=PipeInventory.Single(M(2),1e308,300);var mass=new OwnedFluidStore(new StoredFluidAddress(3,0),2,PipeInventory.Empty);
for(var i=0;i<3;++i){mass.Commit(mass.PrepareAdmission(massive,double.MaxValue,M));mass=Cold(mass);mass.Commit(mass.PrepareWithdrawal(double.MaxValue,M));}
Check(mass.Admitted.Masses[2].Units==CoupledAccountQuantity.FromDouble(1e308).Binary64Units*3&&mass.Audit(),"wide cumulative mass journal overflowed");
Console.WriteLine("PASS exact wide storage journals: finite mass1e308 cycles with cumulative mass3e308 exact; physical arithmetic unchanged");
var wide=default(StorageJournalAmount);var oracle=default(CoupledAccountQuantity);
foreach(var term in new[]{double.MaxValue,double.MaxValue,-double.MaxValue,-double.MaxValue,double.Epsilon,1.0,Math.ScaleB(1.0,-53),-1.0})
{
    wide=wide.Plus(new PipeAmount(term));oracle=oracle.Add(CoupledAccountQuantity.FromDouble(term));
    Check(wide.Units==oracle.Binary64Units,"storage journal units differ from independent Core account");
    if(oracle.TryGetRoundedValue(out var rounded))
        Check(BitConverter.DoubleToInt64Bits(wide.Value)==BitConverter.DoubleToInt64Bits(rounded),"storage journal display rounding differs from Core oracle");
    else
    {
        var refused=false;try{_=wide.Value;}catch(InvalidOperationException){refused=true;}
        Check(refused,"unrepresentable cumulative journal silently became a physical scalar");
    }
}
void Reject(byte[] bytes)
{
    using var stream=new MemoryStream(bytes);using var reader=new BinaryReader(stream);var refused=false;
    try{_=StorageJournalAmount.Read(reader);}catch(InvalidDataException){refused=true;}
    Check(refused,"noncanonical storage journal integer was accepted");
}
Reject(new byte[]{255,255,255,255});Reject(new byte[]{1,16,0,0});
Reject(new byte[]{1,0,0,0,0});Reject(new byte[]{2,0,0,0,1,0});Reject(new byte[]{2,0,0,0,255,255});Reject(new byte[]{1,0,0,0});
Console.WriteLine("PASS storage journal codec: independent exact-unit/display oracle, cancellation, unrepresentable display refusal and malformed canonical integers");

}
}
