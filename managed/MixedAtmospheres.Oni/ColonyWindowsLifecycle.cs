using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static class ColonyWindowsLifecycle
    {
        private static bool installed;
        internal static void Install()
        {
            if(installed||!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))return;
            installed=true;Application.quitting+=Stop;
        }
        internal static void Stop()
        {
            if(!RuntimeInformation.IsOSPlatform(OSPlatform.Windows)||!NativeLibraryBootstrap.ObserverLoaded)return;
            try
            {
                var timer=Stopwatch.StartNew();var status=OniNativeColony.Status.Empty;
                OniNativeColony.Check(OniNativeColony.Stop(ref status));
                UnityEngine.Debug.Log($"[Mixed Atmospheres] Windows workers stopped before process exit in {timer.Elapsed.TotalMilliseconds:F3} ms.");
            }
            catch(Exception error){UnityEngine.Debug.LogError("[Mixed Atmospheres] Windows worker shutdown failed: "+error);}
        }
    }
    [HarmonyPatch(typeof(Game),"OnApplicationQuit")]
    internal static class ColonyWindowsQuitPatch
    {
        private static void Postfix()=>ColonyWindowsLifecycle.Stop();
    }
    [HarmonyPatch(typeof(Game),"StopBE")]
    internal static class ColonyWindowsStopTrace
    {
        private static bool Prepare()=>RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
        private static void Prefix()=>UnityEngine.Debug.Log("[Mixed Atmospheres] Windows Game.StopBE entered.");
        private static void Postfix()=>UnityEngine.Debug.Log("[Mixed Atmospheres] Windows Game.StopBE completed.");
    }
    [HarmonyPatch(typeof(Sim),nameof(Sim.Shutdown))]
    internal static class ColonyWindowsSimShutdownTrace
    {
        private static bool Prepare()=>RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
        private static void Prefix()
        {
            UnityEngine.Debug.Log("[Mixed Atmospheres] Windows Sim.Shutdown entered.");
            if(ColonyOwner.Active)
            {
                UnityEngine.Debug.Log("[Mixed Atmospheres] Windows owner workers stopping before native shutdown.");
                ColonyWindowsLifecycle.Stop();
            }
        }
        private static void Postfix()=>UnityEngine.Debug.Log("[Mixed Atmospheres] Windows Sim.Shutdown completed.");
    }
    [HarmonyPatch(typeof(PauseScreen),"TriggerQuitGame")]
    internal static class ColonyWindowsTriggerQuitTrace
    {
        private static bool Prepare()=>RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
        private static void Prefix()=>UnityEngine.Debug.Log("[Mixed Atmospheres] Windows PauseScreen.TriggerQuitGame entered.");
        private static void Postfix()=>UnityEngine.Debug.Log("[Mixed Atmospheres] Windows PauseScreen.TriggerQuitGame completed.");
    }
    [HarmonyPatch(typeof(LoadScreen),"ForceStopGame")]
    internal static class ColonyWindowsForceStopTrace
    {
        private static bool Prepare()=>RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
        private static void Prefix()=>UnityEngine.Debug.Log("[Mixed Atmospheres] Windows LoadScreen.ForceStopGame entered.");
        private static void Postfix()=>UnityEngine.Debug.Log("[Mixed Atmospheres] Windows LoadScreen.ForceStopGame completed.");
    }
}
