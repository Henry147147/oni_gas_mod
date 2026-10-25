using System;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class ColonyRuntimeValidation
    {
        private static bool inactiveBaselineSet;
        private static void ColonyOwnerInactiveScene()
        {
            if(ColonyOwner.Active)throw new InvalidOperationException("The unsupported colony activated its fluid owner.");
            if(ColonyOwner.StartFailure.Length==0)return;
            if(!DlcManager.IsExpansion1Active()||ColonyOwner.StartFailure!="Spaced Out! is not supported yet")
                throw new InvalidOperationException("Unexpected owner refusal: "+ColonyOwner.StartFailure);
            var observation=NativeBackendObservation.Capture();
            var updates=observation.Hooks.Single(hook=>hook.Name=="update-data").Entries;
            if(!inactiveBaselineSet)
            {
                observerTimestepBaseline=observation.PositiveTimestepReturns;observerUpdateBaseline=updates;inactiveBaselineSet=true;
            }
            Report.Kind="actual-game-unsupported-colony-forwarding-validation";
            Report.PhysicsOwner="native ONI; colony owner refused Spaced Out! with the Mixed Atmospheres inactive notice";
            Report.ObserverTimestepReturns=observation.PositiveTimestepReturns-observerTimestepBaseline;
            Report.ObserverPhysicalUpdates=updates-observerUpdateBaseline;
            Report.NativeObserverForwardingPassed=observation.Attached&&!observation.RecoveryRequired&&!observation.AuthoritativeFluidOwner;
            Report.Width=Grid.WidthInCells;Report.Height=Grid.HeightInCells;
            Report.WorldCount=ClusterManager.Instance.WorldContainers.Count;
            Report.Duplicants=Components.LiveMinionIdentities.Count;
            if(!Report.NativeObserverForwardingPassed)throw new InvalidOperationException("Native forwarding failed after owner refusal.");
            if(Report.ObserverTimestepReturns<(ulong)configuration!.CompletedFrames||Report.ObserverPhysicalUpdates<(ulong)configuration.CompletedFrames)
            {WriteProgressReport();return;}
            Report.ActiveDlcIds=SaveLoader.Instance.GameInfo.dlcIds.ToArray();
            Report.Status="passed";Report.Stage="completed";Report.FinishedUtc=System.DateTime.UtcNow.ToString("O");
            done=true;WriteReport();CaptureScreenshot("colony.png");
        }
    }
}
