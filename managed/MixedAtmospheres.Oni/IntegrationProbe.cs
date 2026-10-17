using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    [DataContract]
    public sealed class IntegrationReport
    {
        [DataMember] public string PackageKind = "integration-probe";
        [DataMember] public string InspectedOniBuild = "744825";
        [DataMember] public string AssemblySha256 = "";
        [DataMember] public string UnityVersion = "";
        [DataMember] public string Platform = "";
        [DataMember] public int PointerBytes;
        [DataMember] public int CellBytes;
        [DataMember] public int FrameMessageBytes;
        [DataMember] public int GameDataUpdateBytes;
        [DataMember] public int GameDataUpdateFields;
        [DataMember] public bool ManagedFrameIntercepted;
        [DataMember] public bool ManagedSnapshotIntercepted;
        [DataMember] public int NativeMessageMethodsPatched;
        [DataMember] public bool DirectNativeMessageIntercepted;
        [DataMember] public bool ManagedSaveIntercepted;
        [DataMember] public bool NativeMixtureRoundtripPassed;
        [DataMember] public bool SettingsSerializationRoundtripPassed;
        [DataMember] public bool NativeTagHelpersVerified;
        [DataMember] public bool AuthoritativeColonySimulationReady = false;
        // Once-per-process main-menu diagnostics: "pending-main-menu",
        // "captured" or "failed". Never one of the integration checks above.
        [DataMember] public string NativeContractCapture = "pending-main-menu";
        [DataMember] public string NativeContractCaptureError = "";
        [DataMember] public string[] MissingRequirements =
        {
            "Intercept every relevant direct native message call, including saves and conduit thermal services",
            "Publish correctly owned GameDataUpdate arrays, event lists, and property texture buffers",
            "Implement solid, liquid, thermal, disease, radiation, backwall, and building callbacks",
            "Integrate selective consumers, pipelines, inventories, and colony save serialization",
            "Pass complete mixture/phase/conversion gameplay and performance acceptance suite"
        };

        public string ToJson() => ReportJson.Serialize(this);
    }

    internal static class IntegrationProbe
    {
        private static string? verifiedAssemblySha256;

        // The loaded assembly cannot change within a process, so the 14 MB hash
        // and layout gate run once; later diagnostics reuse the verified value.
        internal static string VerifyContract()
        {
            var cached = verifiedAssemblySha256;
            if (cached != null) return cached;
            if (IntPtr.Size != 8) throw new PlatformNotSupportedException("The integration probe requires a 64-bit game.");
            string assemblyHash;
            using (var hash = SHA256.Create())
            using (var source = File.OpenRead(typeof(Sim).Assembly.Location))
                assemblyHash = BitConverter.ToString(hash.ComputeHash(source)).Replace("-", "").ToLowerInvariant();
            // Inspected build 744825 has distinct PE hashes on Windows/Linux,
            // but identical Sim/SimMessages declarations and audited imports.
            var inspectedAssembly = assemblyHash == "a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599" ||
                assemblyHash == "6db652990ad1038e86b3493ed43004e12bfd61e62a4be60c787a9a9b9ac52ee1";
            if (!inspectedAssembly ||
                Marshal.SizeOf<Sim.Cell>() != 16 || Marshal.SizeOf<Sim.NewGameFrame>() != 28 ||
                Marshal.SizeOf<Sim.GameDataUpdate>() != 496 ||
                typeof(Sim.GameDataUpdate).GetFields(BindingFlags.Public | BindingFlags.Instance).Length != 77)
                throw new NotSupportedException("This private probe requires the inspected ONI 744825 assembly and layouts. No patches were installed.");
            verifiedAssemblySha256 = assemblyHash;
            return assemblyHash;
        }

        internal static IntegrationReport Run(string verifiedAssemblyHash)
        {
            if(!NitrogenElementTagPatch.Verified)throw new InvalidOperationException("Native tag helper runtime checks have not completed.");
            var frame = typeof(SimMessages).GetMethod(nameof(SimMessages.NewGameFrame), BindingFlags.Public | BindingFlags.Static);
            if (frame?.GetMethodBody() == null) throw new NotSupportedException("Managed frame interception point changed.");
            var report = new IntegrationReport
            {
                UnityVersion = UnityEngine.Application.unityVersion,
                Platform = UnityEngine.Application.platform.ToString(),
                NativeTagHelpersVerified = NitrogenElementTagPatch.Verified,
                AssemblySha256 = verifiedAssemblyHash,
                PointerBytes = IntPtr.Size,
                CellBytes = Marshal.SizeOf<Sim.Cell>(),
                FrameMessageBytes = Marshal.SizeOf<Sim.NewGameFrame>(),
                GameDataUpdateBytes = Marshal.SizeOf<Sim.GameDataUpdate>(),
                GameDataUpdateFields = typeof(Sim.GameDataUpdate).GetFields(BindingFlags.Public | BindingFlags.Instance).Length
            };
            NativeMessagePatch.RequirePatched(frame!);
            NativeMessagePatch.RequirePatched(typeof(Sim).GetMethod(nameof(Sim.HandleMessage))!);
            NativeMessagePatch.RequirePatched(typeof(SimMessages).GetMethod(nameof(SimMessages.SetDebugProperties))!);
            var savePatch = HarmonyLib.Harmony.GetPatchInfo(typeof(Sim).GetMethod(nameof(Sim.Save))!);
            if (savePatch == null || !System.Linq.Enumerable.Any(savePatch.Prefixes,
                item => item.PatchMethod.DeclaringType == typeof(SaveProbePatch)))
                throw new InvalidOperationException("Scoped save interception not installed.");
            report.NativeMessageMethodsPatched = NativeMessagePatch.TargetCount;
            var settings = new AtmosphereSettings
            {
                HardMode = false, MinimumOxygenFraction = 0.17, MaximumOxygenFraction = 0.32,
                MinimumOxygenPartialPressurePa = 16000, MaximumCo2Fraction = 0.004,
                MaximumCo2PartialPressurePa = 450
            };
            var restoredSettings = AtmosphereSettings.FromJson(settings.ToJson());
            if (restoredSettings.HardMode != settings.HardMode ||
                restoredSettings.MinimumOxygenFraction != settings.MinimumOxygenFraction ||
                restoredSettings.MaximumOxygenFraction != settings.MaximumOxygenFraction ||
                restoredSettings.MinimumOxygenPartialPressurePa != settings.MinimumOxygenPartialPressurePa ||
                restoredSettings.MaximumCo2Fraction != settings.MaximumCo2Fraction ||
                restoredSettings.MaximumCo2PartialPressurePa != settings.MaximumCo2PartialPressurePa)
                throw new InvalidOperationException("Atmosphere settings JSON roundtrip failed.");
            report.SettingsSerializationRoundtripPassed = true;

            using var world = new FluidWorld(new WorldConfiguration
            { Width = 2, Height = 1, Gravity = 0, Diffusivity = 0, ThermalConduction = false });
            var air = new Dictionary<string, double>
            { ["oxygen"] = 40 * 0.21 * 0.0319988, ["nitrogen"] = 40 * 0.79 * 0.0280134 };
            world.InitializeCell(0, 0, air, 293.15);
            world.InitializeCell(1, 0, air, 293.15);
            var before = world.Totals();
            ProbeScope.World = world;
            ProbeScope.InterceptedFrames = 0;
            ProbeScope.InterceptedSnapshots = 0;
            ProbeScope.InterceptedDirectMessages = 0;
            ProbeScope.InterceptedSaves = 0;
            byte[] saved;
            try
            {
                // Native calls are redirected only inside this scope;
                // no global Grid sizes, pointers, cells, or game saves are changed.
                SimMessages.NewGameFrame(0.001f, new List<Game.SimActiveRegion>
                {
                    new Game.SimActiveRegion { region = new Pair<Vector2I, Vector2I>(new Vector2I(0, 0), new Vector2I(2, 1)) },
                    new Game.SimActiveRegion { region = new Pair<Vector2I, Vector2I>(new Vector2I(0, 0), new Vector2I(2, 1)) }
                });
                Sim.HandleMessage(SimMessageHashes.PrepareGameData, 0, Array.Empty<byte>());
                SimMessages.SetDebugProperties(new Sim.DebugProperties());
                using var buffer = new MemoryStream();
                using (var writer = new BinaryWriter(buffer, Encoding.UTF8, true)) Sim.Save(writer, 0, 0);
                buffer.Position = 0;
                using (var reader = new BinaryReader(buffer, Encoding.UTF8, true))
                {
                    var size = reader.ReadInt32();
                    if (size != buffer.Length - sizeof(int)) throw new InvalidOperationException("Invalid scoped save size.");
                    saved = reader.ReadBytes(size);
                }
                report.ManagedFrameIntercepted = ProbeScope.InterceptedFrames == 1;
                report.ManagedSnapshotIntercepted = ProbeScope.InterceptedSnapshots == 1;
                report.DirectNativeMessageIntercepted = ProbeScope.InterceptedDirectMessages == 1;
                report.ManagedSaveIntercepted = ProbeScope.InterceptedSaves == 1;
                if (!report.ManagedFrameIntercepted || !report.ManagedSnapshotIntercepted ||
                    !report.DirectNativeMessageIntercepted || !report.ManagedSaveIntercepted)
                    throw new InvalidOperationException("Harmony did not intercept the scoped probe calls.");
            }
            finally { ProbeScope.World = null; }
            using var restored = FluidWorld.Restore(saved);
            var after = restored.Totals();
            foreach (var pair in before.MassesKg)
                if (Math.Abs(after.MassesKg[pair.Key] - pair.Value) > 1e-10)
                    throw new InvalidOperationException("Native mixture mass roundtrip failed.");
            if (!restored.AssessAtmosphere(0, 0).IsBreathable)
                throw new InvalidOperationException("Native atmosphere evaluation failed.");
            report.NativeMixtureRoundtripPassed = true;
            return report;
        }
    }
}
