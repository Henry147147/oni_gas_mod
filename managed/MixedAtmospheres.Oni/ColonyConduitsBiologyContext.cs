using System;
using System.Collections.Generic;
using System.IO;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Native culture allocation/cache fields are public managed data. Restore
    // them once on the replacement row, preserving its fresh temperature
    // handle. Subsequent native SetData calls keep their original ID-triggered
    // cache refresh behavior, including changes between biology ticks.
    internal static class ColonyPipeCultureState
    {
        private static readonly System.Reflection.FieldInfo Rows=AccessTools.Field(typeof(ConduitDiseaseManager),"data");
        private static readonly Dictionary<(ConduitFlow,int),PipeNativeCulture> Pending=new Dictionary<(ConduitFlow,int),PipeNativeCulture>();
        private static readonly Dictionary<(ConduitFlow,int),PipeNativeCulture> RestoredContexts=new Dictionary<(ConduitFlow,int),PipeNativeCulture>();
        private static int savedContexts,boundContexts,massCorrections,cacheCorrections,capturedContexts,captureMissingPrefabs,restoreMissingPrefabs;
        private static bool restored,coverageVerified;
        internal static bool NeedsReadyBarrier=>restored&&!coverageVerified;
        internal static object Report()=>new{Saved=savedContexts,Bound=boundContexts,Pending=Pending.Count,AllocationMassCorrections=massCorrections,CacheCorrections=cacheCorrections,
            CapturedContexts=capturedContexts,CaptureMissingPrefabs=captureMissingPrefabs,RestoreMissingPrefabs=restoreMissingPrefabs,CoverageVerified=coverageVerified};
        internal static void Reset(){Pending.Clear();RestoredContexts.Clear();savedContexts=boundContexts=massCorrections=cacheCorrections=capturedContexts=captureMissingPrefabs=restoreMissingPrefabs=0;restored=coverageVerified=false;}
        internal static List<PipeNativeCulture> CaptureAll()
        {
            RequireReady();
            var captured=new Dictionary<(ConduitFlow,int),PipeNativeCulture>(Pending);
            foreach(var sample in ColonyConduits.NativeCultureCells())
                if(!captured.ContainsKey(sample))
                    captured.Add(sample,Capture(sample.Flow.conduitType==ConduitType.Gas?(byte)1:(byte)2,sample.Cell,
                        sample.Flow.soaInfo.GetDiseaseData(sample.Flow.GetConduit(sample.Cell).idx)));
            capturedContexts=captured.Count;captureMissingPrefabs=0;
            foreach(var pair in captured)if(!PrefabExists(pair.Key.Item1,pair.Key.Item2))++captureMissingPrefabs;
            return new List<PipeNativeCulture>(captured.Values);
        }
        internal static void RestoreAll(IReadOnlyList<PipeNativeCulture> contexts)
        {
            if(Pending.Count!=0)throw new InvalidOperationException("Native pipe cultures already await binding.");
            var prepared=new Dictionary<(ConduitFlow,int),PipeNativeCulture>();
            foreach(var context in contexts)
            {
                if(context.CapturedElement>=ElementLoader.elements.Count||context.Disease!=byte.MaxValue&&context.Disease>=Db.Get().Diseases.Count)
                    throw new InvalidDataException("A saved pipe culture refers to an unknown native element or disease.");
                var flow=context.Phase==1?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                if(prepared.ContainsKey((flow,context.Cell)))throw new InvalidDataException("Duplicate saved native pipe culture.");
                prepared.Add((flow,context.Cell),context);
            }
            // GameStarted can precede conduit prefab/network registration.
            // Queue the exact saved context now; require complete native and
            // prefab coverage at the verified first-positive-time barrier.
            foreach(var pair in prepared){Pending.Add(pair.Key,pair.Value);RestoredContexts.Add(pair.Key,pair.Value);if(!PrefabExists(pair.Key.Item1,pair.Key.Item2))++restoreMissingPrefabs;}
            savedContexts=prepared.Count;restored=true;
        }
        private static bool PrefabExists(ConduitFlow flow,int cell)
        {
            var layer=flow.conduitType==ConduitType.Gas?ObjectLayer.GasConduit:ObjectLayer.LiquidConduit;
            var pipe=Grid.Objects[cell,(int)layer];return pipe!=null&&pipe.GetComponent<Conduit>()!=null;
        }
        internal static void RequireReady()
        {
            if(!NeedsReadyBarrier)return;
            ColonyConduitsSchedule.FlushTopology();
            Bind(Game.Instance.conduitDiseaseManager);
            foreach(var pair in RestoredContexts)
            {
                var flow=pair.Key.Item1;var cell=pair.Key.Item2;
                if(!PrefabExists(flow,cell)||!flow.HasConduit(cell))
                    throw new InvalidDataException($"Saved native pipe culture {cell} has no registered restored pipe at the first positive-time boundary.");
                var current=Capture(pair.Value.Phase,cell,flow.soaInfo.GetDiseaseData(flow.GetConduit(cell).idx));
                if(!pair.Value.SameBits(current))throw new InvalidDataException($"Native pipe culture {cell} changed after topology settlement.");
            }
            if(Pending.Count!=0)throw new InvalidDataException("Saved native pipe cultures have not all bound before positive simulation time.");
            // Include empty pipes rather than inferring complete coverage
            // from the checkpoint's nonempty inventory cells.
            var geometry=ColonyConduits.Geometry;
            foreach(var phase in new byte[]{1,2})
            {
                var flow=phase==1?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                var layer=phase==1?ObjectLayer.GasConduit:ObjectLayer.LiquidConduit;
                for(var y=geometry.Y;y<geometry.Y+geometry.Height;++y)
                    for(var x=geometry.X;x<geometry.X+geometry.Width;++x)
                    {
                        var cell=checked((int)(y*geometry.GridWidth+x));var pipe=Grid.Objects[cell,(int)layer];
                        if(pipe!=null&&pipe.GetComponent<Conduit>()!=null&&!RestoredContexts.ContainsKey((flow,cell)))
                            throw new InvalidDataException($"Restored pipe {cell} has no saved native culture context.");
                    }
            }
            coverageVerified=true;
        }
        // A load recreates native handles after prefab spawn. Bind a saved
        // context before its row's first disease update; retaining unbound
        // contexts also permits an immediate paused save before network build.
        private static void Bind(ConduitDiseaseManager manager)
        {
            if(Pending.Count==0)return;
            var rows=(List<ConduitDiseaseManager.Data>)Rows.GetValue(manager);
            var byHandle=new Dictionary<object,int>();
            for(var row=0;row<rows.Count;++row)
            {object handle=rows[row].temperatureHandle;if(byHandle.ContainsKey(handle))byHandle[handle]=-1;else byHandle.Add(handle,row);}
            foreach(var pair in new List<KeyValuePair<(ConduitFlow,int),PipeNativeCulture>>(Pending))
            {
                var flow=pair.Key.Item1;var cell=pair.Key.Item2;if(!flow.HasConduit(cell))continue;
                var data=flow.soaInfo.GetDiseaseData(flow.GetConduit(cell).idx);
                if(!byHandle.TryGetValue(data.temperatureHandle,out var row))continue;
                if(row<0)throw new InvalidDataException("A restored pipe culture has an ambiguous native temperature handle.");
                var before=rows[row];var restored=Restore(pair.Value,before);
                if(BitConverter.SingleToInt32Bits(before.mass)!=BitConverter.SingleToInt32Bits(restored.mass))++massCorrections;
                if(before.elemIdx!=restored.elemIdx||!Capture(pair.Value.Phase,cell,before).Growth.SameBits(pair.Value.Growth))++cacheCorrections;
                rows[row]=restored;Pending.Remove(pair.Key);++boundContexts;
            }
        }
        internal static PipeNativeCulture Capture(byte phase,int cell,ConduitDiseaseManager.Data data)
        {
            var g=data.growthInfo;
            return new PipeNativeCulture(phase,cell,data.elemIdx,data.mass,data.diseaseIdx,data.diseaseCount,data.accumulatedError,
                new PipeGrowthRule(g.underPopulationDeathRate,g.populationHalfLife,g.overPopulationHalfLife,g.diffusionScale,
                    g.minCountPerKG,g.maxCountPerKG,g.minDiffusionCount,g.minDiffusionInfestationTickCount));
        }
        internal static ConduitDiseaseManager.Data Restore(PipeNativeCulture saved,ConduitDiseaseManager.Data live)
        {
            if(live.diseaseIdx!=saved.Disease||live.diseaseCount!=saved.Count)
                throw new InvalidDataException($"Restored pipe {saved.Cell} germ projection differs from its saved culture.");
            var g=saved.Growth;
            live.elemIdx=saved.CapturedElement;live.mass=saved.CapturedMass;live.accumulatedError=saved.Residue;
            live.growthInfo=new Klei.AI.DiseaseGrowthRules.ElemGrowthInfo{
                underPopulationDeathRate=g.UnderPopulationDeathRate,populationHalfLife=g.PopulationHalfLife,overPopulationHalfLife=g.OverPopulationHalfLife,
                diffusionScale=g.DiffusionScale,minCountPerKG=g.MinimumCountPerKg,maxCountPerKG=g.MaximumCountPerKg,
                minDiffusionCount=g.MinimumDiffusionCount,minDiffusionInfestationTickCount=g.MinimumDiffusionTicks};
            return live;
        }
    }
}
