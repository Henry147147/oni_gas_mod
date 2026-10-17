using System;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        // This workflow spans two native/managed colony sessions. Its evidence
        // survives native shutdown; physical owner state must not survive it.
        private sealed class ReloadWorkflow
        {
            internal int Stage,ProcessId;
            internal ulong FirstSession,FirstFrame;
            internal byte[] FirstImage=Array.Empty<byte>();
            internal string InputPath="",InputHash="",FirstReportHash="",FirstGuid="";
            internal bool ShutdownObserved,MainMenuObserved,MatchingFrameVerified,Verified,FirstAutosaveVerified;
            internal ulong MatchedSession;
        }
        private static readonly ReloadWorkflow reloadWorkflow=new ReloadWorkflow();
        private static bool ReloadRequested=>ColonyRuntimeValidation.Settings?.ReloadInProcess==true;
        internal static string ReloadScreenshotName(string name)=>ReloadRequested && reloadWorkflow.Stage==0 && name=="native-domain.png" ? "first-load-"+name : name;
        private static string ReloadSaveName=>ReloadRequested && reloadWorkflow.Stage>=2 ? "resaved-colony-reload-2.sav" : "resaved-colony.sav";
        internal static bool ContinueInProcessReload()
        {
            if(!ReloadRequested) return false;
            Require(ColonyRestoreMode && completed && colonyLoaded && !failed,"In-process reload requires a completed saved-colony leg.");
            if(reloadWorkflow.Stage==0)
            {
                Require(frameZeroResaveVerified,"First reload leg has no verified paused save.");
                Require(TryCapturePhysicalCheckpoint(out var image),"First reload leg has no complete physical image.");
                reloadWorkflow.ProcessId=Process.GetCurrentProcess().Id;
                reloadWorkflow.FirstSession=latest.Session;reloadWorkflow.FirstFrame=latest.CompletedFrames;
                reloadWorkflow.FirstImage=image;reloadWorkflow.FirstGuid=SaveLoader.Instance.GameInfo.colonyGuid.ToString("N");
                reloadWorkflow.FirstAutosaveVerified=frameZeroResaveVerified;
                reloadWorkflow.InputPath=ColonyRuntimeValidation.Settings!.LoadSavePath;
                reloadWorkflow.InputHash=CheckpointHash(File.ReadAllBytes(reloadWorkflow.InputPath));
                var report=File.ReadAllBytes(ReportPath);reloadWorkflow.FirstReportHash=CheckpointHash(report);
                File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory,"first-load-native-domain.json"),report);
                File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory,"first-load-physical.bin"),image);
                reloadWorkflow.Stage=1;return true;
            }
            Require(reloadWorkflow.Stage==2 && reloadWorkflow.ShutdownObserved && reloadWorkflow.MainMenuObserved &&
                reloadWorkflow.MatchingFrameVerified && reloadWorkflow.MatchedSession==latest.Session && latest.Session>reloadWorkflow.FirstSession &&
                Process.GetCurrentProcess().Id==reloadWorkflow.ProcessId && frameZeroResaveVerified &&
                SaveLoader.Instance.GameInfo.colonyGuid.ToString("N")==reloadWorkflow.FirstGuid &&
                CheckpointHash(File.ReadAllBytes(reloadWorkflow.InputPath))==reloadWorkflow.InputHash,
                "Same-process main-menu reload failed session retirement, exact physical continuation, identity or input isolation.");
            reloadWorkflow.Verified=true;reloadWorkflow.Stage=3;WriteReport();return false;
        }
        internal static void ObserveReloadMainMenu()
        {
            if(!ReloadRequested || reloadWorkflow.Stage!=1) return;
            Require(reloadWorkflow.ShutdownObserved && !active && !arming,"Main menu appeared before the previous native owner was retired.");
            reloadWorkflow.MainMenuObserved=true;reloadWorkflow.Stage=2;
            ColonyRuntimeValidation.Settings!.CompletedFrames=checked((int)reloadWorkflow.FirstFrame);
        }
        private static void ObserveReloadShutdown()
        {
            if(ReloadRequested && reloadWorkflow.Stage==1) reloadWorkflow.ShutdownObserved=true;
        }
        private static void ObserveReloadPhysicalFrame(byte[] image)
        {
            if(!ReloadRequested || reloadWorkflow.Stage!=2 || latest.CompletedFrames!=reloadWorkflow.FirstFrame) return;
            Require(latest.Session>reloadWorkflow.FirstSession && image.SequenceEqual(reloadWorkflow.FirstImage),
                "The reloaded native owner differs from the first session at the same completed physical frame.");
            reloadWorkflow.MatchingFrameVerified=true;reloadWorkflow.MatchedSession=latest.Session;
        }
        private static object? InProcessReloadReport()=>!ReloadRequested ? null : new {
            Requested=true,reloadWorkflow.Verified,reloadWorkflow.Stage,
            FirstProcessId=reloadWorkflow.ProcessId,CurrentProcessId=Process.GetCurrentProcess().Id,
            FirstNativeSession=reloadWorkflow.FirstSession,CurrentNativeSession=latest.Session,
            FirstCompletedFrame=reloadWorkflow.FirstFrame,reloadWorkflow.MatchedSession,
            reloadWorkflow.ShutdownObserved,reloadWorkflow.MainMenuObserved,reloadWorkflow.MatchingFrameVerified,
            reloadWorkflow.FirstAutosaveVerified,FirstPhysicalSha256=CheckpointHash(reloadWorkflow.FirstImage),
            FirstReportSha256=reloadWorkflow.FirstReportHash,InputSha256=reloadWorkflow.InputHash,
            InputUnchanged=reloadWorkflow.InputPath.Length!=0 && File.Exists(reloadWorkflow.InputPath) &&
                CheckpointHash(File.ReadAllBytes(reloadWorkflow.InputPath))==reloadWorkflow.InputHash,
            WholeColonyFluidAuthority=false
        };
    }
}
