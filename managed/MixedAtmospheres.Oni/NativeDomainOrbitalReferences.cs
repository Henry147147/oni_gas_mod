using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool restoredOrbitalReferencesVerified, orbitalReferencesVerified;
        private static int orbitalReferenceInspections;
        private static ulong restoredOrbitalReferenceFrame;
        private static double restoredOrbitalReferenceTime;
        private static string orbitalReferenceStage = "", orbitalReferenceError = "";
        private static NativeOrbitalWorldFact[] orbitalWorlds = Array.Empty<NativeOrbitalWorldFact>();
        private static NativeOrbitalReferenceFact[] orbitalReferences = Array.Empty<NativeOrbitalReferenceFact>();
        internal static bool HoldOrbitalReferenceRestorePause => ColonyRestoreMode && colonyLoaded && !restoredOrbitalReferencesVerified;

        private static void VerifyNativeOrbitalReferences(string stage)
        {
            orbitalReferencesVerified = false; orbitalReferenceStage = stage; orbitalReferenceError = ""; ++orbitalReferenceInspections;
            var worlds = new List<NativeOrbitalWorldFact>(); var references = new List<NativeOrbitalReferenceFact>();
            try
            {
                if (ClusterManager.Instance == null || SaveLoader.Instance?.saveManager == null)
                    throw new InvalidDataException("Native orbital reference admission requires the restored world and save registries.");
                var manager = SaveLoader.Instance.saveManager;
                var roots = ((Dictionary<Tag, List<SaveLoadRoot>>)AccessTools.Field(typeof(SaveManager), "sceneObjects").GetValue(manager))
                    .Values.SelectMany(list => list).ToArray();
                int Registrations(SaveLoadRoot? root) => root == null ? 0 : roots.Count(value => ReferenceEquals(value, root));
                foreach (var world in ClusterManager.Instance.WorldContainers)
                {
                    var id = world.GetComponent<KPrefabID>(); var root = world.GetComponent<SaveLoadRoot>();
                    var owner = id == null ? -1 : id.InstanceID;
                    worlds.Add(new NativeOrbitalWorldFact { OwnerId = owner, WorldId = world.id,
                        Tracked = id != null && ReferenceEquals(KPrefabIDTracker.Get().GetInstance(owner), id),
                        RootInitialized = root != null && root.IsInitialized(), RootSpawned = root != null && root.isSpawned,
                        SaveRegistrations = Registrations(root) });
                    var mechanics = world.GetComponent<OrbitalMechanics>();
                    if (mechanics == null) throw new InvalidDataException("Native world " + owner + " has no inspected OrbitalMechanics component.");
                    var saved = (List<Ref<OrbitalObject>>)AccessTools.Field(typeof(OrbitalMechanics), "orbitingObjects").GetValue(mechanics);
                    if (saved == null) throw new InvalidDataException("Native world " + owner + " has no orbital reference list.");
                    for (var slot = 0; slot < saved.Count; ++slot)
                    {
                        var reference = saved[slot];
                        if (reference == null) throw new InvalidDataException("Native world " + owner + " has a null orbital reference record.");
                        // Ref.Get and GetId both mutate the record on a missing
                        // object. Read its saved ID/cache directly instead.
                        var savedId = (int)AccessTools.Field(typeof(Ref<OrbitalObject>), "id").GetValue(reference);
                        var target = (OrbitalObject?)AccessTools.Field(typeof(Ref<OrbitalObject>), "obj").GetValue(reference);
                        if (target == null && savedId >= 0) target = KPrefabIDTracker.Get().GetInstance(savedId)?.GetComponent<OrbitalObject>();
                        var targetId = target == null ? null : target.GetComponent<KPrefabID>();
                        var targetRoot = target == null ? null : target.GetComponent<SaveLoadRoot>();
                        references.Add(new NativeOrbitalReferenceFact { OwnerId = owner, Slot = slot, SavedId = savedId,
                            OrbitingWorldId = target == null ? -1 : (int)AccessTools.Field(typeof(OrbitalObject), "orbitingWorldId").GetValue(target),
                            TargetId = targetId == null ? -1 : targetId.InstanceID, Prefab = targetId == null ? "" : targetId.PrefabTag.Name,
                            Tracked = targetId != null && ReferenceEquals(KPrefabIDTracker.Get().GetInstance(targetId.InstanceID), targetId),
                            RootInitialized = targetRoot != null && targetRoot.IsInitialized(), RootSpawned = targetRoot != null && targetRoot.isSpawned,
                            SaveRegistrations = Registrations(targetRoot) });
                    }
                }
                orbitalWorlds = worlds.ToArray(); orbitalReferences = references.ToArray();
                NativeOrbitalReferenceAdmission.Validate(orbitalWorlds, orbitalReferences);
                orbitalReferencesVerified = true;
            }
            catch (Exception problem)
            {
                orbitalWorlds = worlds.ToArray(); orbitalReferences = references.ToArray(); orbitalReferenceError = problem.Message;
                throw;
            }
        }
        private static object NativeOrbitalReferenceReport() => new {
            Required = ColonyRestoreMode || automaticSaveAdmissions > 0 || orbitalReferenceInspections > 0,
            Verified = orbitalReferencesVerified, RestoredBeforeAdvanceVerified = restoredOrbitalReferencesVerified,
            RestoredPhysicalFrame = restoredOrbitalReferenceFrame, RestoredPhysicalTime = restoredOrbitalReferenceTime,
            Source = "read-only-native-orbital-reference-and-save-root-registry", Stage = orbitalReferenceStage,
            Inspections = orbitalReferenceInspections, Error = orbitalReferenceError, Worlds = orbitalWorlds, References = orbitalReferences,
            RewrittenReferences = 0, GeneralManagedReferenceIntegrityVerified = false
        };
    }
}
