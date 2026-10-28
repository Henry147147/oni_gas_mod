using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using HarmonyLib;
using KSerialization;

namespace MixedAtmospheres.Oni
{
    // Colony saves (docs/colony-owner.md#saves): ONI's native section becomes a
    // MACOLONY envelope holding the original native bytes and the colony
    // checkpoint. A game without the mod hands the envelope to its native
    // loader, which refuses it.
    internal static unsafe class ColonyOwnerPersistence
    {
        // Envelopes record the build that wrote them and are accepted from
        // the first supported build up to the running one
        // (docs/game-update-resilience.md#saves-across-builds).
        private const int MaterialModel = 3, NativeLoad = -672538170;
        private static int RunningBuild => GameBuildCheck.RunningBuild;
        [DllImport("SimDLL", EntryPoint = "SIM_BeginSave")] private static extern byte* BeginSave(int* size, int width, int height);
        [DllImport("SimDLL", EntryPoint = "SIM_EndSave")] private static extern void EndSave();
        private static (OniNativeColony.Config config, byte[] image)? pending;
        internal static int Saves { get; private set; }
        internal static ulong LastSavedFrame { get; private set; }
        // Exact image supplied to the successful envelope writer, not disk readback.
        internal static string ValidationWriterOwnerSha256 { get; private set; } = "";
        internal static string ValidationWriterPhysicalSha256 { get; private set; } = "";
        internal static int Loads { get; private set; }
        // What the last restore actually runs (decoded from the owner image,
        // not from config flags) and, under validation, whether an immediate
        // re-checkpoint reproduced the saved owner image byte for byte.
        internal static ColonyPhysicalProfile? RestoredProfile { get; private set; }
        internal static string RestoredImageSha256 { get; private set; } = "";
        internal static bool? RestoredImageReencoded { get; private set; }
        internal static double RestoredReencodeMilliseconds { get; private set; }
        internal static string RestoredReencodeScope { get; private set; } = "";

        private static byte[] CatalogueHash(OniNativeColony.Material[] materials)
        {
            var bytes = new byte[materials.Length * 168];
            fixed (byte* target = bytes)
                for (var k = 0; k < materials.Length; ++k) Marshal.StructureToPtr(materials[k], (IntPtr)(target + k * 168), false);
            using var hash = SHA256.Create(); return hash.ComputeHash(bytes);
        }
        private static byte[] ConfigBytes(OniNativeColony.Config config)
        {
            var bytes = new byte[56];
            fixed (byte* target = bytes) Marshal.StructureToPtr(config, (IntPtr)target, false);
            return bytes;
        }

