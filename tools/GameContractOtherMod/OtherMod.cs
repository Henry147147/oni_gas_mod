using System;
using System.Reflection;
using HarmonyLib;

// What a mod loaded before Mixed Atmospheres does: Harmony patches, under its
// own ID, on game methods Mixed Atmospheres depends on: two it calls and one
// it patches itself. Two Harmony IDs stand for two mods in one assembly.
public static class OtherMod
{
    public static void Prefix() { }
    public static void Postfix() { }

    public static void Patch()
    {
        var first = new Harmony("example.first-mod");
        var second = new Harmony("example.second-mod");
        var flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance;
        MethodBase Method(Type type, string name, params Type[] parameters) =>
            type.GetMethod(name, flags, null, parameters, null) ?? throw new MissingMethodException(type.FullName, name);
        var prefix = new HarmonyMethod(typeof(OtherMod).GetMethod(nameof(Prefix)));
        var postfix = new HarmonyMethod(typeof(OtherMod).GetMethod(nameof(Postfix)));
        first.Patch(Method(typeof(SaveLoader), "Save", typeof(string), typeof(bool), typeof(bool)), prefix: prefix);
        second.Patch(Method(typeof(SaveLoader), "Save", typeof(string), typeof(bool), typeof(bool)), postfix: postfix);
        first.Patch(Method(typeof(CustomGameSettings), "SetQualitySetting", typeof(Klei.CustomSettings.SettingConfig), typeof(string)), prefix: prefix);
        // MA_OTHER_MOD_LEAVES_LOAD=1 leaves the load entry alone: Mixed Atmospheres is still off, and its
        // guard on LoadScreen.DoLoad and MainMenu.ResumeGame, which it installs only on unpatched
        // entries, can be tested (tools/oni_headless.py --resume-save).
        if (Environment.GetEnvironmentVariable("MA_OTHER_MOD_LEAVES_LOAD") != "1")
            first.Patch(Method(typeof(LoadScreen), "DoLoad", typeof(string)), prefix: prefix);
    }
}

// The same patches as an installed mod, for a game run with this folder
// loaded before Mixed Atmospheres (tools/oni_headless.py --extra-mod-first).
public sealed class OtherModEntry : KMod.UserMod2
{
    public override void OnLoad(Harmony harmony) => OtherMod.Patch();
}
