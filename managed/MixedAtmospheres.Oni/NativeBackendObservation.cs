using System;
using System.IO;
using Newtonsoft.Json;

namespace MixedAtmospheres.Oni
{
    // Explicit private diagnostic mode. The normal probe does not load hooks.
    // All native hooks forward; this cannot activate custom fluid ownership.
    internal static class NativeBackendObservation
    {
        private sealed class Settings
        {
            public int SchemaVersion = 1;
            public bool Enabled = false;
        }
        private static OniNativeObserver? observer;
        private static string reportPath = "";
        private static System.DateTime nextWrite;
        private static bool diagnosticFaulted;
        internal static bool Enabled => observer != null && !diagnosticFaulted;
        internal static OniNativeObserver.Reading Capture() => observer?.Capture()
            ?? throw new InvalidOperationException("Native observer is not enabled.");
        internal static void Flush() => WriteReport();

        internal static void Initialize(string modDirectory)
        {
            var file = Path.Combine(modDirectory, "native-backend-observation.json");
            if (!File.Exists(file)) return;
            if (new FileInfo(file).Length > 4096) throw new InvalidDataException("Native observer configuration exceeds 4096 bytes.");
            var settings = JsonConvert.DeserializeObject<Settings>(File.ReadAllText(file), new JsonSerializerSettings {
                MissingMemberHandling = MissingMemberHandling.Error, TypeNameHandling = TypeNameHandling.None
            }) ?? throw new InvalidDataException("Native observer configuration is empty.");
            if (settings.SchemaVersion != 1) throw new NotSupportedException("Unsupported native observer configuration version.");
            if (!settings.Enabled) return;
            if (observer != null) throw new InvalidOperationException("Native observer already initialized.");
            var managed = Path.GetDirectoryName(typeof(Sim).Assembly.Location)
                ?? throw new InvalidOperationException("Cannot locate the loaded game assembly.");
            var game = Directory.GetParent(managed)?.Parent?.FullName
                ?? throw new InvalidOperationException("Cannot locate the loaded game's root.");
            NativeLibraryBootstrap.LoadObserver(modDirectory);
            observer = new OniNativeObserver(game);
            reportPath = Path.Combine(modDirectory, "native-backend-observation-report.json");
            WriteReport();
            UnityEngine.Debug.Log("[Mixed Atmospheres] Native SimDLL forwarding observer attached. Original colony physics remains active.");
        }
        internal static void AfterNativeMessage(int message)
        {
            if (observer == null || diagnosticFaulted || message != 1078620451 || System.DateTime.UtcNow < nextWrite) return;
            try { WriteReport(); }
            catch (Exception error)
            {
                diagnosticFaulted = true;
                UnityEngine.Debug.LogError("[Mixed Atmospheres] Native observer diagnostics failed: " + error);
            }
        }
        private static void WriteReport()
        {
            if (observer == null) return;
            var reading = observer.Capture();
            var report = new {
                SchemaVersion = 1, Status = "observing", OriginalNativePhysicsActive = true,
                BoundedNativeDomainActive = !reading.ForwardingOnly,
                AuthoritativeFluidOwner = false, Observation = reading,
                HookNames = OniNativeObserver.HookNames, Trace = observer.CaptureTrace()
            };
            File.WriteAllText(reportPath, ReportJson.Serialize(report));
            nextWrite = System.DateTime.UtcNow.AddSeconds(1);
        }
        internal static void ResetAfterFailedLoad()
        {
            try
            {
                observer?.Dispose();
                observer = null;
            }
            finally
            {
                if (NativeLibraryBootstrap.ObserverLoaded)
                {
                    var status = OniNativeObserver.CaptureStatus();
                    if (status.RecoveryRequired || status.Attached)
                    {
                        UnityEngine.Application.Quit(1);
                        throw new InvalidOperationException("Native observer restoration is incomplete; restarting the process is required.");
                    }
                }
            }
        }
    }
}