        internal static bool TryWriteSave(BinaryWriter writer, int offsetX, int offsetY)
        {
            if (!ColonyOwner.Active) return false;
            if (ColonyOwner.Status.State != 1 || ColonyFault.Faulted)
                throw new InvalidOperationException("A faulted colony owner cannot be saved: " + (ColonyFault.Faulted ? ColonyFault.Reason : ColonyOwner.Status.Error));
            // Normally settled at SaveLoader.Save(string) entry; any other
            // path settles here, before the native section is captured.
            var settledFrame = ColonySaveBarrier.ForCapture();
            // Bind restored in-flight requests if their actors are ready;
            // otherwise they are saved again under their loaded actors.
            ColonyCallbackRequests.TryRebind(false);
            int size; byte* data = BeginSave(&size, offsetX, offsetY); byte[] native;
            try { native = new ReadOnlySpan<byte>(data, size).ToArray(); } finally { EndSave(); }
            var image = ColonyOwner.Checkpoint();
            var completedFrame = ColonyCheckpointMetadata.CompletedFrames(image);
            if (completedFrame != settledFrame)
                throw new InvalidOperationException("The colony owner advanced between its save barrier and its capture.");
            // The actors answering the image's callback-owned requests (wrapper 13).
            var requests = ColonyCallbackRequests.Capture();
            ColonyCallbackRequestImage.ValidateOwner(image, requests);
            var config = ConfigBytes(ColonyOwner.Config);
            // Sections hold what is in flight outside the owner image; with
            // none the payload keeps the exact historical MACOLP01 bytes.
            var sections = new List<KeyValuePair<uint, byte[]>>();
            if (ColonyPipeSave.Capture() is byte[] pipes)
                sections.Add(new KeyValuePair<uint, byte[]>(ColonySavePayload.PipeRuntime, pipes));
            if (ColonySavedParticles.Capture(ColonyOwner.Config) is byte[] particles)
                sections.Add(new KeyValuePair<uint, byte[]>(ColonySavePayload.CarbonParticles, particles));
            // Only a colony whose breathing rules differ from the defaults.
            if (ModSettingsRuntime.CaptureRecord() is byte[] settings)
                sections.Add(new KeyValuePair<uint, byte[]>(ColonySavePayload.ColonySettings, settings));
            if (requests != null)
                sections.Add(new KeyValuePair<uint, byte[]>(ColonySavePayload.CallbackRequests, requests));
            if(ColonyNativeClock.Capture() is byte[] clock)
                sections.Add(new KeyValuePair<uint, byte[]>(ColonySavePayload.NativeRuntimeClock,clock));
            var payload = ColonySavePayload.Encode(config, image, sections);
            var envelope = ColonySaveEnvelope.Create(RunningBuild, MaterialModel, completedFrame,
                CatalogueHash(ColonyOwner.Materials), native, payload);
            envelope.WriteLengthPrefixed(writer); ++Saves; LastSavedFrame=completedFrame;
            if (ColonyRuntimeValidation.Settings?.SaveBeforeResume == true && ColonyRuntimeValidation.Settings.Mode == "colony-owner-load")
            {
                using var digest = SHA256.Create();
                ValidationWriterOwnerSha256 = BitConverter.ToString(digest.ComputeHash(image)).Replace("-", "").ToLowerInvariant();
                ValidationWriterPhysicalSha256 = BitConverter.ToString(digest.ComputeHash(ColonyCheckpointMetadata.PhysicalImage(image).ToArray())).Replace("-", "").ToLowerInvariant();
            }
            UnityEngine.Debug.Log($"[Mixed Atmospheres] Colony saved at owner frame {completedFrame} ({envelope.EncodedLength} bytes).");
            return true;
        }

        private static ColonySavePayload ValidateCurrentContent(ReadOnlySpan<byte> sim)
        {
            // Check framing/checksum with its recorded catalogue first so the
            // removed content gets a useful refusal even before elements load.
            var recorded = sim.Length >= 60 ? sim.Slice(28, ColonySaveEnvelope.CatalogHashBytes).ToArray() : new byte[ColonySaveEnvelope.CatalogHashBytes];
            var envelope = ColonySaveEnvelope.Parse(sim, ColonySaveEnvelope.FirstSupportedBuild, RunningBuild, MaterialModel, recorded);
            var payload = ColonySavePayload.Decode(envelope.CustomData);
            if(payload.Section(ColonySavePayload.NativeRuntimeClock) is byte[] clock)
            {
                _=ColonyNativeClockState.Decode(clock);
                if(payload.Section(ColonySavePayload.PipeRuntime)==null)throw new InvalidDataException("Native loop continuation requires its mixed-pipe section.");
            }
            ColonyCheckpointMetadata.RefuseRemovedNitrogen(payload.Owner);
            ColonyCheckpointMetadata.RefuseUnboundedPressure(payload.Owner);
            ColonyCheckpointMetadata.RefuseExperimentalActivity(payload.Owner);
            var settings = payload.Section(ColonySavePayload.ColonySettings) ??
                throw new NotSupportedException("This R&D save predates the current breathing settings. Start a new colony.");
            _ = ColonySettingsRecord.Decode(settings, SettingValues.Defaults());
            return payload;
        }

