using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // OrbitalObject.OnSpawn deactivates its own GameObject. Unity can then
    // postpone the later SaveLoadRoot.Start indefinitely for a hidden orbit.
    // Finish that existing root's normal, idempotent lifecycle first. This
    // does not manufacture a missing object or change the orbital reference.
    [HarmonyPatch(typeof(OrbitalObject), "OnSpawn")]
    internal static class NativeOrbitalSaveLifecycle
    {
        private static readonly List<int> completedRoots = new List<int>();
        private static int alreadySpawnedRoots;
        private static string error = "";

        private static bool Prepare() => NativeSealedDomainValidation.IsMode;

        private static void Prefix(OrbitalObject __instance)
        {
            var root = __instance.GetComponent<SaveLoadRoot>();
            if (root != null && root.isSpawned) { ++alreadySpawnedRoots; return; }
            try
            {
                var id = __instance.GetComponent<KPrefabID>();
                var manager = SaveLoader.Instance?.saveManager;
                if (root == null || !root.IsInitialized() || id == null || id.InstanceID < 0 ||
                    id.PrefabTag.Name != "OrbitalBG" ||
                    !ReferenceEquals(KPrefabIDTracker.Get().GetInstance(id.InstanceID), id) || manager == null)
                    throw new InvalidDataException("Native orbital spawning requires its existing tracked, initialized save root.");
                var registry = (Dictionary<Tag, List<SaveLoadRoot>>)AccessTools.Field(typeof(SaveManager), "sceneObjects").GetValue(manager);
                int Registrations() => registry.Values.Sum(list => list.Count(value => ReferenceEquals(value, root)));
                if (Registrations() != 0)
                    throw new InvalidDataException("An unspawned native orbital root already has a save registration.");
                root.Spawn();
                if (!root.isSpawned || Registrations() != 1)
                    throw new InvalidDataException("Native orbital save-root spawning did not register exactly once.");
                completedRoots.Add(id.InstanceID);
            }
            catch (Exception problem) { error = problem.Message; throw; }
        }

        internal static object Report() => new {
            Source = "existing-SaveLoadRoot.Spawn-before-OrbitalObject.OnSpawn",
            CompletedRootIds = completedRoots.ToArray(), AlreadySpawnedRoots = alreadySpawnedRoots,
            Error = error, ReferencesRewritten = 0, ObjectsCreated = 0
        };

        internal static void Reset() { completedRoots.Clear(); alreadySpawnedRoots = 0; error = ""; }
    }
}
