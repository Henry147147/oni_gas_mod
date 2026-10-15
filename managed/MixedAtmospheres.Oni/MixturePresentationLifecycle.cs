using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using HarmonyLib;
using Newtonsoft.Json;

namespace MixedAtmospheres.Oni
{
    // Pinned game order: Sim.Shutdown frees native state; Game.DestroyInstances
    // clears Grid before destroying property textures. Either real teardown
    // retires the session first. Texture reset/recreation alone does not.
    [HarmonyPatch]
    internal static class MixturePresentationLifecycle
    {
        private sealed class SessionState
        {
            public bool HasProvider { get; set; }
            public bool HasWorldProvider { get; set; }
            public bool HasModel { get; set; }
            public bool HasCachedFrame { get; set; }
            public bool IsFixture { get; set; }
            public int InventoryProviderCount { get; set; }
        }

        private sealed class ShutdownObservation
        {
            public string Boundary { get; set; } = "";
            public string RecordedUtc { get; set; } = "";
            public SessionState Before { get; set; } = new SessionState();
            public SessionState After { get; set; } = new SessionState();
            public bool OwnedBindingsCleared { get; set; }
            public string CleanupError { get; set; } = "";
            public bool Passed => !After.HasProvider && !After.HasWorldProvider && !After.HasModel &&
                !After.HasCachedFrame && !After.IsFixture && After.InventoryProviderCount == 0 &&
                OwnedBindingsCleared && CleanupError.Length == 0;
        }

        private sealed class ObservationSettings
        {
            internal readonly string OutputDirectory;
            internal readonly Func<bool> IsPresentationMode, BindingsCleared;
            internal readonly List<ShutdownObservation> Observations = new List<ShutdownObservation>();
            internal ObservationSettings(string directory, Func<bool> mode, Func<bool> bindings)
            { OutputDirectory = directory; IsPresentationMode = mode; BindingsCleared = bindings; }
        }

        private static ObservationSettings? observation;
        internal static string LastShutdownObservationError { get; private set; } = "";

        // Private disposable-colony diagnostics opt in explicitly and supply
        // their presentation-mode predicate. Normal mod use writes no report.
        internal static void EnableShutdownObservation(string outputDirectory, Func<bool> isPresentationMode, Func<bool> bindingsCleared)
        {
            if (string.IsNullOrWhiteSpace(outputDirectory)) throw new ArgumentException("An output directory is required.", nameof(outputDirectory));
            observation = new ObservationSettings(Path.GetFullPath(outputDirectory),
                isPresentationMode ?? throw new ArgumentNullException(nameof(isPresentationMode)),
                bindingsCleared ?? throw new ArgumentNullException(nameof(bindingsCleared)));
            LastShutdownObservationError = "";
        }

        internal static void DisableShutdownObservation()
        { observation = null; LastShutdownObservationError = ""; }

        internal static IEnumerable<MethodBase> TargetMethods()
        {
            yield return Require(typeof(Sim), nameof(Sim.Shutdown));
            yield return Require(typeof(Game), "DestroyInstances");
        }

        private static MethodInfo Require(Type type, string name)
        {
            var method = AccessTools.Method(type, name, Type.EmptyTypes);
            if (method == null || method.ReturnType != typeof(void) || method.GetMethodBody() == null)
                throw new NotSupportedException("Mixture presentation teardown contract changed: " + type.Name + "." + name);
            return method;
        }

        private static SessionState CaptureState() => new SessionState
        {
            HasProvider = MixturePresentationRuntime.HasProvider,
            HasWorldProvider = MixturePresentationRuntime.HasWorldProvider,
            HasModel = MixturePresentationRuntime.CurrentModel != null,
            HasCachedFrame = MixturePresentationRuntime.HasCachedFrame,
            IsFixture = MixturePresentationRuntime.IsFixture,
            InventoryProviderCount = MixturePresentationRuntime.InventoryProviderCount
        };