        internal static bool TryLoad(IReader reader, ref int result)
        {
            var data = reader.RawBytes(); var position = reader.Position;
            if (position < 0 || position > data.Length - 4) return false;
            var length = BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(position));
            if (length < 8 || length > data.Length - position - 4 || !ColonySaveEnvelope.HasMagic(data.AsSpan(position + 4, length))) return false;
            // The menu checks normally refuse this first (LoadRefusal).
            var packs = GameplayPacks.ActiveUnsupported();
            if (packs.Length != 0) throw new NotSupportedException(ModAdmission.PacksLoadRefusal(packs));
            _ = ValidateCurrentContent(data.AsSpan(position + 4, length));
            var materials = ColonyOwner.BuildCatalogue();
            var envelope = ColonySaveEnvelope.Parse(data.AsSpan(position + 4, length), ColonySaveEnvelope.FirstSupportedBuild, RunningBuild, MaterialModel, CatalogueHash(materials));
            // Once the MACOLONY envelope is recognized, an unknown managed
            // payload is an incompatible mixture save, not a native save.
            var sections = ColonySavePayload.Decode(envelope.CustomData);
            ColonyCallbackRequestImage.ValidateOwner(sections.Owner, sections.Section(ColonySavePayload.CallbackRequests));
            OniNativeColony.Config config;
            fixed (byte* source = sections.Config) config = Marshal.PtrToStructure<OniNativeColony.Config>((IntPtr)source);
            var image = sections.Owner;
            ColonyNativeClock.Load(sections.Section(ColonySavePayload.NativeRuntimeClock));
            ModSettingsRuntime.LoadRecord(sections.Section(ColonySavePayload.ColonySettings));
            if (sections.Section(ColonySavePayload.CarbonParticles) is byte[] particles) ColonySavedParticles.Load(particles, config);
            if (sections.Section(ColonySavePayload.PipeRuntime) is byte[] pipeImage) ColonyPipeSave.Load(pipeImage);
            if (sections.Section(ColonySavePayload.CallbackRequests) is byte[] requests) ColonyCallbackRequests.Load(requests);
            else ColonyCallbackRequests.Unload();
            if (reader.ReadInt32() != length) throw new InvalidDataException("Colony save section changed while loading.");
            _ = reader.ReadBytes(length);
            var native = envelope.NativeData.ToArray(); IntPtr loaded;
            using(var guard=new ColonyNativeLoadGuard(config))
                fixed (byte* pointer = native) loaded = Sim.SIM_HandleMessage(NativeLoad, native.Length, pointer);
            if (loaded == IntPtr.Zero) throw new InvalidDataException("ONI's native loader rejected the colony's native section.");
            ColonyLoadedTiles.Load(config);
            pending = (config, image); result = 0; ++Loads;
            return true;
        }

        // Before ONI leaves the menu (LoadScreen.DoLoad, or the main menu's
        // Resume button): a colony save this build cannot continue is refused
        // there, with what the player should do in plain words. Anything we
        // cannot read as a Mixed Atmospheres save is left to ONI's own loader.
        internal static string? LoadRefusal(string filename)
        {
            var reason = EnvelopeRefusal(filename, out var mixture);
            if (!mixture) return null;
            var packs = GameplayPacks.ActiveUnsupported();
            if (packs.Length != 0) return ModAdmission.PacksLoadRefusal(packs);
            return reason == null ? null : "This colony was saved by a Mixed Atmospheres build that this one cannot continue, so it was not loaded. " +
                "The save has not been changed.\n\n" + reason;
        }

        // Whether a save file holds a Mixed Atmospheres colony (its native
        // section is a MACOLONY envelope). Reads the file only.
        internal static bool IsMixtureSave(string filename) => ReadSimSection(filename) is byte[] sim && ColonySaveEnvelope.HasMagic(sim);

        private static byte[]? ReadSimSection(string filename)
        {
            try { return ColonySaveReadback.SimSection(File.ReadAllBytes(filename)); }
            catch (Exception) { return null; }
        }

        private static string? EnvelopeRefusal(string filename, out bool mixture)
        {
            mixture = false;
            var sim = ReadSimSection(filename);
            if (sim == null || !ColonySaveEnvelope.HasMagic(sim)) return null;
            mixture = true;
            try
            {
                _ = ValidateCurrentContent(sim);
                // The material catalogue is compared once elements exist (the in-game load checks it again).
                var catalogue = ElementLoader.elements != null && ElementLoader.elements.Count != 0 ? CatalogueHash(ColonyOwner.BuildCatalogue()) :
                    sim.Length >= 60 ? sim.AsSpan(28, ColonySaveEnvelope.CatalogHashBytes).ToArray() : new byte[ColonySaveEnvelope.CatalogHashBytes];
                var envelope = ColonySaveEnvelope.Parse(sim, ColonySaveEnvelope.FirstSupportedBuild, RunningBuild, MaterialModel, catalogue);
                var payload = ColonySavePayload.Decode(envelope.CustomData);
                _ = ColonyCheckpointMetadata.Profile(payload.Owner);
                if (payload.Section(ColonySavePayload.ColonySettings) is byte[] settings) _ = ColonySettingsRecord.Decode(settings, SettingValues.Defaults());
                ColonyCallbackRequestImage.ValidateOwner(payload.Owner, payload.Section(ColonySavePayload.CallbackRequests));
                return null;
            }
            catch (Exception error) { return error.Message; }
        }

