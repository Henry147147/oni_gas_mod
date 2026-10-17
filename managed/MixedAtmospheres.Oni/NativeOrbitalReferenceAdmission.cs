using System.Collections.Generic;
using System.IO;

namespace MixedAtmospheres.Oni
{
    internal sealed class NativeOrbitalWorldFact
    {
        public int OwnerId, WorldId;
        public bool Tracked, RootInitialized, RootSpawned;
        public int SaveRegistrations;
    }

    internal sealed class NativeOrbitalReferenceFact
    {
        public int OwnerId, Slot, SavedId, TargetId = -1, OrbitingWorldId = -1;
        public string Prefab = "";
        public bool Tracked, RootInitialized, RootSpawned;
        public int SaveRegistrations;
    }

    // Pure admission over observations. Optional empty refs are valid; a
    // positive saved identity must resolve without calling Ref.Get(), which
    // clears a dangling native reference as a side effect.
    internal static class NativeOrbitalReferenceAdmission
    {
        internal static void Validate(IReadOnlyList<NativeOrbitalWorldFact> worlds, IReadOnlyList<NativeOrbitalReferenceFact> references)
            => Inspect(worlds, references, false);

        // Only a tracked, initialized object whose native Start has not yet
        // spawned/registered its root may wait. Inspect every identity first:
        // an unrelated pending root must never postpone a dangling reference.
        internal static int PendingRestoreRoots(IReadOnlyList<NativeOrbitalWorldFact> worlds, IReadOnlyList<NativeOrbitalReferenceFact> references)
            => Inspect(worlds, references, true);

        private static int Inspect(IReadOnlyList<NativeOrbitalWorldFact> worlds, IReadOnlyList<NativeOrbitalReferenceFact> references, bool allowNativeStart)
        {
            if (worlds.Count == 0) throw new InvalidDataException("Native orbital reference admission has no live worlds.");
            var pending = 0;
            bool RootReady(bool initialized, bool spawned, int registrations)
            {
                if (initialized && spawned && registrations == 1) return true;
                if (allowNativeStart && initialized && !spawned && registrations == 0) { ++pending; return true; }
                return false;
            }
            var owners = new Dictionary<int, int>(); var worldIds = new HashSet<int>();
            foreach (var world in worlds)
            {
                if (world.OwnerId < 0 || world.WorldId < 0 || owners.ContainsKey(world.OwnerId) || !worldIds.Add(world.WorldId) || !world.Tracked ||
                    !RootReady(world.RootInitialized, world.RootSpawned, world.SaveRegistrations))
                    throw new InvalidDataException("Native orbital world " + world.OwnerId + " has no unique registered save root.");
                owners.Add(world.OwnerId, world.WorldId);
            }
            var slots = new HashSet<(int, int)>();
            foreach (var reference in references)
            {
                if (!owners.ContainsKey(reference.OwnerId) || reference.Slot < 0 || !slots.Add((reference.OwnerId, reference.Slot)) || reference.SavedId < -1)
                    throw new InvalidDataException("Native orbital reference has an invalid world, slot or saved identity.");
                if (reference.SavedId == -1 && reference.TargetId == -1) continue;
                if (reference.TargetId < 0 || reference.SavedId >= 0 && reference.SavedId != reference.TargetId ||
                    reference.OrbitingWorldId != owners[reference.OwnerId] || !reference.Tracked || reference.Prefab != "OrbitalBG" ||
                    !RootReady(reference.RootInitialized, reference.RootSpawned, reference.SaveRegistrations))
                    throw new InvalidDataException("Native orbital saved reference " + reference.SavedId + " in world " + reference.OwnerId +
                        " does not resolve to one tracked OrbitalBG save root in that world. Restore a source save containing that native object; this reference was not rewritten.");
            }
            return pending;
        }
    }
}
