using System;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static int[] clusterProbeCells=Array.Empty<int>();
        private static int clusterOriginalWorld,clusterOtherWorld;
        private static ulong clusterProbeSession,clusterCameraFrame;

        // Exercise each real generated asteroid, rather than counting world
        // metadata while owning only the currently selected camera world.
        private static void SpacedOutWorlds(int frame,OniNativeColony.Status status)
        {
            if(Once(0,"cluster rooms"))
            {
                if(!DlcManager.IsExpansion1Active())throw new InvalidOperationException("Cluster ownership requires actual Spaced Out content.");
                var layout=ColonyOwner.CaptureWorldLayout();
                if(layout.Worlds.Count<2)throw new InvalidOperationException("The cluster control needs multiple generated worlds.");
                clusterOriginalWorld=ClusterManager.Instance.activeWorldId;
                clusterOtherWorld=layout.Worlds.First(w=>w.Id!=clusterOriginalWorld).Id;
                clusterProbeSession=status.Session;
                clusterProbeCells=layout.Worlds.Select(w=>(w.X+w.Width/2)+(w.Y+w.Height/2)*layout.GridWidth).ToArray();
                var tool=CellEventLogger.Instance.SandBoxTool;
                foreach(var cell in clusterProbeCells)
                {
                    Grid.CellToXY(cell,out var x,out var y);
                    FloorArenas.Clear(x-1,y-1,3,3,out _);
                    for(var dy=-1;dy<=1;++dy)for(var dx=-1;dx<=1;++dx)
                        SimMessages.ReplaceElement(Grid.XYToCell(x+dx,y+dy),dx==0&&dy==0?SimHashes.Vacuum:SimHashes.Unobtanium,
                            tool,dx==0&&dy==0?0:20000,dx==0&&dy==0?0:300);
                }
                Record("actual multi-asteroid cluster",true,$"{layout.Worlds.Count} worlds on {layout.GridWidth}x{layout.GridHeight}");
            }
            // Closing the border displaces its original gas into neighbours,
            // including the centre. Clear that completed displacement before
            // applying the declared source, as the normal arena helper does.
            if(Once(4,"clear sealed cluster probes"))
                foreach(var cell in clusterProbeCells)
                    SimMessages.ReplaceElement(cell,SimHashes.Vacuum,CellEventLogger.Instance.SandBoxTool,0,0);
            if(Once(6,"cluster mixtures"))
            {
                var empty=clusterProbeCells.All(cell=>CellMass(cell)==0);
                Record("sealed probes are empty before the source",empty,string.Join("; ",clusterProbeCells.Select(cell=>$"{cell}: {CellMass(cell):R} kg")));
                foreach(var cell in clusterProbeCells)
                {
                    SimMessages.AddRemoveSubstance(cell,SimHashes.Oxygen,CellEventLogger.Instance.OxygenModifierSimUpdate,1,300,byte.MaxValue,0);
                    SimMessages.AddRemoveSubstance(cell,SimHashes.CarbonDioxide,CellEventLogger.Instance.OxygenModifierSimUpdate,.5f,300,byte.MaxValue,0);
                }
            }
            if(Once(14,"cluster ownership"))
            {
                foreach(var cell in clusterProbeCells)
                {
                    var oxygen=SpeciesMass(cell,SimHashes.Oxygen);var carbon=SpeciesMass(cell,SimHashes.CarbonDioxide);
                    Record($"world {Grid.WorldIdx[cell]} owns both constituents",ColonyOwner.OwnsFluidCell(cell)&&Math.Abs(oxygen-1)<1e-6&&Math.Abs(carbon-.5)<1e-6,
                        $"cell {cell}: oxygen {oxygen:R} kg, carbon dioxide {carbon:R} kg");
                }
                var ids=ColonyOwner.CaptureWorldLayout().CellWorldIds();var hole=Array.FindIndex(ids,id=>id<0);
                Record("unallocated cluster cells stay closed",hole>=0&&!ColonyOwner.OwnsFluidCell(hole),$"reserved cell {hole}");
                clusterCameraFrame=status.CompletedFrames;ClusterManager.Instance.SetActiveWorld(clusterOtherWorld);
            }
            if(Once(18,"other world camera"))
            {
                Record("changing worlds retains the running owner",ClusterManager.Instance.activeWorldId==clusterOtherWorld&&
                    status.Session==clusterProbeSession&&status.CompletedFrames>clusterCameraFrame&&clusterProbeCells.All(ColonyOwner.OwnsFluidCell),
                    $"camera world {ClusterManager.Instance.activeWorldId}, session {status.Session}, frame {status.CompletedFrames}");
                ClusterManager.Instance.SetActiveWorld(clusterOriginalWorld);
            }
            if(Once(22,"cluster complete"))
            {
                Record("returning camera preserves cluster ownership",ClusterManager.Instance.activeWorldId==clusterOriginalWorld&&
                    status.Session==clusterProbeSession&&clusterProbeCells.All(ColonyOwner.OwnsFluidCell),$"camera world {ClusterManager.Instance.activeWorldId}");
                Record("cluster mass and energy ledgers close",status.State==1&&status.MaximumRelativeMassError<=1e-12&&status.MaximumRelativeEnergyError<=1e-12,
                    $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
                Finish();
            }
        }
    }
}