        // The owner resumes at the first completed frame after the managed
        // cluster exists (ColonyOwner.AfterNativeMessage).
        internal static (OniNativeColony.Config config, byte[] image)? TakePending()
        {
            // Every session passes here before its owner starts or restores.
            ColonyPipeRuntime.Persistable = ColonyPipeSave.WriterEnabled;
            var value = pending; pending = null; return value;
        }
        internal static void Reset() { ColonyLoadedTiles.Reset(); ColonyNativeLoadGuard.Reset(); ColonyNativeClock.Reset(); ColonyCallbackRequests.Reset(); ColonySaveBarrier.Reset(); ColonySavedParticles.Reset(); ColonySaveControls.Reset(); ColonyContinuationProbe.Reset(); ColonyLoadRefusal.Reset(); ColonyPipeSave.Reset(); pending = null; ValidationWriterOwnerSha256 = ValidationWriterPhysicalSha256 = ""; RestoredProfile = null; RestoredImageSha256 = ""; RestoredImageReencoded = null; RestoredReencodeMilliseconds = 0; RestoredReencodeScope = ""; }

        internal static object? RestoredReport() => RestoredProfile is not ColonyPhysicalProfile p ? null : new {
            p.OwnerVersion, p.PhysicalVersion, p.LiquidLaw, p.PressureIterationCap, p.HeatIterationCap, p.GasHeatMixing,
            ImageSha256 = RestoredImageSha256, Reencoded = RestoredImageReencoded, ReencodeScope = RestoredReencodeScope, ReencodeMilliseconds = RestoredReencodeMilliseconds,
            NativeLoadTransitionChecksSuppressed=ColonyNativeLoadGuard.Suppressed,TilesRecreatedFromSavedCells=ColonyLoadedTiles.Preserved };

        // A restore that fails refuses the colony (ColonyLoadRefusal) instead
        // of leaving it to ONI's own fluid simulation.
        internal static void RestoreOwner(OniNativeColony.Config saved, byte[] image)
        {
            if (ColonyLoadRefusal.Refused) return;
            try { ColonyOwner.Restore(saved, image); Restored(image); ColonyPipeSave.TryRestore(); }
            catch (Exception error) { ColonyLoadRefusal.Refuse(error); }
        }

