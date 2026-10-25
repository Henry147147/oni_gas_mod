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
}
