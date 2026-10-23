using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static int germReferenceFrame=-1;
        // Oracle mode deliberately leaves mixed pipe ownership off. The game's
        // actual ConduitDiseaseManager evolves three native single-element
        // packets; the common helper only predicts and compares each update.
        private static void MixedPipeGermEvolution(int frame)
        {
            if(Once(0,"native integer germ oracle arena")){germReferenceFrame=-1;Arena((x,y)=>(SimHashes.Oxygen,1f,300f));}
            if(Once(5,"three isolated native gas packets"))
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{Cell(1,3)},new List<int>{Cell(3,3)},new List<int>{Cell(5,3)});
            if(Once(10,"seed original native disease evolution"))
            {
                Record("oracle uses actual native pipe evolution",!ColonyConduits.Active,"Mixed pipe ownership must remain off for the oracle.");
                var food=Db.Get().Diseases.GetIndex(Db.Get().Diseases.FoodGerms.id);var slime=Db.Get().Diseases.GetIndex(Db.Get().Diseases.SlimeGerms.id);
                var cold=Game.Instance.gasConduitFlow.AddElement(Cell(1,3),SimHashes.Oxygen,.5f,280,food,16777219);
                var culture=Game.Instance.gasConduitFlow.AddElement(Cell(3,3),SimHashes.ContaminatedOxygen,.5f,293.15f,slime,1000);
                var hot=Game.Instance.gasConduitFlow.AddElement(Cell(5,3),SimHashes.Oxygen,.5f,900,slime,1);
                Record("native oracle packets accept their fixed mass",cold==.5f&&culture==.5f&&hot==.5f,$"{cold:R}/{culture:R}/{hot:R} kg; native IDs {food}/{slime}");
                ColonyGermEvolutionProbe.StartControl();
                Record("isolated positive-mass native equation oracle is seeded",ColonyGermEvolutionProbe.SeedPositiveMassReference(out var detail),detail);
            }
            if(frame>=10&&frame<80&&germReferenceFrame!=frame){germReferenceFrame=frame;ColonyGermEvolutionProbe.TickReference();}
            if(Once(80,"verify native count and binary32 rounding trajectory"))
            {
                ColonyGermEvolutionProbe.Stop();var report=ColonyGermEvolutionProbe.Report;
                Record("common germ step matches actual native updates",ColonyGermEvolutionProbe.PipeSamples>=20&&ColonyGermEvolutionProbe.Diseases==2&&ColonyGermEvolutionProbe.Mismatches==0,report);
                Record("native extinction resets count identity and rounding",ColonyGermEvolutionProbe.Extinctions>0,report);
                Record("isolated native current-mass oracle exercises growth",ColonyGermEvolutionProbe.ReferenceSamples>=20&&ColonyGermEvolutionProbe.ReferenceBirths>0&&ColonyGermEvolutionProbe.Deaths>0,report);
                Record("fixed oracle remains in supported integer domain",ColonyGermEvolutionProbe.Unsupported==0,report);Finish();
            }
        }
    }
}