        // Called right after the native restore, before any input or advance.
        internal static void Restored(byte[] image)
        {
            RestoredProfile = ColonyCheckpointMetadata.Profile(image);
            ColonyContinuationProbe.Restored(ColonyCheckpointMetadata.CompletedFrames(image));
            using (var hash = SHA256.Create()) RestoredImageSha256 = BitConverter.ToString(hash.ComputeHash(image)).Replace("-", "").ToLowerInvariant();
            if (ColonyRuntimeValidation.Settings == null) return;
            var started = System.Diagnostics.Stopwatch.GetTimestamp();
            var again = ColonyOwner.Checkpoint();
            RestoredReencodeMilliseconds = (System.Diagnostics.Stopwatch.GetTimestamp() - started) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
            // An older owner wrapper re-encodes in the current wrapper version:
            // then its physical image and durable frame must be identical.
            var savedVersion = ColonyCheckpointMetadata.OwnerVersion(image); var currentVersion = ColonyCheckpointMetadata.OwnerVersion(again);
            RestoredReencodeScope = savedVersion == currentVersion ? "owner image" : $"physical image (owner wrapper {savedVersion} -> {currentVersion})";
            RestoredImageReencoded = savedVersion == currentVersion ? again.AsSpan().SequenceEqual(image) :
                savedVersion < currentVersion && ColonyCheckpointMetadata.PhysicalImage(again).SequenceEqual(ColonyCheckpointMetadata.PhysicalImage(image)) &&
                ColonyCheckpointMetadata.CompletedFrames(again) == ColonyCheckpointMetadata.CompletedFrames(image);
            if (RestoredImageReencoded != true) throw new InvalidDataException("The restored colony owner does not reproduce its saved image.");
        }
    }

    [HarmonyPatch(typeof(LoadScreen), nameof(LoadScreen.DoLoad), new[] { typeof(string) })]
    internal static class ColonyLoadPrecheckPatch
    {
        private static bool Prefix(string __0)
        {
            var reason = ColonyOwnerPersistence.LoadRefusal(__0);
            if (reason == null) return true;
            Refuse(reason);
            return false;
        }

        internal static void Refuse(string reason)
        {
            UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Colony save refused before loading: " + reason);
            if (ColonyRuntimeValidation.Settings != null) { ColonyRuntimeValidation.RecordLoadRefusal(reason); return; }
            try
            {
                var parent = FrontEndManager.Instance != null ? FrontEndManager.Instance.gameObject : GameScreenManager.Instance.ssOverlayCanvas;
                Util.KInstantiateUI(ScreenPrefabs.Instance.ConfirmDialogScreen.gameObject, parent, true).GetComponent<ConfirmDialogScreen>().PopupConfirmDialog(
                    reason, null, null, title_text: "COLONY NOT LOADED");
            }
            catch (Exception error) { UnityEngine.Debug.LogWarning("[Mixed Atmospheres] The load refusal could not be shown: " + error.Message); }
        }
    }

    // The main menu's Resume button loads the latest save without
    // LoadScreen.DoLoad; it gets the same check. The path is chosen as
    // MainMenu.ResumeGame chooses it, without consuming its preference key.
    [HarmonyPatch(typeof(MainMenu), "ResumeGame")]
    internal static class ColonyResumePrecheckPatch
    {
        private static bool Prefix()
        {
            string? path;
            try
            {
                path = KPlayerPrefs.HasKey("AutoResumeSaveFile") ? KPlayerPrefs.GetString("AutoResumeSaveFile") :
                    !string.IsNullOrEmpty(Klei.GenericGameSettings.instance.scriptedProfile.saveGame) ? null : SaveLoader.GetLatestSaveForCurrentDLC();
            }
            catch (Exception error) { UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Resume check skipped: " + error.Message); return true; }
            if (string.IsNullOrEmpty(path)) return true;
            var reason = ColonyOwnerPersistence.LoadRefusal(path!);
            if (reason == null) return true;
            // As ResumeGame would, so the refused save does not resume again
            // by itself every time the menu opens.
            if (KPlayerPrefs.HasKey("AutoResumeSaveFile")) KPlayerPrefs.DeleteKey("AutoResumeSaveFile");
            ColonyLoadPrecheckPatch.Refuse(reason);
            return false;
        }
    }
    [HarmonyPatch(typeof(Sim), nameof(Sim.Save))]
    internal static class ColonyOwnerSavePatch
    {
        private static bool Prefix(BinaryWriter __0, int __1, int __2) => !ColonyOwnerPersistence.TryWriteSave(__0, __1, __2);
    }
    [HarmonyPatch]
    internal static class ColonyOwnerLoadPatch
    {
        private static IEnumerable<MethodBase> TargetMethods() => new[] { AccessTools.Method(typeof(Sim), nameof(Sim.LoadWorld)), AccessTools.Method(typeof(Sim), nameof(Sim.Load)) };
        // VanillaLoad (validation only) leaves the save to ONI's own loader, as a
        // game without the mod would: its native loader must refuse the envelope.
        private static bool Prefix(IReader __0, ref int __result) =>
            ColonyRuntimeValidation.Settings?.VanillaLoad == true || !ColonyOwnerPersistence.TryLoad(__0, ref __result);
        private static void Postfix(int __result) { if (ColonyRuntimeValidation.Settings?.VanillaLoad == true) ColonyRuntimeValidation.RecordVanillaLoad(__result); }
    }
}