        internal static void EndSession(string boundary = "explicit-call")
        {
            var before = CaptureState();
            MixturePresentationRuntime.Shutdown();
            // Observation failures must never prevent the game's own teardown.
            LastShutdownObservationError = "";
            try
            {
                var settings = observation;
                if (settings == null || !settings.IsPresentationMode()) return;
                var entry = new ShutdownObservation
                {
                    Boundary = boundary, RecordedUtc = System.DateTime.UtcNow.ToString("O"),
                    Before = before, After = CaptureState(), OwnedBindingsCleared = settings.BindingsCleared(),
                    CleanupError = MixturePresentationRuntime.LastShutdownError
                };
                if (settings.Observations.Count == 64) settings.Observations.RemoveAt(0);
                settings.Observations.Add(entry);
                var json = SerializeObservations(settings.Observations);
                Directory.CreateDirectory(settings.OutputDirectory);
                File.WriteAllText(Path.Combine(settings.OutputDirectory, "presentation-shutdown.json"), json);
            }
            catch (Exception error) { LastShutdownObservationError = error.ToString(); }
        }

        private static string SerializeObservations(List<ShutdownObservation> observations)
        {
            // Fixed-schema evidence needs no reflected contracts or changes to
            // Newtonsoft's process-wide settings shared with the game/mods.
            using var output = new StringWriter(CultureInfo.InvariantCulture);
            using (var writer = new JsonTextWriter(output) { Formatting = Formatting.Indented })
            {
                writer.WriteStartObject();
                writer.WritePropertyName("Kind"); writer.WriteValue("mixture-presentation-shutdown-observation");
                writer.WritePropertyName("Passed"); writer.WriteValue(observations.TrueForAll(item => item.Passed));
                writer.WritePropertyName("Observations"); writer.WriteStartArray();
                foreach (var entry in observations)
                {
                    writer.WriteStartObject();
                    writer.WritePropertyName("Boundary"); writer.WriteValue(entry.Boundary);
                    writer.WritePropertyName("RecordedUtc"); writer.WriteValue(entry.RecordedUtc);
                    writer.WritePropertyName("Before"); WriteState(writer, entry.Before);
                    writer.WritePropertyName("After"); WriteState(writer, entry.After);
                    writer.WritePropertyName("OwnedBindingsCleared"); writer.WriteValue(entry.OwnedBindingsCleared);
                    writer.WritePropertyName("CleanupError"); writer.WriteValue(entry.CleanupError);
                    writer.WritePropertyName("Passed"); writer.WriteValue(entry.Passed);
                    writer.WriteEndObject();
                }
                writer.WriteEndArray(); writer.WriteEndObject();
            }
            return output.ToString();
        }

        private static void WriteState(JsonWriter writer, SessionState state)
        {
            writer.WriteStartObject();
            writer.WritePropertyName("HasProvider"); writer.WriteValue(state.HasProvider);
            writer.WritePropertyName("HasWorldProvider"); writer.WriteValue(state.HasWorldProvider);
            writer.WritePropertyName("HasModel"); writer.WriteValue(state.HasModel);
            writer.WritePropertyName("HasCachedFrame"); writer.WriteValue(state.HasCachedFrame);
            writer.WritePropertyName("IsFixture"); writer.WriteValue(state.IsFixture);
            writer.WritePropertyName("InventoryProviderCount"); writer.WriteValue(state.InventoryProviderCount);
            writer.WriteEndObject();
        }

        [HarmonyPrefix, HarmonyPriority(Priority.First)]
        private static void Prefix(MethodBase __originalMethod)
        {
            EndSession(__originalMethod.DeclaringType?.Name + "." + __originalMethod.Name);
            if (MixturePresentationRuntime.LastShutdownError.Length != 0)
                UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Presentation cleanup: " + MixturePresentationRuntime.LastShutdownError);
            if (LastShutdownObservationError.Length != 0)
                UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Presentation cleanup observation: " + LastShutdownObservationError);
        }
    }
}
