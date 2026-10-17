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
        {
            if (worlds.Count == 0) throw new InvalidDataException("Native orbital reference admission has no live worlds.");
            var owners = new Dictionary<int, int>(); var worldIds = new HashSet<int>();
            foreach (var world in worlds)
            {
                if (world.OwnerId < 0 || world.WorldId < 0 || owners.ContainsKey(world.OwnerId) || !worldIds.Add(world.WorldId) || !world.Tracked ||
                    !world.RootInitialized || !world.RootSpawned || world.SaveRegistrations != 1)
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
                    reference.OrbitingWorldId != owners[reference.OwnerId] || !reference.Tracked || reference.Prefab != "OrbitalBG" || !reference.RootInitialized ||
                    !reference.RootSpawned || reference.SaveRegistrations != 1)
                    throw new InvalidDataException("Native orbital saved reference " + reference.SavedId + " in world " + reference.OwnerId +
                        " does not resolve to one tracked OrbitalBG save root in that world. Restore a source save containing that native object; this reference was not rewritten.");
            }
        }
    }
}
