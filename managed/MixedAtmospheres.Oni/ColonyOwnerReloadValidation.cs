using System;
using System.IO;
using System.Security.Cryptography;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class ColonyRuntimeValidation
    {
        private static readonly ColonyMenuReload ownerReload=new ColonyMenuReload();
        private static bool OwnerReloadRequested => configuration?.Mode=="colony-owner-load" && configuration.ReloadInProcess;
        private static string OwnerReloadInputHash()
        {using var hash=SHA256.Create();return BitConverter.ToString(hash.ComputeHash(File.ReadAllBytes(configuration!.LoadSavePath))).Replace("-","").ToLowerInvariant();}
        private static object? OwnerReloadReport() => !OwnerReloadRequested ? null : new {
            Requested=true,ownerReload.Stage,ownerReload.Verified,ownerReload.FirstSession,
            CurrentSession=ColonyOwner.Status.Session,ownerReload.SavedFlags,CurrentFlags=ColonyOwner.Config.Flags,
            ownerReload.ShutdownObserved,ownerReload.MainMenuObserved,Loads=ColonyOwnerPersistence.Loads,
            FirstRestoredImageSha256=ownerReload.RestoredImageSha256,CurrentRestoredImageSha256=ColonyOwnerPersistence.RestoredImageSha256,
            Scope="owner teardown, source identity, restored physical profile and byte-identical restored owner image; not an input-transcript or in-flight proof"
        };
        private static bool ContinueOwnerMenuReload()
        {
            if(!OwnerReloadRequested) { RefreshOwnerCompletionReport(); return false; }
            var guid=SaveLoader.Instance.GameInfo.colonyGuid.ToString("N");var hash=OwnerReloadInputHash();
            if(ownerReload.Stage==0)
            {
                ownerReload.Begin(ColonyOwner.Status.Session,ColonyOwner.Config.Flags,guid,hash,ColonyOwnerPersistence.RestoredProfile,ColonyOwnerPersistence.RestoredImageSha256);
                RefreshOwnerCompletionReport();
                ReportJson.WriteAtomic(Path.Combine(OutputDirectory,"first-load-colony-validation.json"),Report);
                File.Copy(Path.Combine(OutputDirectory,"colony-owner.json"),Path.Combine(OutputDirectory,"first-load-colony-owner.json"),false);
                Report.Status="pending";Report.Stage="returning-to-main-menu-for-owner-reload";Report.FinishedUtc="";
                WriteReport();CaptureScreenshot("first-load-colony.png");
                if(driver==null)throw new InvalidOperationException("Owner reload has no lifecycle driver.");
                driver.StartCoroutine(ReturnToMenuAfterScreenshot("first-load-colony.png"));return true;
            }
            ownerReload.Complete(ColonyOwner.Status.Session,ColonyOwner.Config.Flags,guid,hash,ColonyOwnerPersistence.Loads,ColonyOwnerPersistence.RestoredProfile,ColonyOwnerPersistence.RestoredImageSha256);
            RefreshOwnerCompletionReport();
            return false;
        }
        private static void RefreshOwnerCompletionReport()
        {
            var status=OniNativeColony.Status.Empty;
            OniNativeColony.Check(OniNativeColony.CopyStatus(ref status));
            var path=Path.Combine(OutputDirectory,"colony-owner.json");
            var final=Newtonsoft.Json.Linq.JObject.Parse(File.ReadAllText(path));
            final["Saves"]=ColonyOwnerPersistence.Saves;
            final["Loads"]=ColonyOwnerPersistence.Loads;
            final["LastSavedFrame"]=ColonyOwnerPersistence.LastSavedFrame;
            final["SaveBarrier"]=Newtonsoft.Json.Linq.JToken.Parse(ReportJson.Serialize(ColonySaveBarrier.Report()));
            final["SavedParticles"]=Newtonsoft.Json.Linq.JToken.Parse(ReportJson.Serialize(ColonySavedParticles.Report()));
            final["PipeSave"]=Newtonsoft.Json.Linq.JToken.Parse(ReportJson.Serialize(ColonyPipeSave.Report()));
            var continuation=ColonyContinuationProbe.Report();
            final["Continuation"]=continuation==null ? Newtonsoft.Json.Linq.JValue.CreateNull() : Newtonsoft.Json.Linq.JToken.Parse(ReportJson.Serialize(continuation));
            var control=ColonySaveControls.Report();
            final["SaveControl"]=control==null ? Newtonsoft.Json.Linq.JValue.CreateNull() : Newtonsoft.Json.Linq.JToken.Parse(ReportJson.Serialize(control));
            var restored=ColonyOwnerPersistence.RestoredReport();
            final["RestoredOwner"]=restored==null ? Newtonsoft.Json.Linq.JValue.CreateNull() : Newtonsoft.Json.Linq.JToken.Parse(ReportJson.Serialize(restored));
            var reload=OwnerReloadReport();
            final["OwnerMenuReload"]=reload==null ? Newtonsoft.Json.Linq.JValue.CreateNull() : Newtonsoft.Json.Linq.JToken.Parse(ReportJson.Serialize(reload));
            final["CompletionStatus"]=Newtonsoft.Json.Linq.JToken.Parse(ReportJson.Serialize(new {
                Scope="native status after validation actions; top-level physical fields retain their sampled frame",
                status.State,status.Session,status.CompletedFrames,status.Substeps,status.RejectedAttempts,status.SkippedRelaxations,
                status.MaximumRelativeMassError,status.MaximumRelativeEnergyError,status.TraceSinkKg,status.TraceSinkJoules,status.Error
            }));
            ReportJson.WriteAtomicText(path,final.ToString(Newtonsoft.Json.Formatting.Indented));
            if(status.State!=1 || status.Error.Length!=0)
                throw new InvalidOperationException("Owner failed during validation completion: "+status.Error);
        }
        private static void ObserveOwnerReloadMainMenu()
        {
            if(!OwnerReloadRequested || ownerReload.Stage!=1) return;
            ownerReload.MainMenu(ColonyOwner.Active);
            colonyWindow.Reset();colonyOwnerWallStart=default;Report.GameStarted=false;
            Report.RestoredFrame=0;Report.Status="pending";Report.Stage="reloading-owner-colony";
        }
    }
}
