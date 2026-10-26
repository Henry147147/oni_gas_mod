using System;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static GameObject? aquaticReaderCreature;
        // Query controls only: exact bionic tank storage and the remaining
        // ecosystem conversions have their own acceptance requirements.
        private static void DlcFluidReaders(int frame)
        {
            if(Once(0,"DLC mixture query arena"))
            {
                aquaticReaderCreature=null;
                Arena((x,y)=>x==4?(SimHashes.Unobtanium,20000f,300f):
                    x<4?(SimHashes.ChlorineGas,1f,300f):(SimHashes.Oxygen,1.2f,300f));
            }
            if(Once(6,"minority oxygen and gas-visible water"))
            {
                for(var y=0;y<Height;++y)for(var x=0;x<4;++x)
                    SimMessages.AddRemoveSubstance(Cell(x,y),SimHashes.Oxygen,CellEventLogger.Instance.OxygenModifierSimUpdate,.3f,300f,byte.MaxValue,0);
                for(var x=5;x<Width;++x)
                    SimMessages.AddRemoveSubstance(Cell(x,0),SimHashes.Water,CellEventLogger.Instance.OxygenModifierSimUpdate,400f,300f,byte.MaxValue,0);
            }
            if(Once(10,"aquatic pack creature"))
            {
                if(DlcManager.GetActiveDLCIds().Contains("DLC5_ID"))
                {
                    var prefab=Assets.GetPrefab(PufferFishConfig.ID);
                    if(prefab==null)throw new InvalidOperationException("Selected Aquatic content has no puffer fish prefab.");
                    aquaticReaderCreature=Util.KInstantiate(prefab,Grid.CellToPosCBC(Cell(6,0),Grid.SceneLayer.Creatures));
                    aquaticReaderCreature.SetActive(true);
                }
            }
            if(Once(16,"actual bionic refill and aquatic habitat queries"))
            {
                var cell=Cell(2,2);var expected=0f;var hidden=0;
                foreach(var offset in GasBreatherFromWorldProvider.DEFAULT_BREATHABLE_OFFSETS)
                {
                    var sample=Grid.OffsetCell(cell,offset);
                    if(!Grid.IsValidCell(sample)||!Grid.AreCellsInSameWorld(cell,sample))continue;
                    var shown=Grid.Element[sample].HasTag(GameTags.Breathable);
                    var owned=ColonyOwner.TryReadBreathingCell(sample,out var mass,out _);
                    if(owned&&mass>0&&!shown)++hidden;
                    if(owned?mass>0:shown)expected+=1f/GasBreatherFromWorldProvider.DEFAULT_BREATHABLE_OFFSETS.Length;
                }
                var minion=Components.LiveMinionIdentities.Items.FirstOrDefault();
                if(minion==null)throw new InvalidOperationException("Bionic query control needs a live minion brain.");
                var before=ColonyDlcFluidReaders.BionicSamples;
                var flags=AbsorbCellQuery.GetAbsorbOxygenFlags(cell,minion.GetComponent<MinionBrain>(),-1f,out var available,out var ratio,cell);
                Record("bionic refill scoring sees breathable minority oxygen",hidden>0&&expected>0&&available>0&&
                    BitConverter.SingleToInt32Bits(expected)==BitConverter.SingleToInt32Bits(ratio)&&
                    (flags&AbsorbCellQuery.AbsorbOxygenSafeCellFlags.IsBreathable)!=0&&ColonyDlcFluidReaders.BionicSamples>before,
                    $"hidden oxygen samples {hidden}, score {ratio:R}/{expected:R}, available {available:R}kg, flags {flags}");
                var water=Cell(6,0);var has=ColonyOwner.TryReadView(water,out var view);
                Record("aquatic habitat reads physical liquid under a gas projection",has&&!Grid.Element[water].IsLiquid&&
                    view.LiquidFraction>=.35f&&view.LiquidFraction<.5f&&Grid.IsSubstantialLiquid(water)&&
                    Grid.IsSubstantialLiquidUnsafe(water)&&!Grid.IsSubstantialLiquid(water,.5f),
                    $"shown {Grid.Element[water].id}, liquid fraction {view.LiquidFraction:R}, queries {ColonyDlcFluidReaders.AquaticReads}");
                if(DlcManager.GetActiveDLCIds().Contains("DLC5_ID"))
                {
                    var monitor=aquaticReaderCreature==null?null:aquaticReaderCreature.GetSMI<AquaticCreatureSuffocationMonitor.Instance>();
                    Record("Aquatic creature's own suffocation monitor accepts the mixture habitat",monitor!=null&&monitor.CanBreath(),
                        aquaticReaderCreature==null?"creature missing":$"cell {Grid.PosToCell(aquaticReaderCreature)}, monitor present {monitor!=null}");
                }
                if(aquaticReaderCreature!=null)Util.KDestroyGameObject(aquaticReaderCreature);
                Finish();
            }
        }
    }
}
