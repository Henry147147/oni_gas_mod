using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Captures the game's own serialization without sending any message to its
    // singleton. Derived licensed data remains beside this private probe and
    // must never be included in a distributable mod package.
    internal static class NativeContractCapture
    {
        [ThreadStatic] private static bool active;
        [ThreadStatic] private static int expectedMessage;
        [ThreadStatic] private static byte[]? captured;
        internal static bool IsActive => active;
        internal static IntPtr Accept(int message, int length, IntPtr payload)
        {
            if (!active || message != expectedMessage || captured != null || length <= 0 || length > 64 * 1024 * 1024 || payload == IntPtr.Zero)
                throw new InvalidOperationException("Unexpected native contract capture message.");
            captured = new byte[length];
            Marshal.Copy(payload, captured, 0, length);
            return IntPtr.Zero;
        }
        private static byte[] Capture(int message, System.Action serialize)
        {
            if (active || ProbeScope.World != null) throw new InvalidOperationException("Nested native capture is forbidden.");
            active = true; expectedMessage = message; captured = null;
            try
            {
                serialize();
                return captured ?? throw new InvalidOperationException("No native payload was captured.");
            }
            finally { active = false; captured = null; }
        }

        internal static void Export()
        {
            var elements = ElementLoader.elements;
            if (elements == null || elements.Count == 0) throw new InvalidOperationException("Element database is not initialized.");
            NativeMessagePatch.RequirePatched(AccessTools.Method(typeof(SimMessages), nameof(SimMessages.CreateSimElementsTable)));
            NativeMessagePatch.RequirePatched(AccessTools.Method(typeof(SimMessages), nameof(SimMessages.CreateDiseaseTable)));
            var elementBytes = Capture(1108437482, () => SimMessages.CreateSimElementsTable(elements));
            var diseaseBytes = Capture(825301935, () => SimMessages.CreateDiseaseTable(Db.Get().Diseases));
            var directory = Path.Combine(Mod.DirectoryPath, "game-contract-local");
            Directory.CreateDirectory(directory);
            File.WriteAllBytes(Path.Combine(directory, "elements.bin"), elementBytes);
            File.WriteAllBytes(Path.Combine(directory, "diseases.bin"), diseaseBytes);
            var report = new CapturedContract
            {
                ElementRecordBytes = Marshal.SizeOf<Sim.Element>(),
                ElementCount = elements.Count,
                ElementTableBytes = elementBytes.Length,
                DiseaseTableBytes = diseaseBytes.Length,
                VacuumIndex = elements.FindIndex(item => item.id == SimHashes.Vacuum),
                OxygenIndex = elements.FindIndex(item => item.id == SimHashes.Oxygen),
                WaterIndex = elements.FindIndex(item => item.id == SimHashes.Water),
                GraniteIndex = elements.FindIndex(item => item.id == SimHashes.Granite),
                UnobtaniumIndex = elements.FindIndex(item => item.id == SimHashes.Unobtanium)
            };
            for (var index = 0; index < elements.Count; ++index)
            {
                var element = elements[index];
                report.Elements.Add(new CapturedElement
                {
                    Index = index, Id = (int)element.id, Name = element.id.ToString(),
                    Phase = element.IsGas ? "gas" : element.IsLiquid ? "liquid" : element.IsSolid ? "solid" : "vacuum"
                });
            }
            File.WriteAllText(Path.Combine(directory, "contract.json"), ReportJson.Serialize(report));
            File.WriteAllText(Path.Combine(directory, "DO-NOT-REDISTRIBUTE.txt"),
                "Derived data from the locally licensed ONI installation. For private native-interface experiments only.\n" +
                "Do not commit, distribute, or include this directory in a Workshop package.\n");
            RuntimeCoverageCapture.Export(directory);
            UnityEngine.Debug.Log("[Mixed Atmospheres] Native contract captured without native initialization: " + directory);
        }
    }

    [DataContract]
    internal sealed class CapturedContract
    {
        [DataMember] public string OniBuild = "744825";
        [DataMember] public int ElementCount, ElementRecordBytes, ElementTableBytes, DiseaseTableBytes;
        [DataMember] public int VacuumIndex, OxygenIndex, WaterIndex, GraniteIndex, UnobtaniumIndex;
        [DataMember] public List<CapturedElement> Elements = new List<CapturedElement>();
    }
    [DataContract]
    internal sealed class CapturedElement
    {
        [DataMember] public int Index, Id;
        [DataMember] public string Name = "", Phase = "";
    }

    [HarmonyPatch(typeof(MainMenu), "OnSpawn")]
    internal static class MainMenuContractExportPatch
    {
        private static bool captureAttempted;
        private static void Postfix()
        {
            // Diagnostics only, once per process: quit-to-menu never re-hashes,
            // re-walks every prefab or rewrites the private contract files.
            if (!captureAttempted)
            {
                captureAttempted = true;
                // Licensed-data exports are for development packages only;
                // ordinary play writes nothing into the mod's folder.
                if (Mod.Development) CaptureDiagnostics();
                GameplayPacks.NoticeAtMainMenu();
            }
            ColonyRuntimeValidation.OnMainMenu(MainMenu.Instance);
        }

        // A capture failure is not a gameplay requirement. It must not reach
        // LogError (ONI's crash reporter offers to disable mods); it is a
        // warning plus a recorded field in integration-probe.json. The six
        // integration checks still fail through Mod.OnLoad's LogError.
        private static void CaptureDiagnostics()
        {
            var status = "failed"; var detail = "";
            try { NativeContractCapture.Export(); status = "captured"; }
            catch (Exception error)
            {
                detail = error.ToString();
                try { UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Native contract diagnostics capture failed; gameplay is unaffected: " + error); }
                catch { /* Logging must not turn a diagnostic into a menu failure. */ }
            }
            try { Mod.RecordNativeContractCapture(status, detail); }
            catch (Exception error)
            {
                try { UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Could not record the native contract capture outcome: " + error.Message); }
                catch { /* Diagnostic reporting is best effort. */ }
            }
        }
    }
}
