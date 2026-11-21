using System;
using System.Threading;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Pack readers share the owner's existing breathing rules and physical
    // liquid occupancy. Native recipe rates and chore/survival timers stay
    // in their original components.
    internal static class ColonyDlcFluidReaders
    {
        private static long aquaticReads,bionicSamples;
        internal static long AquaticReads=>Interlocked.Read(ref aquaticReads);
        internal static long BionicSamples=>Interlocked.Read(ref bionicSamples);
        internal static void Reset(){Interlocked.Exchange(ref aquaticReads,0);Interlocked.Exchange(ref bionicSamples,0);}
        internal static bool Liquid(int cell,float threshold,out bool result)
        {
            result=false;
            if(!ColonyOwner.TryReadView(cell,out var view))return false;
            Interlocked.Increment(ref aquaticReads);
            // A visible gas can coexist with substantial liquid. All liquid
            // phases contribute to the published physical volume fraction.
            result=(view.Flags&OniNativeColony.ViewLiquid)!=0&&view.LiquidFraction>=threshold;
            return true;
        }
        internal static void RefillRatio(int cell,MinionBrain brain,ref float ratio)
        {
            if(!ColonyOwner.Active||brain==null||brain.OxygenBreather==null||!Grid.IsValidCell(cell))return;
            var head=Grid.CellAbove(cell);
            if(!Grid.IsValidCell(head)||Grid.Solid[cell]||Grid.Solid[head]||Grid.IsTileUnderConstruction[cell]||Grid.IsTileUnderConstruction[head])return;
            var offsets=GasBreatherFromWorldProvider.DEFAULT_BREATHABLE_OFFSETS;
            var value=0f;var changed=false;
            foreach(var offset in offsets)
            {
                var sample=Grid.OffsetCell(cell,offset);
                if(!Grid.IsValidCell(sample)||!Grid.AreCellsInSameWorld(cell,sample))continue;
                bool breathable;
                if(ColonyOwner.TryReadBreathingCell(sample,out var mass,out _))
                {changed=true;Interlocked.Increment(ref bionicSamples);breathable=mass>0;}
                else breathable=Grid.Element[sample].HasTag(GameTags.Breathable);
                // Retain the game's denominator and binary32 addition order,
                // including invalid/foreign cells in the denominator.
                if(breathable)value+=1f/offsets.Length;
            }
            if(changed)ratio=value;
        }
    }
    // Pacu variants share the flopping chore but lack the aquatic survival
    // monitor. Its normal timer also handles rescue, storage and save/load.
    [HarmonyPatch(typeof(BasePacuConfig), nameof(BasePacuConfig.CreatePrefab))]
    internal static class PacuSurvivalPatch
    {
        private static void Postfix(UnityEngine.GameObject __result)
        {
            if (ColonyOwner.Available && GameplayPacks.ActiveUnsupported().Length == 0)
                __result.AddOrGetDef<AquaticCreatureSuffocationMonitor.Def>();
        }
    }

    [HarmonyPatch(typeof(Grid),nameof(Grid.IsSubstantialLiquidUnsafe))]
    internal static class ColonyAquaticHabitatPatch
    {
        private static bool Prefix(int cell,float threshold,ref bool __result)
        {
            try {if(!ColonyDlcFluidReaders.Liquid(cell,threshold,out var liquid))return true;__result=liquid;return false;}
            catch(Exception error){ColonyFault.Raise("aquatic liquid habitat reader",error);__result=false;return false;}
        }
    }
    [HarmonyPatch(typeof(AbsorbCellQuery),nameof(AbsorbCellQuery.GetAbsorbOxygenFlags))]
    internal static class ColonyBionicRefillScorePatch
    {
        private static void Postfix(int cell,MinionBrain brain,ref float breathableCellRatioInSample)
        {
            try {ColonyDlcFluidReaders.RefillRatio(cell,brain,ref breathableCellRatioInSample);}
            catch(Exception error){ColonyFault.Raise("bionic oxygen refill reader",error);breathableCellRatioInSample=0;}
        }
    }
}
