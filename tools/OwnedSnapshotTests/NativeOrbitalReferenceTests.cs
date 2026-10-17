using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using MixedAtmospheres.Oni;

internal static class NativeOrbitalReferenceTests
{
    private static NativeOrbitalWorldFact World(int id = 1) => new NativeOrbitalWorldFact {
        OwnerId = id, WorldId = id - 1, Tracked = true, RootInitialized = true, RootSpawned = true, SaveRegistrations = 1 };
    private static NativeOrbitalReferenceFact Reference(int owner = 1, int slot = 0, int id = 2) => new NativeOrbitalReferenceFact {
        OwnerId = owner, Slot = slot, SavedId = id, TargetId = id, OrbitingWorldId = owner - 1, Prefab = "OrbitalBG",
        Tracked = true, RootInitialized = true, RootSpawned = true, SaveRegistrations = 1 };
    internal static void Run(System.Action<string, System.Action> test, System.Action<bool> check, System.Action<System.Action> reject)
    {
        test("native orbital reference inspection uses the actual raw fields and save lifecycle declarations", () => {
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public;
            check(typeof(OrbitalMechanics).GetField("orbitingObjects", flags)?.FieldType == typeof(List<Ref<OrbitalObject>>));
            check(typeof(Ref<OrbitalObject>).GetField("id", flags)?.FieldType == typeof(int));
            check(typeof(Ref<OrbitalObject>).GetField("obj", flags)?.FieldType == typeof(OrbitalObject));
            check(typeof(OrbitalObject).GetField("orbitingWorldId", flags)?.FieldType == typeof(int));
            check(typeof(SaveManager).GetField("sceneObjects", flags)?.FieldType == typeof(Dictionary<Tag, List<SaveLoadRoot>>));
            check(typeof(KMonoBehaviour).GetMethod(nameof(KMonoBehaviour.IsInitialized), flags, null, Type.EmptyTypes, null)?.ReturnType == typeof(bool));
        });
        test("the installed orbital spawn deactivates its object before Unity can start a later save-root component", () => {
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public;
            var orbital = PatchProcessor.GetOriginalInstructions(typeof(OrbitalObject).GetMethod("OnSpawn", flags)!);
            var deactivate = orbital.Select((instruction, index) => (instruction, index)).Single(item =>
                item.instruction.operand is MethodInfo method && method.DeclaringType == typeof(UnityEngine.GameObject) && method.Name == "SetActive");
            check(deactivate.index > 0 && orbital[deactivate.index - 1].opcode == OpCodes.Ldc_I4_0);
            check(!orbital.Any(instruction => instruction.operand is MethodInfo method && method.DeclaringType == typeof(SaveLoadRoot)));
            var root = PatchProcessor.GetOriginalInstructions(typeof(SaveLoadRoot).GetMethod("OnSpawn", flags)!);
            check(root.Count(instruction => instruction.operand is MethodInfo method && method.DeclaringType == typeof(SaveManager) && method.Name == "Register") == 1);
            var start = PatchProcessor.GetOriginalInstructions(typeof(KMonoBehaviour).GetMethod(nameof(KMonoBehaviour.Start))!);
            check(start.Count(instruction => instruction.operand is MethodInfo method && method.DeclaringType == typeof(KMonoBehaviour) && method.Name == nameof(KMonoBehaviour.Spawn)) == 1);
        });
        test("orbital admission retains multiple worlds resolved refs optional empties and a new cached native object", () => {
            var created = Reference(9, 1, 12); created.SavedId = -1;
            var optional = new NativeOrbitalReferenceFact { OwnerId = 9, Slot = 2, SavedId = -1 };
            NativeOrbitalReferenceAdmission.Validate(new[] { World(), World(9) }, new[] { Reference(), Reference(9, 0, 11), created, optional });
            check(created.SavedId == -1 && created.TargetId == 12 && optional.TargetId == -1);
        });
        test("a dangling saved orbital ID rejects without clearing or resolving the reference", () => {
            var missing = new NativeOrbitalReferenceFact { OwnerId = 1, Slot = 0, SavedId = 2 };
            reject(() => NativeOrbitalReferenceAdmission.Validate(new[] { World() }, new[] { missing }));
            check(missing.SavedId == 2 && missing.TargetId == -1 && missing.Prefab == "");
        });
        test("a live orbital target must retain its saved identity prefab and unique initialized save registration", () => {
            for (var fault = 0; fault < 9; ++fault)
            {
                var reference = Reference();
                switch (fault) {
                    case 0: reference.TargetId = 3; break;
                    case 1: reference.Prefab = "Asteroid"; break;
                    case 2: reference.Tracked = false; break;
                    case 3: reference.RootInitialized = false; break;
                    case 4: reference.RootSpawned = false; break;
                    case 5: reference.SaveRegistrations = 0; break;
                    case 6: reference.SaveRegistrations = 2; break;
                    case 7: reference.TargetId = -1; break;
                    case 8: reference.OrbitingWorldId = 90; break;
                }
                reject(() => NativeOrbitalReferenceAdmission.Validate(new[] { World() }, new[] { reference }));
                check(reference.SavedId == 2);
            }
        });
        test("world orbital ownership requires a unique tracked registered native save root", () => {
            reject(() => NativeOrbitalReferenceAdmission.Validate(Array.Empty<NativeOrbitalWorldFact>(), Array.Empty<NativeOrbitalReferenceFact>()));
            reject(() => NativeOrbitalReferenceAdmission.Validate(new[] { World(), World() }, Array.Empty<NativeOrbitalReferenceFact>()));
            var alias = World(5); alias.WorldId = 0;
            reject(() => NativeOrbitalReferenceAdmission.Validate(new[] { World(), alias }, Array.Empty<NativeOrbitalReferenceFact>()));
            for (var fault = 0; fault < 6; ++fault)
            {
                var world = World();
                switch (fault) {
                    case 0: world.OwnerId = -1; break;
                    case 1: world.Tracked = false; break;
                    case 2: world.RootInitialized = false; break;
                    case 3: world.RootSpawned = false; break;
                    case 4: world.SaveRegistrations = 0; break;
                    case 5: world.SaveRegistrations = 2; break;
                }
                reject(() => NativeOrbitalReferenceAdmission.Validate(new[] { world }, Array.Empty<NativeOrbitalReferenceFact>()));
            }
        });
        test("orbital slots cannot refer to unknown worlds duplicate positions or invalid saved IDs", () => {
            for (var fault = 0; fault < 4; ++fault)
            {
                var valid = Reference(); var late = Reference(1, 1, 3);
                switch (fault) {
                    case 0: late.OwnerId = 20; break;
                    case 1: late.Slot = 0; break;
                    case 2: late.Slot = -1; break;
                    case 3: late.SavedId = -2; break;
                }
                reject(() => NativeOrbitalReferenceAdmission.Validate(new[] { World() }, new[] { valid, late }));
                check(valid.SavedId == 2 && valid.TargetId == 2 && valid.SaveRegistrations == 1);
            }
        });
        test("restored initialized save roots wait for native Start and become admissible without changing their facts", () => {
            var worlds = Enumerable.Range(1, 9).Select(World).ToArray(); var reference = Reference();
            foreach (var world in worlds) { world.RootSpawned = false; world.SaveRegistrations = 0; }
            reference.RootSpawned = false; reference.SaveRegistrations = 0;
            check(NativeOrbitalReferenceAdmission.PendingRestoreRoots(worlds, new[] { reference }) == 10);
            reject(() => NativeOrbitalReferenceAdmission.Validate(worlds, new[] { reference }));
            check(worlds.All(world => !world.RootSpawned && world.SaveRegistrations == 0) && !reference.RootSpawned && reference.SavedId == 2);
            foreach (var world in worlds) { world.RootSpawned = true; world.SaveRegistrations = 1; }
            check(NativeOrbitalReferenceAdmission.PendingRestoreRoots(worlds, new[] { reference }) == 1);
            reference.RootSpawned = true; reference.SaveRegistrations = 1;
            check(NativeOrbitalReferenceAdmission.PendingRestoreRoots(worlds, new[] { reference }) == 0);
            NativeOrbitalReferenceAdmission.Validate(worlds, new[] { reference });
        });
        test("a pending world never postpones a missing or mismatched orbital identity", () => {
            var world = World(); world.RootSpawned = false; world.SaveRegistrations = 0;
            for (var fault = 0; fault < 5; ++fault)
            {
                var reference = Reference();
                switch (fault) {
                    case 0: reference.TargetId = -1; break;
                    case 1: reference.TargetId = 3; break;
                    case 2: reference.Prefab = "Asteroid"; break;
                    case 3: reference.Tracked = false; break;
                    case 4: reference.OrbitingWorldId = 99; break;
                }
                reject(() => NativeOrbitalReferenceAdmission.PendingRestoreRoots(new[] { world }, new[] { reference }));
                check(reference.SavedId == 2 && !world.RootSpawned && world.SaveRegistrations == 0);
            }
        });
        test("inconsistent or uninitialized native roots reject instead of waiting", () => {
            foreach (var (initialized, spawned, registrations) in new[] {
                (false, false, 0), (false, true, 1), (true, true, 0), (true, false, 1), (true, true, 2), (true, false, -1) })
            {
                var world = World(); world.RootInitialized = initialized; world.RootSpawned = spawned; world.SaveRegistrations = registrations;
                reject(() => NativeOrbitalReferenceAdmission.PendingRestoreRoots(new[] { world }, new[] { Reference() }));
                var reference = Reference(); reference.RootInitialized = initialized; reference.RootSpawned = spawned; reference.SaveRegistrations = registrations;
                reject(() => NativeOrbitalReferenceAdmission.PendingRestoreRoots(new[] { World() }, new[] { reference }));
            }
        });
        test("restore waiting retains the strict world slot and optional-reference rules", () => {
            var world = World(); world.RootSpawned = false; world.SaveRegistrations = 0;
            reject(() => NativeOrbitalReferenceAdmission.PendingRestoreRoots(new[] { world, world }, Array.Empty<NativeOrbitalReferenceFact>()));
            reject(() => NativeOrbitalReferenceAdmission.PendingRestoreRoots(new[] { world }, new[] { Reference(9) }));
            reject(() => NativeOrbitalReferenceAdmission.PendingRestoreRoots(new[] { world }, new[] { Reference(), Reference() }));
            var optional = new NativeOrbitalReferenceFact { OwnerId = 1, Slot = 0, SavedId = -1 };
            check(NativeOrbitalReferenceAdmission.PendingRestoreRoots(new[] { world }, new[] { optional }) == 1 && optional.TargetId == -1);
        });
    }
}
