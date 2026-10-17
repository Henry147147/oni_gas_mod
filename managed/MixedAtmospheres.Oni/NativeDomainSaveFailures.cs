using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using UnityEngine;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        internal sealed class ColonySaveUnavailableException : Exception
        {
            internal string UserMessage {get;}
            internal ColonySaveUnavailableException(string detail,bool actors):base(detail)
            {UserMessage=actors ? "Saving this development colony requires removing the active fluid actors first." :
                "The colony could not be saved. Let the simulation finish its current work, then try again.";}
        }
        private static ConfirmDialogScreen? saveProblemDialog;
        private static int declinedColonySaves,unsuccessfulColonyWrites;
        private static string lastSaveProblem="";
        private static bool ioSaveControlVerified;
        private static ulong ioSaveControlFrame;
        private static int ioSaveControlStage,ioSaveInitialPauseDepth,ioSavePriorAdmissions,ioSavePriorProblems,ioSavePriorDeclines,ioSaveDialogFrame;
        private static double ioSaveSourceTime;
        private static byte[]? ioSaveSourceImage;
        private static string ioSavePriorSelection="",ioSaveBlockedPath="",ioSaveBeforeHash="",ioSaveAfterHash="";
        private static bool ioSaveNativeDialogObserved,ioSaveDraggingRestored;
        private static string ioSaveNativeDialogText="";
        private static Game? ioSaveGame;
        private static Game.SavingPreCB? ioSavePreviousPre;
        private static Game.SavingActiveCB? ioSavePreviousActive;
        private static Game.SavingPostCB? ioSavePreviousPost;
        private static int ioSavePreCallbacks,ioSaveActiveCallbacks,ioSavePostCallbacks;
        private static bool IoSaveControlRequired=>ColonyRuntimeValidation.Settings?.VerifySaveIoRecovery==true;
        private static bool HoldIoControlPause=>IoSaveControlRequired && ioSaveControlStage>0 && ioSaveControlStage<3;
        private static HashSet<int> ActiveSaveDialogs()=>new HashSet<int>(Resources.FindObjectsOfTypeAll<ConfirmDialogScreen>()
            .Where(screen=>screen.gameObject.activeInHierarchy && screen.IsActive()).Select(screen=>screen.GetInstanceID()));
        private static void ShowColonySaveProblem(string detail,string message,HashSet<int>? previousDialogs=null)
        {
            lastSaveProblem=detail;
            // ONI can already have displayed its ordinary disk-write dialog.
            // Preserve that screen instead of layering a second modal over it.
            var existing=previousDialogs==null ? null : Resources.FindObjectsOfTypeAll<ConfirmDialogScreen>().FirstOrDefault(screen=>
                screen.gameObject.activeInHierarchy && screen.IsActive() && !previousDialogs.Contains(screen.GetInstanceID()));
            if(existing!=null) {saveProblemDialog=existing;return;}
            saveProblemDialog=(ConfirmDialogScreen)GameScreenManager.Instance.StartScreen(
                ScreenPrefabs.Instance.ConfirmDialogScreen.gameObject,GameScreenManager.Instance.ssOverlayCanvas.gameObject);
            saveProblemDialog.PopupConfirmDialog(message,
                null,null,title_text:"COLONY NOT SAVED");
        }
        internal static void DeclineColonySave(ColonySaveUnavailableException problem)
        {
            ++declinedColonySaves;ShowColonySaveProblem(problem.Message,problem.UserMessage);
            Debug.LogWarning("[Mixed Atmospheres] Colony save declined before serialization; the current colony is retained.");
        }
        private static void RecordUnsuccessfulColonyWrite(ColonySaveLease lease,Exception problem)
        {
            ++unsuccessfulColonyWrites;colonyFileSaved=false;
            SaveLoader.SetActiveSaveFilePath(lease.PreviousSavePath);
            ShowColonySaveProblem(problem.ToString(),"The colony could not be written. Check the save destination and available disk space, then try again.",lease.PreviousDialogs);
            Debug.LogWarning("[Mixed Atmospheres] Colony save declined after write verification; the prior save selection is retained.");
        }
        private static void AcknowledgePrivateSaveProblem()
        {
            // Only the disposable control calls this. Normal play leaves the
            // actual save dialog to the player.
            var dialog=saveProblemDialog;
            Require(dialog!=null && dialog.gameObject.activeInHierarchy && dialog.IsActive(),"The controlled save decline did not display its dialog.");
            saveProblemDialog=null;dialog!.OnSelect_OK();
        }
        private static void VerifyIoSaveRecovery()
        {
            if(!IoSaveControlRequired || ioSaveControlVerified) return;
            if(ioSaveControlStage==0)
            {
                Require(rejectedSaveVerified && !SpeedControlScreen.Instance.IsPaused,"The write-recovery control requires a resumed pending-command control.");
                ioSaveInitialPauseDepth=PauseDepth();SpeedControlScreen.Instance.Pause(false);
                AccessTools.Method(typeof(Game),"UnsafeSim200ms").Invoke(Game.Instance,new object[]{0f});Capture();
                Require(TryCapturePhysicalCheckpoint(out var before),"Write-recovery source was not drained.");
                ioSaveControlFrame=latest.CompletedFrames;ioSaveSourceTime=latest.TimeSeconds;ioSaveSourceImage=before;ioSaveBeforeHash=CheckpointHash(before);
                ioSavePriorSelection=SaveLoader.GetActiveSaveFilePath();
                ioSaveBlockedPath=Path.Combine(Path.GetDirectoryName(ioSavePriorSelection)!,"ma-private-unwritable-save.sav");
                Require(!File.Exists(ioSaveBlockedPath) && !Directory.Exists(ioSaveBlockedPath),"Write-recovery destination already exists.");
                Directory.CreateDirectory(ioSaveBlockedPath);File.WriteAllText(Path.Combine(ioSaveBlockedPath,"retained.txt"),"retained private save target");
                ioSavePriorAdmissions=automaticSaveAdmissions;ioSavePriorProblems=unsuccessfulColonyWrites;ioSavePriorDeclines=declinedColonySaves;
                ioSaveGame=Game.Instance;
                ioSavePreviousPre=(Game.SavingPreCB?)AccessTools.Field(typeof(Game),"activatePreCB").GetValue(ioSaveGame);
                ioSavePreviousActive=(Game.SavingActiveCB?)AccessTools.Field(typeof(Game),"activateActiveCB").GetValue(ioSaveGame);
                ioSavePreviousPost=(Game.SavingPostCB?)AccessTools.Field(typeof(Game),"activatePostCB").GetValue(ioSaveGame);
                ioSaveGame.SetAutoSaveCallbacks(callback=>
                {
                    ++ioSavePreCallbacks;if(ioSavePreviousPre!=null) ioSavePreviousPre(callback);else callback();
                },()=>{++ioSaveActiveCallbacks;ioSavePreviousActive?.Invoke();},()=>{++ioSavePostCallbacks;ioSavePreviousPost?.Invoke();});
                ioSaveControlStage=1;Game.Instance.StartDelayedSave(ioSaveBlockedPath,false,true);return;
            }
            if(unsuccessfulColonyWrites==ioSavePriorProblems)
            {
                if(declinedColonySaves>ioSavePriorDeclines)
                {AcknowledgePrivateSaveProblem();throw new InvalidOperationException("Native paired inventory was rejected before the intended IO control: "+lastSaveProblem);}
                WriteReport();return;
            }
            if(ioSaveControlStage==1)
            {
                var dialog=saveProblemDialog;
                Require(dialog!=null,"The controlled disk write did not retain its actual save dialog.");
                var text=((LocText)AccessTools.Field(typeof(ConfirmDialogScreen),"popupMessage").GetValue(dialog)).text;
                ioSaveNativeDialogText=text;
                var ioMessage=string.Format(STRINGS.UI.CRASHSCREEN.SAVEFAILED,"IOException. You may not have enough free space!");
                var deniedMessage=string.Format(STRINGS.UI.CRASHSCREEN.SAVEFAILED,"Unauthorized Access Exception");
                ioSaveNativeDialogObserved=text==ioMessage || text==deniedMessage;
                Require(ioSaveNativeDialogObserved,"The write-recovery control did not exercise ONI's real IO-decline path.");
                AcknowledgePrivateSaveProblem();ioSaveDialogFrame=Time.frameCount;ioSaveControlStage=2;return;
            }
            // Let ONI finish the five post-save coroutine yields itself.
            if(Time.frameCount-ioSaveDialogFrame<8) return;
            Capture();Require(TryCapturePhysicalCheckpoint(out var after),"The declined write left a pending owner transaction.");
            ioSaveAfterHash=CheckpointHash(after);
            ioSaveDraggingRestored=(bool)AccessTools.Field(typeof(PlayerController),"draggingAllowed").GetValue(PlayerController.Instance);
            ioSaveControlVerified=SaveLoader.GetActiveSaveFilePath()==ioSavePriorSelection &&
                unsuccessfulColonyWrites==ioSavePriorProblems+1 && automaticSaveAdmissions==ioSavePriorAdmissions+1 &&
                !colonyFileSaved && !savedFileReadbackVerified && stagedColonySave==null &&
                latest.State==1 && latest.CompletedFrames==ioSaveControlFrame && latest.TimeSeconds==ioSaveSourceTime &&
                after.SequenceEqual(ioSaveSourceImage!) && ioSaveDraggingRestored &&
                ioSavePreCallbacks==1 && ioSaveActiveCallbacks==1 && ioSavePostCallbacks==1 &&
                File.ReadAllText(Path.Combine(ioSaveBlockedPath,"retained.txt"))=="retained private save target" && PauseDepth()==ioSaveInitialPauseDepth+1;
            Require(ioSaveControlVerified,"Declined disk write changed its target, selection, physical owner or retained save/pause resources.");
            ioSaveSourceImage=null;ioSaveControlStage=3;ReleasePrivateSaveCallbacks();SpeedControlScreen.Instance.Unpause(false);
            Require(PauseDepth()==ioSaveInitialPauseDepth,"The write-recovery control did not restore the running colony.");
        }
        private static void ReleasePrivateSaveCallbacks()
        {
            if(ioSaveGame!=null) ioSaveGame.SetAutoSaveCallbacks(ioSavePreviousPre,ioSavePreviousActive,ioSavePreviousPost);
            ioSaveGame=null;ioSavePreviousPre=null;ioSavePreviousActive=null;ioSavePreviousPost=null;
        }
        private static object SaveProblemReport()=>new {DeclinedBeforeSerialization=declinedColonySaves,UnsuccessfulWrites=unsuccessfulColonyWrites,
            LastDiagnostic=lastSaveProblem,IoControlRequired=IoSaveControlRequired,IoControlVerified=ioSaveControlVerified,
            IoControlFrame=ioSaveControlFrame,IoControlStage=ioSaveControlStage,NativeIoDialogObserved=ioSaveNativeDialogObserved,
            NativeIoDialogText=ioSaveNativeDialogText,
            DraggingRestored=ioSaveDraggingRestored,BeforePhysicalSha256=ioSaveBeforeHash,AfterPhysicalSha256=ioSaveAfterHash,
            PreCallbacks=ioSavePreCallbacks,ActiveCallbacks=ioSaveActiveCallbacks,PostCallbacks=ioSavePostCallbacks,
            AdvancedAfterIoControl=ioSaveControlVerified && latest.CompletedFrames>ioSaveControlFrame};
    }
}
