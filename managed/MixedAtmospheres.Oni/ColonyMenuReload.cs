using System;
using System.IO;

namespace MixedAtmospheres.Oni
{
    internal sealed class ColonyMenuReload
    {
        internal int Stage { get; private set; }
        internal ulong FirstSession { get; private set; }
        internal uint SavedFlags { get; private set; }
        internal bool ShutdownObserved { get; private set; }
        internal bool MainMenuObserved { get; private set; }
        internal bool Verified => Stage==3;
        // The first session's restored physical profile and owner image: an
        // identical saved input must restore identical bytes the second time.
        internal ColonyPhysicalProfile RestoredProfile { get; private set; }
        internal string RestoredImageSha256 { get; private set; } = "";
        private string guid="",inputHash="";
        internal void Begin(ulong session,uint flags,string colonyGuid,string hash,ColonyPhysicalProfile? profile,string restoredImage)
        {
            if(Stage!=0 || session==0 || string.IsNullOrEmpty(colonyGuid) || string.IsNullOrEmpty(hash) || profile==null || string.IsNullOrEmpty(restoredImage))
                throw new InvalidDataException("Invalid first owner reload session.");
            FirstSession=session;SavedFlags=flags;guid=colonyGuid;inputHash=hash;RestoredProfile=profile.Value;RestoredImageSha256=restoredImage;Stage=1;
        }
        internal void Shutdown() { if(Stage==1) ShutdownObserved=true; }
        internal void MainMenu(bool ownerActive)
        {
            if(Stage!=1) return;
            if(!ShutdownObserved || ownerActive) throw new InvalidDataException("Main menu appeared before owner retirement.");
            MainMenuObserved=true;Stage=2;
        }
        internal void Complete(ulong session,uint flags,string colonyGuid,string hash,int loads,ColonyPhysicalProfile? profile,string restoredImage)
        {
            if(Stage!=2 || !ShutdownObserved || !MainMenuObserved || session<=FirstSession || flags!=SavedFlags ||
                colonyGuid!=guid || hash!=inputHash || loads!=2 || profile==null || !profile.Value.Equals(RestoredProfile) || restoredImage!=RestoredImageSha256)
                throw new InvalidDataException("Owner menu reload did not preserve identity/profile or retire its previous session.");
            Stage=3;
        }
    }
}
