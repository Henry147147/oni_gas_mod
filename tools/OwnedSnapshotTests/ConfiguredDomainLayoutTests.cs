using System;
using System.Linq;
using System.Runtime.InteropServices;
using MixedAtmospheres.Oni;
using static MixedAtmospheres.Oni.OniNativeDomain;

internal static class ConfiguredDomainLayoutTests
{
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        test("configured import and actor phase admission follow the mixture or liquid flags",()=> {
            foreach(var flags in new[]{0U, PhysicsGravity|PhysicsConduction, PhysicsVacuum})
                check(Enumerable.Range(0,6).Where(k=>ConfiguredDomainLayout.AdmitsSpecies(flags,(uint)k)).SequenceEqual(new[]{0,1,2,4}));
            check(Enumerable.Range(0,6).Where(k=>ConfiguredDomainLayout.AdmitsSpecies(PhysicsLiquid,(uint)k)).SequenceEqual(new[]{3}));
            foreach(var flags in new[]{PhysicsMixture, PhysicsVacuum|PhysicsMixture})
                check(Enumerable.Range(0,6).Where(k=>ConfiguredDomainLayout.AdmitsSpecies(flags,(uint)k)).SequenceEqual(new[]{0,1,2,3,4}));
            check(ConfiguredDomainLayout.AdmitsSpecies(ConfiguredDomainLayout.VacuumPhysics.Flags,3) &&
                ConfiguredDomainLayout.AdmitsSpecies(ConfiguredDomainLayout.VacuumPhysics.Flags,1));
        });
        test("configured physics record matches the native 40-byte MAOniDomainPhysics layout and flags",()=> {
            check(Marshal.SizeOf<Physics>()==40 && Marshal.OffsetOf<Physics>(nameof(Physics.Flags)).ToInt32()==8 &&
                Marshal.OffsetOf<Physics>(nameof(Physics.Reserved)).ToInt32()==12 &&
                Marshal.OffsetOf<Physics>(nameof(Physics.GravityMetresPerSecondSquared)).ToInt32()==16 &&
                Marshal.OffsetOf<Physics>(nameof(Physics.ShearViscosityPaSeconds)).ToInt32()==24 &&
                Marshal.OffsetOf<Physics>(nameof(Physics.BulkViscosityPaSeconds)).ToInt32()==32);
            check(Marshal.SizeOf<Configuration>()==40 && Marshal.SizeOf<InitialCell>()==56 && Marshal.SizeOf<Cell>()==96 && Marshal.SizeOf<Status>()==440);
            check(PhysicsGravity==1 && PhysicsConduction==2 && PhysicsViscosity==4 && PhysicsMixture==8 && PhysicsLiquid==16 && PhysicsVacuum==32 &&
                ConfiguredScenario==7 && ConfiguredAbiVersion==1 && ConfiguredMaximumSide==256 && ConfiguredMaximumCells==4096);
            var value=Physics.Create(PhysicsGravity|PhysicsConduction,9.81);
            check(value.Size==40 && value.Version==1 && value.Flags==3 && value.Reserved==0 && value.GravityMetresPerSecondSquared==9.81 &&
                BitConverter.DoubleToInt64Bits(value.ShearViscosityPaSeconds)==0 && BitConverter.DoubleToInt64Bits(value.BulkViscosityPaSeconds)==0);
        });
        test("the in-game configured room is larger than 3x2 with interior pillar and ledge solids touching fluid",()=> {
            var room=ConfiguredDomainLayout.PillaredRoom();
            check(room.Width==7 && room.Height==5 && room.Count==35 && room.SolidCount==4 && room.FluidCount==31 && room.FirstFluid==0);
            check(room.SolidIndices.SequenceEqual(new[]{3,10,17,26}) && room.Mask.Count(bit=>bit==1)==4);
            check(room.IsSolid(3,0) && room.IsSolid(3,2) && !room.IsSolid(3,3) && room.IsSolid(5,3) && !room.IsSolid(-1,0) && !room.IsSolid(7,0));
            // Pillar foot: left, right and the pillar cell above is solid; ledge: four fluid faces.
            check(room.FluidFaces(3)==2 && room.FluidFaces(10)==2 && room.FluidFaces(17)==3 && room.FluidFaces(26)==4);
            var copy=room.Mask;copy[0]=1;check(!room.IsSolid(0) && room.SameMask(room.Mask) && !room.SameMask(copy) && !room.SameMask(null));
        });
        test("configured layouts reject sizes, masks and inventories the native ABI rejects",()=> {
            reject(()=>new ConfiguredDomainLayout(1,4,new byte[4]));reject(()=>new ConfiguredDomainLayout(257,2,new byte[514]));
            reject(()=>new ConfiguredDomainLayout(65,64,new byte[65*64]));reject(()=>new ConfiguredDomainLayout(3,2,new byte[5]));
            reject(()=>new ConfiguredDomainLayout(3,2,new byte[]{0,0,2,0,0,0}));reject(()=>new ConfiguredDomainLayout(2,2,new byte[]{1,1,1,1}));
            reject(()=>new ConfiguredDomainLayout(2,2,null!));
            check(new ConfiguredDomainLayout(256,16,new byte[4096]).Count==4096 && new ConfiguredDomainLayout(64,64,new byte[4096]).FluidCount==4096);
            var room=ConfiguredDomainLayout.FromSolids(3,2,(dx,dy)=>dx==1 && dy==0);
            check(room.FirstFluid==0 && room.SolidIndices.SequenceEqual(new[]{1}));
            InitialCell Gas(int index)=>new InitialCell{Size=56,Version=1,Oxygen=.5+index,Temperature=290};
            var inventory=room.Inventory(Gas);
            check(inventory.Length==6 && inventory[1].Size==56 && inventory[1].Version==1 && inventory[1].Temperature==0 &&
                Enumerable.Range(0,5).All(species=>inventory[1].Mass(species)==0) && inventory[2].Oxygen==2.5);
            foreach(var bad in new[]{new InitialCell{Size=56,Version=1,Oxygen=1},new InitialCell{Size=56,Version=1,Temperature=300},
                new InitialCell{Size=48,Version=1,Oxygen=1,Temperature=300},new InitialCell{Size=56,Version=2,Oxygen=1,Temperature=300},
                new InitialCell{Size=56,Version=1,Oxygen=double.NaN,Temperature=300},new InitialCell{Size=56,Version=1,Oxygen=1,Temperature=double.PositiveInfinity},
                new InitialCell{Size=56,Version=1,Oxygen=-1,Water=2,Temperature=300}})
                reject(()=>room.Inventory(_=>bad));
        });
        test("configured vacuum inventories and copies require an explicit opt-in and exact zero records",()=> {
            var room=ConfiguredDomainLayout.FromSolids(3,2,(dx,dy)=>dx==1 && dy==0);
            InitialCell Empty(int index)=>new InitialCell {Size=56,Version=1};
            reject(()=>room.Inventory(Empty));
            var inventory=room.Inventory(Empty,true);check(inventory.Length==6 && inventory.All(c=>c.Temperature==0));
            var cells=new Cell[6];check(!room.CopyMatchesMask(cells) && room.CopyMatchesMask(cells,true));
            cells[0].InternalEnergy=1e-300;check(!room.CopyMatchesMask(cells,true));
            reject(()=>room.Inventory(_=>new InitialCell {Size=56,Version=1,Temperature=double.NaN},true));
            reject(()=>room.Inventory(_=>new InitialCell {Size=56,Version=1,Oxygen=1},true));
        });
        test("configured copies must publish solids as all-zero cells and fluid with positive mass and temperature",()=> {
            var room=ConfiguredDomainLayout.FromSolids(3,2,(dx,dy)=>dx==1 && dy==0);
            Cell Fluid()=>new Cell{Oxygen=.5,InternalEnergy=1e5,Pressure=1e5,Temperature=290};
            var cells=new[]{Fluid(),new Cell(),Fluid(),Fluid(),Fluid(),Fluid()};
            check(ConfiguredDomainLayout.IsZero(cells[1]) && !ConfiguredDomainLayout.IsZero(cells[0]) && room.CopyMatchesMask(cells));
            var leaked=(Cell[])cells.Clone();leaked[1].MomentumY=1e-300;check(!room.CopyMatchesMask(leaked));
            var empty=(Cell[])cells.Clone();empty[0]=new Cell();check(!room.CopyMatchesMask(empty));
            check(!room.CopyMatchesMask(cells.Take(5).ToArray()));
        });
    }
}
