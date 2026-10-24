using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // In-flight CO2 particles in owner saves (docs/colony-owner.md#saves).
    // ONI does not save CO2Manager's particles; under the owner their mass is
    // in no ledger until they deposit, so dropping them at a save would destroy
    // it. The save holds every active particle as a MACO2P01 image (exact
    // binary32 fields); a load recreates them through the original factory
    // before the manager's first update or any new spawn, and the recreated
    // pool must re-encode to the saved bytes.
    internal static class ColonySavedParticles
    {
        private static NativeTransientParticleImage? pending;
        private static OniNativeColony.Config pendingConfig;
        private static bool restoring;
        internal static int SavedCount { get; private set; }
        internal static int RestoredCount { get; private set; }
        internal static string SavedSha256 { get; private set; } = "";
        internal static string RestoredSha256 { get; private set; } = "";
        internal static bool Pending => pending != null;
        // Restored particles still in flight; each leaves the pool once (deposit or expiry).
        private static readonly List<CO2> inFlight = new List<CO2>();
        internal static int RetiredCount { get; private set; }
        internal static void Reset() { pending = null; restoring = false; SavedCount = RestoredCount = RetiredCount = 0; SavedSha256 = RestoredSha256 = ""; inFlight.Clear(); }
        internal static object Report() => new { Format = "MACO2P01", SavedCount, SavedSha256, Pending, RestoredCount, RestoredSha256, RetiredCount };
        // Before every manager update or spawn (a spawn may reuse a retired object).
        internal static void Observe()
        {
            RestorePending();
            if (inFlight.Count == 0 || CO2Manager.instance == null) return;
            var live = new HashSet<CO2>(Live());
            RetiredCount += inFlight.RemoveAll(particle => particle == null || !live.Contains(particle));
        }

        private static List<CO2> Live() => AccessTools.Field(typeof(CO2Manager), "co2Items").GetValue(CO2Manager.instance) as List<CO2>
            ?? throw new InvalidOperationException("CO2 particle collection is unavailable.");
        private static OniNativeDomain.Configuration Geometry(OniNativeColony.Config config) => new OniNativeDomain.Configuration
        { GridWidth = config.GridWidth, X = config.OffsetX, Y = config.OffsetY, Width = config.Width, Height = config.Height };

        private static NativeTransientParticleImage CaptureImage(OniNativeColony.Config config)
        {
            var records = new List<NativeTransientParticle>();
            if (CO2Manager.instance != null)
                foreach (var particle in Live())
                {
                    if (particle == null) throw new InvalidOperationException("A destroyed CO2 particle is still in the active pool.");
                    if (particle.GetComponent<SaveLoadRoot>() != null) throw new NotSupportedException("CO2 particles now have their own save root.");
                    var position = particle.transform.GetPosition(); var velocity = particle.velocity;
                    var animation = particle.GetComponent<KBatchedAnimController>() ?? throw new InvalidOperationException("CO2 animation component is missing.");
                    records.Add(new NativeTransientParticle(checked((ulong)records.Count + 1), checked((uint)Grid.PosToCell(position)), animation.FlipX ? 1U : 0U,
                        new[] { position.x, position.y, position.z, velocity.x, velocity.y, velocity.z,
                            particle.mass, particle.temperature, particle.lifetimeRemaining, animation.Rotation }));
                }
            var image = NativeTransientParticleImage.Create(records, Geometry(config));
            image.ValidateGrid(checked((uint)Grid.WidthInCells), checked((uint)Grid.HeightInCells));
            return image;
        }
        private static string Hash(byte[] bytes)
        { using var sha = System.Security.Cryptography.SHA256.Create(); return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant(); }

        // At a settled save: null when no particle is in flight.
        internal static byte[]? Capture(OniNativeColony.Config config)
        {
            RestorePending();
            var image = CaptureImage(config);
            if (image.Particles.Count == 0) return null;
            var bytes = image.CopyEncodedData(); SavedCount = image.Particles.Count; SavedSha256 = Hash(bytes);
            return bytes;
        }

        // At load: validated against the saved geometry, restored later.
        internal static void Load(byte[] bytes, OniNativeColony.Config config)
        {
            pending = NativeTransientParticleImage.Parse(bytes, Geometry(config)); pendingConfig = config;
            pending.ValidateGrid(config.GridWidth, config.GridHeight);
        }

        // Before CO2Manager's first update, its first new spawn, or a save.
        internal static void RestorePending()
        {
            var image = pending;
            if (image == null || restoring || CO2Manager.instance == null) return;
            if (Live().Count != 0) throw new InvalidOperationException("CO2 particles exist before the saved ones were restored.");
            restoring = true;
            try
            {
                foreach (var item in image.Particles)
                {
                    var position = new Vector3(item.X, item.Y, item.Z);
                    var particle = CO2Manager.instance.SpawnCO2(position, item.MassKg, item.Temperature, (item.Flags & 1) != 0, item.Rotation);
                    particle.transform.SetPosition(position);
                    particle.velocity = new Vector3(item.VelocityX, item.VelocityY, item.VelocityZ);
                    particle.lifetimeRemaining = item.LifetimeRemaining;
                    particle.GetComponent<KBatchedAnimController>().Rotation = item.Rotation;
                    inFlight.Add(particle);
                }
                var restored = CaptureImage(pendingConfig).CopyEncodedData();
                if (!restored.AsSpan().SequenceEqual(image.CopyEncodedData()))
                    throw new InvalidOperationException("Restored CO2 particles differ from the saved image.");
                RestoredCount = image.Particles.Count; RestoredSha256 = Hash(restored); pending = null;
            }
            finally { restoring = false; }
        }
    }

    [HarmonyPatch(typeof(CO2Manager), nameof(CO2Manager.Sim33ms))]
    internal static class ColonySavedParticlesUpdatePatch
    {
        private static void Prefix() => ColonySavedParticles.Observe();
    }
    [HarmonyPatch(typeof(CO2Manager), nameof(CO2Manager.SpawnCO2), new[] { typeof(Vector3), typeof(float), typeof(float), typeof(bool), typeof(float) })]
    internal static class ColonySavedParticlesSpawnPatch
    {
        private static void Prefix() => ColonySavedParticles.Observe();
    }
}
