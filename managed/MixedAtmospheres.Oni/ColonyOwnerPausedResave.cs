using System;
using System.IO;
using System.Security.Cryptography;

namespace MixedAtmospheres.Oni
{
    // Explicit private owner-load control. Positive ticks stay at zero until
    // an ordinary paused save has preserved the physical and delivery boundary.
    internal static class ColonyOwnerPausedResave
    {
        private static ulong verifiedSession;
        private static string savedFile = "", physicalBefore = "", physicalAfter = "", savedPhysical = "", savedOwner = "";
        private static uint pending;
        private static bool savedOwnerReencoded, callbacksPreserved, completeOwnerUnchanged;
        private static long migrated;
        private static int phase, pendingComponents, savesBefore;
        private static float started=-1;
        private static ColonyActivityResavePoint? before;
        private static byte[] beforeImage=Array.Empty<byte>(), beforePhysical=Array.Empty<byte>(), callbacksBefore=Array.Empty<byte>();
        private static bool Required => ColonyRuntimeValidation.Settings?.Mode == "colony-owner-load" && ColonyRuntimeValidation.Settings.SaveBeforeResume;
        internal static bool Holding => Required && (!ColonyOwner.Active || verifiedSession != ColonyOwner.Status.Session);
        internal static void Reset() { verifiedSession = 0; savedFile = physicalBefore = physicalAfter = savedPhysical = savedOwner = ""; pending = 0; migrated = 0; savedOwnerReencoded = callbacksPreserved = completeOwnerUnchanged = false; phase=pendingComponents=savesBefore=0;started=-1;before=null;beforeImage=beforePhysical=callbacksBefore=Array.Empty<byte>(); }
        internal static object Report() => new { Required, Phase=phase, PendingComponents=pendingComponents, Verified = verifiedSession != 0, Session = verifiedSession, SavedFile = savedFile,
            PhysicalBeforeSha256 = physicalBefore, PhysicalAfterSha256 = physicalAfter, WriterPhysicalSha256 = savedPhysical,
            WriterOwnerSha256 = savedOwner, WriterImageReencoded = savedOwnerReencoded, CompleteOwnerBeforeAfterIdentical = completeOwnerUnchanged, CallbackRequestsPreserved = callbacksPreserved, PendingFrames = pending, ReplayMigratedActors = migrated };
        private static string Hash(byte[] image)
        { using var hash=SHA256.Create();return BitConverter.ToString(hash.ComputeHash(image)).Replace("-", "").ToLowerInvariant(); }
        private static ColonyActivityResavePoint Point()
        {
            var state=OniNativeColony.ActivityState.Empty;OniNativeColony.Check(OniNativeColony.CopyActivity(ref state));
            var owner=ColonyOwner.Status;
            if(state.Session!=owner.Session)throw new InvalidOperationException("Paused resave observed a different activity session.");
            return new ColonyActivityResavePoint { Session=owner.Session,Frame=owner.CompletedFrames,OwnerSeconds=owner.TimeSeconds,
                GameSeconds=GameClock.Instance.GetTime(),ActorInputs=ColonyActors.Queued,Committed=state.LastCommittedSequence,
                Next=state.NextSubmissionSequence,Head=state.PendingHeadSequence,HeadSeconds=state.PendingHeadSeconds,Pending=state.PendingFrames,Enabled=state.Enabled };
        }
        private static bool SpawnReady()
        {
            pendingComponents=0;
            foreach(var component in UnityEngine.Object.FindObjectsByType<KMonoBehaviour>(UnityEngine.FindObjectsInactive.Include,UnityEngine.FindObjectsSortMode.None))
            {
                if(component==null || !component.enabled || !component.gameObject.activeInHierarchy || component.GetComponent<SaveLoadRoot>()==null)continue;
                var prefab=component.GetComponent<KPrefabID>();
                if(prefab==null || !ReferenceEquals(KPrefabIDTracker.Get().GetInstance(prefab.InstanceID),prefab))continue;
                if(!component.IsInitialized() || !component.isSpawned)++pendingComponents;
            }
            return pendingComponents==0;
        }
        internal static void Step()
        {
            if(!Holding || !ColonyOwner.Active)return;
            if(started<0){started=UnityEngine.Time.realtimeSinceStartup;SpeedControlScreen.Instance.Pause(false);}
            if(UnityEngine.Time.realtimeSinceStartup-started>90)
                throw new InvalidOperationException("Restored components or deferred paused save did not finish their normal lifecycle.");
            if(ColonyOwnerPersistence.Loads==0 || ColonyOwnerPersistence.RestoredImageReencoded!=true)
                throw new InvalidOperationException("Paused resave requires an exactly reencoded restored owner.");
            if(phase==0)
            {
                // GameStarted precedes some KMonoBehaviour.Start/SMI restores.
                // Saving synchronously during those restores can overwrite the
                // serializer's active buffers. Inspect actual lifecycle state.
                if(!SpawnReady())return;
                ColonyNativeClock.BeforePositiveTime();
                _=ColonySaveBarrier.Settle();
                ColonyActivity.QueueFrame(0);
                _=ColonySaveBarrier.Settle();
                if(!ColonyCallbackRequests.TryRebind(false))return;
                before=Point();beforeImage=ColonyOwner.Checkpoint();beforePhysical=ColonyCheckpointMetadata.PhysicalImage(beforeImage).ToArray();
                callbacksBefore=ColonyCallbackRequests.Capture()??Array.Empty<byte>();
                savesBefore=ColonyOwnerPersistence.Saves;
                savedFile=Path.Combine(ColonyRuntimeValidation.OutputDirectory,"owner-frame-zero-resave.sav");
                if(File.Exists(savedFile))throw new InvalidOperationException("Paused resave destination already exists.");
                phase=1;
                // The game's own deferred entry supplies the Unity lifecycle
                // boundary. Keep the baseline and pause state while it waits.
                Game.Instance.StartDelayedSave(savedFile,false,false);
                return;
            }
            if(phase!=1 || ColonyOwnerPersistence.Saves==savesBefore)return;
            var after=Point();var afterImage=ColonyOwner.Checkpoint();var afterPhysical=ColonyCheckpointMetadata.PhysicalImage(afterImage).ToArray();
            var callbacksAfter=ColonyCallbackRequests.Capture()??Array.Empty<byte>();
            callbacksPreserved=callbacksBefore.AsSpan().SequenceEqual(callbacksAfter);
            ColonyActivityResaveProof.Validate(before!,after,beforePhysical,afterPhysical);
            ColonyActivityResaveProof.ValidateImmutableOwner(beforeImage,afterImage);completeOwnerUnchanged=true;
            physicalBefore=Hash(beforePhysical);physicalAfter=Hash(afterPhysical);
            savedPhysical=ColonyOwnerPersistence.ValidationWriterPhysicalSha256;savedOwner=ColonyOwnerPersistence.ValidationWriterOwnerSha256;
            savedOwnerReencoded=Hash(afterImage)==savedOwner&&Hash(beforeImage)==savedOwner;
            if(!savedOwnerReencoded || !callbacksPreserved || ColonyOwnerPersistence.Saves!=savesBefore+1 || !File.Exists(savedFile) || ColonyOwnerPersistence.LastSavedFrame!=before!.Frame ||
                !SpeedControlScreen.Instance.IsPaused || savedPhysical!=physicalBefore || savedOwner.Length!=64)
                throw new InvalidOperationException("The ordinary paused save did not retain the verified owner image.");
            pending=after.Pending;migrated=ColonyActivity.ReplayMigratedActors;verifiedSession=after.Session;phase=2;
            beforeImage=beforePhysical=callbacksBefore=Array.Empty<byte>();before=null;
            UnityEngine.Debug.Log("[Mixed Atmospheres] owner frame-zero resave verified at frame "+after.Frame+", pending activity "+pending+", migrated actors "+migrated+".");
        }
    }
}
