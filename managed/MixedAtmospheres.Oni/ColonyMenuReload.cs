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
        private string guid="",inputHash="";
        internal void Begin(ulong session,uint flags,string colonyGuid,string hash)
        {
            if(Stage!=0 || session==0 || string.IsNullOrEmpty(colonyGuid) || string.IsNullOrEmpty(hash))
                throw new InvalidDataException("Invalid first owner reload session.");
            FirstSession=session;SavedFlags=flags;guid=colonyGuid;inputHash=hash;Stage=1;
        }
        internal void Shutdown() { if(Stage==1) ShutdownObserved=true; }
        internal void MainMenu(bool ownerActive)
        {
            if(Stage!=1) return;
            if(!ShutdownObserved || ownerActive) throw new InvalidDataException("Main menu appeared before owner retirement.");
            MainMenuObserved=true;Stage=2;
        }
        internal void Complete(ulong session,uint flags,string colonyGuid,string hash,int loads)
        {
            if(Stage!=2 || !ShutdownObserved || !MainMenuObserved || session<=FirstSession || flags!=SavedFlags ||
                colonyGuid!=guid || hash!=inputHash || loads!=2)
                throw new InvalidDataException("Owner menu reload did not preserve identity/profile or retire its previous session.");
            Stage=3;
        }
    }
}
