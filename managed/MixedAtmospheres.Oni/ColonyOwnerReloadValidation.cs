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
            Scope="owner teardown, source identity and profile reload; not an input-transcript or in-flight proof"
        };
        private static bool ContinueOwnerMenuReload()
        {
            if(!OwnerReloadRequested) return false;
            var guid=SaveLoader.Instance.GameInfo.colonyGuid.ToString("N");var hash=OwnerReloadInputHash();
            if(ownerReload.Stage==0)
            {
                ownerReload.Begin(ColonyOwner.Status.Session,ColonyOwner.Config.Flags,guid,hash);
                ReportJson.WriteAtomic(Path.Combine(OutputDirectory,"first-load-colony-validation.json"),Report);
                File.Copy(Path.Combine(OutputDirectory,"colony-owner.json"),Path.Combine(OutputDirectory,"first-load-colony-owner.json"),false);
                Report.Status="pending";Report.Stage="returning-to-main-menu-for-owner-reload";Report.FinishedUtc="";
                WriteReport();CaptureScreenshot("first-load-colony.png");
                if(driver==null)throw new InvalidOperationException("Owner reload has no lifecycle driver.");
                driver.StartCoroutine(ReturnToMenuAfterScreenshot("first-load-colony.png"));return true;
            }
            ownerReload.Complete(ColonyOwner.Status.Session,ColonyOwner.Config.Flags,guid,hash,ColonyOwnerPersistence.Loads);
            var path=Path.Combine(OutputDirectory,"colony-owner.json");
            var final=Newtonsoft.Json.Linq.JObject.Parse(File.ReadAllText(path));
            final["OwnerMenuReload"]=Newtonsoft.Json.Linq.JToken.Parse(ReportJson.Serialize(OwnerReloadReport()!));
            ReportJson.WriteAtomicText(path,final.ToString(Newtonsoft.Json.Formatting.Indented));
            return false;
        }
        private static void ObserveOwnerReloadMainMenu()
        {
            if(!OwnerReloadRequested || ownerReload.Stage!=1) return;
            ownerReload.MainMenu(ColonyOwner.Active);
            colonyOwnerBaseline=0;colonyOwnerWallStart=default;Report.GameStarted=false;
            Report.RestoredFrame=0;Report.Status="pending";Report.Stage="reloading-owner-colony";
        }
    }
}
