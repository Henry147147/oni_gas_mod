using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using HarmonyLib;
using UnityEngine;
using UnityEngine.UI;

namespace MixedAtmospheres.Oni
{
    // The settings window (docs/settings.md): an IMGUI window in the status
    // panel's style, opened from the mod's row in ONI's Mods screen, from the
    // status panel or by an optional shortcut. A modal uGUI screen under it
    // keeps clicks, keys and scrolling from reaching the game. In a colony the
    // numeric colony settings show recorded values; the CO2 switch remains editable.
    internal sealed class ModSettingsScreen : MonoBehaviour
    {
        private static ModSettingsScreen? instance;
        private static readonly Color Accent = new Color(.35f, .91f, .91f), Warn = new Color(1, .62f, .3f), Muted = new Color(.72f, .79f, .86f),
            Panel = new Color(.08f, .11f, .16f, .98f), Row = new Color(.12f, .16f, .22f, 1);
        private static readonly string[] Tabs = { "Gameplay", "Display & keys", "Advanced" };
        private static readonly string[] AdvancedTabs = { "Solver", "Breathing thresholds", "Performance", "Display", "Saved presets" };
        private int advancedTab;
        private bool open, openedInColony;
        private int tab;
        private Vector2 scroll;
        private string newName = "", newDescription = "", renameText = "", feedback = "";
        private string workerInput = "";
        private bool feedbackBad;
        private string? renaming, deleting;
        private List<ModSettingsStore.PresetEntry> userPresets = new List<ModSettingsStore.PresetEntry>();
        private ModSettingsBlocker? blocker;
        private GUIStyle? title, text, small, button, field, tagStyle;
        private string autoOpenedIn = "";
        private static readonly string AutoOpen = Environment.GetEnvironmentVariable("MA_SETTINGS_OPEN") ?? ""; // test-only

        internal static bool IsOpen => instance != null && instance.open;
        internal static void Install()
        {
            if (instance != null) return;
            var owner = new GameObject("Mixed Atmospheres settings");
            DontDestroyOnLoad(owner);
            instance = owner.AddComponent<ModSettingsScreen>();
        }
        internal static void Remove() { if (instance != null) { instance.Hide(); Destroy(instance.gameObject); } instance = null; }
        internal static void Open(int page = -1) { if (instance != null) instance.Show(page, true); }

        private static bool InColony => Game.Instance != null && Game.Instance.GameStarted();
        private static bool ReadOnlyColony => InColony;

        private void Show(int page, bool pause)
        {
            if (page >= 0 && page < Tabs.Length) tab = page;
            RefreshPresets();
            if (!open)
            {
                open = true; openedInColony = InColony; feedback = ""; renaming = deleting = null;
                blocker = ModSettingsBlocker.Create(this, pause);
            }
            Debug.Log("[Mixed Atmospheres] Settings window opened (" + Tabs[tab] + (ReadOnlyColony ? ", colony settings read-only" : "") + ").");
        }
        internal void Hide()
        {
            if (!open) return;
            open = false;
            var b = blocker; blocker = null;
            if (b != null) b.Release();
        }
        internal void BlockerClosed() { blocker = null; open = false; }

        private void RefreshPresets()
        {
            try { userPresets = ModSettingsRuntime.Store?.ListPresets() ?? new List<ModSettingsStore.PresetEntry>(); }
            catch (Exception error) { userPresets = new List<ModSettingsStore.PresetEntry>(); Say("Your presets could not be listed: " + error.Message, true); }
        }
        private void Say(string message, bool bad = false) { feedback = message; feedbackBad = bad; if (bad) Debug.LogWarning("[Mixed Atmospheres] Settings: " + message); }

        private int modsWait = -1;
        private void Update()
        {
            // Test-only: MA_SETTINGS_OPEN=<tab number> opens the window once at
            // the main menu and once in a running colony, without pausing it;
            // "mods" opens ONI's Mods screen at the menu and clicks the mod's
            // Settings button there.
            // Menu and colony use different canvases/input stacks. Never keep
            // a modal from the previous context after a load or menu return.
            if (open && openedInColony != InColony) Hide();
            if (AutoOpen.Length == 0 || open) return;
            if (modsWait >= 0)
            {
                var injected = GameObject.Find("MixedAtmospheresSettingsButton");
                if (injected != null) { modsWait = -1; Debug.Log("[Mixed Atmospheres] Mods screen Settings button found; clicking it."); injected.GetComponent<KButton>().SignalClick(KKeyCode.Mouse0); }
                else if (++modsWait > 600) { modsWait = -1; Debug.LogWarning("[Mixed Atmospheres] Mods screen Settings button not found."); }
                return;
            }
            var context = InColony ? (ColonyOwner.Active && ModSettingsRuntime.Colony != null ? "colony" : "") : MainMenu.Instance != null ? "menu" : "";
            if (context.Length == 0 || autoOpenedIn.Contains(context)) return;
            autoOpenedIn += context;
            if (AutoOpen == "mods" && context == "menu")
            {
                try { AccessTools.Method(typeof(MainMenu), "Mods").Invoke(MainMenu.Instance, null); modsWait = 0; }
                catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] The Mods screen could not be opened: " + error.Message); }
                return;
            }
            Show(int.TryParse(AutoOpen, NumberStyles.Integer, CultureInfo.InvariantCulture, out var page) ? page : 0, false);
        }

        // The values the colony tabs show and a new preset saves.
        private static SettingValues Shown => ReadOnlyColony ?
            (ModSettingsRuntime.Colony ?? SettingValues.Defaults()).WithScope(SettingScope.Preference, ModSettingsRuntime.Current) : ModSettingsRuntime.Current;

        private void Styles()
        {
            if (title != null) return;
            var font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            title = new GUIStyle(GUI.skin.label) { font = font, fontSize = 17, fontStyle = FontStyle.Bold };
            text = new GUIStyle(GUI.skin.label) { font = font, fontSize = 14, fontStyle = FontStyle.Normal, wordWrap = true, richText = false };
            small = new GUIStyle(GUI.skin.label) { font = font, fontSize = 12, fontStyle = FontStyle.Normal, wordWrap = true };
            tagStyle = new GUIStyle(GUI.skin.label) { font = font, fontSize = 12, fontStyle = FontStyle.Bold, wordWrap = false };
            button = new GUIStyle(GUI.skin.button) { font = font, fontSize = 13, fontStyle = FontStyle.Normal };
            field = new GUIStyle(GUI.skin.textField) { font = font, fontSize = 14, fontStyle = FontStyle.Normal };
        }

        private void OnGUI()
        {
            if (!open) return;
            GUI.depth = -1000; // above the status panel
            Styles();
            var scale = Mathf.Max(1, Screen.height / 900f);
            var previousMatrix = GUI.matrix; var previousColor = GUI.color; var previousContent = GUI.contentColor;
            try
            {
                GUI.matrix = Matrix4x4.Scale(new Vector3(scale, scale, 1));
                var width = Mathf.Min(860, Screen.width / scale - 32); var height = Mathf.Min(680, Screen.height / scale - 48);
                var rect = new Rect((Screen.width / scale - width) / 2, (Screen.height / scale - height) / 2, width, height);
                GUI.color = Panel; GUI.DrawTexture(rect, Texture2D.whiteTexture); GUI.color = Color.white;
                GUILayout.BeginArea(new Rect(rect.x + 16, rect.y + 12, rect.width - 32, rect.height - 24));
                Header();
                GUILayout.Space(6);
                var selected = GUILayout.Toolbar(tab, Tabs, button!, GUILayout.Height(28));
                if (selected != tab) { tab = selected; scroll = Vector2.zero; }
                GUILayout.Space(6);
                scroll = GUILayout.BeginScrollView(scroll);
                switch (tab)
                {
                    case 0:
                        GUILayout.Label("Duplicants always need enough oxygen. CO₂ continues to mix and displace oxygen when its breathing limits are off.", text!);
                        SettingsTab(SettingCatalog.Breathing, false);
                        GUILayout.Label("Oxygen requirements and optional CO₂ thresholds are under Advanced → Breathing thresholds.", small!);
                        SettingsTab(SettingCatalog.PhaseChange, false);
                        SettingsTab(SettingCatalog.Simulation, false);
                        break;
                    case 1:
                        SettingsTab(SettingCatalog.Display, false); SettingsTab(SettingCatalog.Panel, false); SettingsTab(SettingCatalog.Keys, false); break;
                    case 2:
                        Section("Advanced settings");
                        GUILayout.Label("Technical controls for tuning and experiments. The default solver is recommended for ordinary play.", small!);
                        var selectedAdvanced = GUILayout.Toolbar(advancedTab, AdvancedTabs, button!, GUILayout.Height(28));
                        if (selectedAdvanced != advancedTab) { advancedTab = selectedAdvanced; scroll = Vector2.zero; }
                        if (advancedTab == 0) { SolverPresets(); SettingsTab(SettingCatalog.Solver, true); }
                        else if (advancedTab == 1) SettingsTab(SettingCatalog.Breathing, true);
                        else if (advancedTab == 2) SettingsTab(SettingCatalog.Performance, true);
                        else if (advancedTab == 3) { SettingsTab(SettingCatalog.Display, true); SettingsTab(SettingCatalog.Panel, true); SettingsTab(SettingCatalog.Keys, true); }
                        else PresetsTab();
                        break;
                }
                GUILayout.EndScrollView();
                Footer();
                GUILayout.EndArea();
                // IMGUI consumes keyboard input inside the window; Escape closes it.
                if (Event.current.type == EventType.KeyDown && Event.current.keyCode == KeyCode.Escape) { Event.current.Use(); Hide(); }
            }
            catch (ArgumentException) { /* IMGUI layout mismatch after a change this frame */ }
            finally { GUI.matrix = previousMatrix; GUI.color = previousColor; GUI.contentColor = previousContent; }
        }

        private void Header()
        {
            GUILayout.BeginHorizontal();
            GUI.contentColor = Accent; GUILayout.Label("MIXED ATMOSPHERES · SETTINGS", title!); GUI.contentColor = Color.white;
            GUILayout.FlexibleSpace();
            if (GUILayout.Button("Close", button!, GUILayout.Width(90), GUILayout.Height(26))) Hide();
            GUILayout.EndHorizontal();
            string context;
            if (ReadOnlyColony)
                context = ModSettingsRuntime.Colony == null ? "This colony is not simulated by Mixed Atmospheres; it has no colony settings. Preferences apply at once." :
                    "Changes to CO₂ breathing limits apply to this colony and are saved with it. Evaporation, solver settings and breathing thresholds are fixed for this colony. Display and performance changes apply at once.";
            else
                context = "Choose gameplay settings for new colonies here. Each colony saves its rules. Display and performance changes apply at once.";
            GUILayout.Label(context, small!);
            var experimental = Shown.Experimental(SettingScope.Colony).Select(d => d.Label).ToArray();
            if (experimental.Length != 0)
            {
                GUI.contentColor = Warn;
                GUILayout.Label("Experimental: " + string.Join(", ", experimental) + ". Outside the measured values; results and speed are not verified.", small!);
                GUI.contentColor = Color.white;
            }
            if (ModSettingsRuntime.Notices.Count != 0)
            {
                GUILayout.BeginHorizontal();
                GUI.contentColor = Warn; GUILayout.Label("Attention: " + string.Join("\n", ModSettingsRuntime.Notices), small!); GUI.contentColor = Color.white;
                if (GUILayout.Button("Dismiss", button!, GUILayout.Width(80))) ModSettingsRuntime.Notices.Clear();
                GUILayout.EndHorizontal();
            }
            if (feedback.Length != 0) { GUI.contentColor = feedbackBad ? Warn : Accent; GUILayout.Label(feedback, small!); GUI.contentColor = Color.white; }
        }

        private void Footer()
        {
            if (tab != 2) return;
            GUILayout.Space(4);
            GUILayout.BeginHorizontal();
            GUI.contentColor = Muted;
            GUILayout.Label("Settings folder: " + (ModSettingsRuntime.Store?.Root ?? "unavailable") + (ModSettingsRuntime.Validation ? " (ignored in this validation run)" : ""), small!);
            GUI.contentColor = Color.white;
            if (GUILayout.Button("Open settings folder", button!, GUILayout.Width(170), GUILayout.Height(26))) ModSettingsRuntime.OpenFolder();
            GUILayout.EndHorizontal();
        }

        private void SolverPresets()
        {
            Section("Advanced · Solver presets");
            GUILayout.Label("Apply solver tunables for new colonies. Breathing settings stay as chosen on Gameplay.", small!);
            GUILayout.BeginHorizontal();
            GUI.enabled = !ReadOnlyColony;
            foreach (var preset in BuiltInPresets.All.Where(p => p.Id == "default" || p.Id == "robust" || p.Id == "fast" || p.Id == "precise"))
                if (GUILayout.Button(preset.Name + (preset.Experimental ? " (experimental)" : ""), button!, GUILayout.Height(26)))
                {
                    ModSettingsRuntime.Set(ModSettingsRuntime.Current.WithGroup(SettingCatalog.Solver, preset.Values), "custom");
                    Say("Applied " + preset.Name + " solver settings for new colonies.");
                }
            GUI.enabled = true;
            GUILayout.EndHorizontal();
        }

        private void PresetsTab()
        {
            var colonyLocked = ReadOnlyColony;
            Section("Advanced · Complete presets");
            GUILayout.Label("These replace all colony rules, including gameplay choices. Solver-only presets are on Advanced → Solver.", small!);
            foreach (var preset in BuiltInPresets.All)
            {
                RowStart();
                GUILayout.BeginVertical();
                PresetTitle(preset.Name, preset.Experimental, preset.Id == ModSettingsRuntime.CurrentPresetId);
                GUILayout.Label(preset.Description, small!);
                GUILayout.EndVertical();
                GUI.enabled = !colonyLocked;
                if (GUILayout.Button("Load", button!, GUILayout.Width(70), GUILayout.Height(26))) Load(preset);
                GUI.enabled = true;
                if (GUILayout.Button("Copy", button!, GUILayout.Width(70), GUILayout.Height(26))) Export(preset.Name, preset.Description, preset.Values, false);
                RowEnd();
            }
            Section("Your presets");
            if (userPresets.Count == 0) GUILayout.Label("None yet. Save the current settings below, import a preset from the clipboard, or put preset files in the presets folder.", small!);
            foreach (var entry in userPresets.ToArray())
            {
                RowStart();
                GUILayout.BeginVertical();
                if (entry.Preset == null)
                {
                    GUI.contentColor = Warn; GUILayout.Label(entry.Id + ": cannot be used (" + entry.Error + ")", text!); GUI.contentColor = Color.white;
                }
                else if (renaming == entry.Id)
                {
                    GUILayout.BeginHorizontal();
                    renameText = GUILayout.TextField(renameText, ModSettingsStore.MaximumNameLength, field!, GUILayout.Width(300));
                    if (GUILayout.Button("Rename", button!, GUILayout.Width(80))) Act(() => { var id = ModSettingsRuntime.Store!.RenamePreset(entry.Id, renameText); Follow(entry.Id, id); renaming = null; return "Renamed to \"" + renameText.Trim() + "\"."; });
                    if (GUILayout.Button("Cancel", button!, GUILayout.Width(80))) renaming = null;
                    GUILayout.EndHorizontal();
                }
                else
                {
                    PresetTitle(entry.Preset.Name, entry.Preset.Experimental, entry.Id == ModSettingsRuntime.CurrentPresetId);
                    if (entry.Preset.Description.Length != 0) GUILayout.Label(entry.Preset.Description, small!);
                }
                GUILayout.EndVertical();
                if (entry.Preset != null && renaming != entry.Id)
                {
                    GUI.enabled = !colonyLocked;
                    if (GUILayout.Button("Load", button!, GUILayout.Width(62), GUILayout.Height(26))) Load(entry.Preset);
                    GUI.enabled = true;
                    if (GUILayout.Button("Overwrite", button!, GUILayout.Width(80), GUILayout.Height(26)))
                        Act(() => { ModSettingsRuntime.Store!.SavePreset(entry.Preset.Name, entry.Preset.Description, Shown, true, entry.Id); return "Saved the shown settings into \"" + entry.Preset.Name + "\"."; });
                    if (GUILayout.Button("Rename", button!, GUILayout.Width(70), GUILayout.Height(26))) { renaming = entry.Id; renameText = entry.Preset.Name; }
                    if (GUILayout.Button("Copy", button!, GUILayout.Width(56), GUILayout.Height(26))) Export(entry.Preset.Name, entry.Preset.Description, entry.Preset.Values, entry.Preset.HasPreferences);
                }
                if (renaming != entry.Id)
                {
                    if (deleting == entry.Id)
                    {
                        GUI.contentColor = Warn;
                        if (GUILayout.Button("Confirm", button!, GUILayout.Width(74), GUILayout.Height(26)))
                            Act(() => { ModSettingsRuntime.Store!.DeletePreset(entry.Id); deleting = null; return "Deleted \"" + (entry.Preset?.Name ?? entry.Id) + "\"."; });
                        GUI.contentColor = Color.white;
                    }
                    else if (GUILayout.Button("Delete", button!, GUILayout.Width(74), GUILayout.Height(26))) deleting = entry.Id;
                }
                RowEnd();
            }
            Section("Save and share");
            GUILayout.BeginHorizontal();
            GUILayout.Label("Name", text!, GUILayout.Width(90));
            newName = GUILayout.TextField(newName, ModSettingsStore.MaximumNameLength, field!, GUILayout.Width(300));
            if (GUILayout.Button(colonyLocked ? "Save this colony's settings" : "Save current settings", button!, GUILayout.Width(220), GUILayout.Height(26)))
                Act(() => { var id = ModSettingsRuntime.Store!.SavePreset(newName, newDescription, Shown, false); if (!colonyLocked) ModSettingsRuntime.Set(ModSettingsRuntime.Current, id); newName = newDescription = ""; return "Saved preset \"" + id + "\"."; });
            GUILayout.EndHorizontal();
            GUILayout.BeginHorizontal();
            GUILayout.Label("Description", text!, GUILayout.Width(90));
            newDescription = GUILayout.TextField(newDescription, 200, field!, GUILayout.Width(520));
            GUILayout.EndHorizontal();
            GUILayout.Space(4);
            GUILayout.BeginHorizontal();
            if (GUILayout.Button("Import from clipboard", button!, GUILayout.Height(26)))
                Act(() => { var warnings = new List<string>(); var id = ModSettingsRuntime.Store!.ImportPreset(GUIUtility.systemCopyBuffer ?? "", warnings);
                    return "Imported preset \"" + id + "\"." + (warnings.Count == 0 ? "" : " " + string.Join(" ", warnings)); });
            if (GUILayout.Button("Refresh list", button!, GUILayout.Height(26))) { RefreshPresets(); Say("Preset list refreshed."); }
            GUI.enabled = !colonyLocked;
            if (GUILayout.Button("Reset colony settings to Default", button!, GUILayout.Height(26))) Load(BuiltInPresets.Default);
            GUI.enabled = true;
            if (GUILayout.Button("Reset preferences", button!, GUILayout.Height(26)))
            { ModSettingsRuntime.Set(ModSettingsRuntime.Current.WithScope(SettingScope.Preference, SettingValues.Defaults()), ModSettingsRuntime.CurrentPresetId); Say("Preferences reset to their defaults."); }
            GUILayout.EndHorizontal();
            GUILayout.Label("Copy puts a preset on the clipboard to share; Import reads one from the clipboard. Preset files are plain JSON in the presets folder.", small!);
        }

        private void PresetTitle(string name, bool experimental, bool current)
        {
            GUILayout.BeginHorizontal();
            GUILayout.Label(name, text!, GUILayout.ExpandWidth(false));
            if (current) { GUI.contentColor = Accent; GUILayout.Label(ReadOnlyColony ? "for new colonies" : "current", tagStyle!, GUILayout.ExpandWidth(false)); }
            if (experimental) { GUI.contentColor = Warn; GUILayout.Label("experimental", tagStyle!, GUILayout.ExpandWidth(false)); }
            GUI.contentColor = Color.white;
            GUILayout.FlexibleSpace();
            GUILayout.EndHorizontal();
        }

        private void Load(SettingsPreset preset)
        {
            if (ReadOnlyColony) return;
            ModSettingsRuntime.Set(preset.ApplyTo(ModSettingsRuntime.Current), preset.Id);
            Say("Loaded \"" + preset.Name + "\"" + (preset.HasPreferences ? " (colony settings and preferences)." : " (colony settings).") + " New colonies use it.");
        }
        private void Export(string name, string description, SettingValues values, bool preferences)
        {
            GUIUtility.systemCopyBuffer = ModSettingsStore.PresetText(name, description, values, preferences);
            Say("\"" + name + "\" is on the clipboard as preset text.");
        }
        private void Act(Func<string> action)
        {
            try { Say(action()); }
            catch (Exception error) when (error is ArgumentException || error is IOException || error is UnauthorizedAccessException || error is FormatException || error is NotSupportedException)
            { Say(error.Message, true); }
            RefreshPresets();
        }
        // Keeps the current choice pointing at a renamed preset.
        private static void Follow(string from, string to)
        { if (ModSettingsRuntime.CurrentPresetId == from) ModSettingsRuntime.Set(ModSettingsRuntime.Current, to); }

        private void SettingsTab(string group, bool advanced)
        {
            var definitions = SettingCatalog.All.Where(d => d.Group == group && d.Advanced == advanced).ToArray();
            if (definitions.Length == 0) return;
            Section((advanced ? "Advanced · " : "") + group);
            var colony = group == SettingCatalog.Solver || group == SettingCatalog.Breathing || group == SettingCatalog.PhaseChange || group == SettingCatalog.Simulation;
            var locked = colony && ReadOnlyColony;
            var values = Shown;
            if (group == SettingCatalog.Breathing && advanced)
                GUILayout.Label("Oxygen requirements always apply. CO₂ thresholds apply only when CO₂ breathing limits is On in Gameplay.", small!);
            if (group == SettingCatalog.Solver)
                GUILayout.Label("The supported solver and mixed pipes are always enabled. These controls are saved with the colony. Values marked experimental were not measured.", small!);
            foreach (var d in definitions)
            {
                var value = values[d.Key];
                var rowLocked = locked && d.Key != SettingCatalog.Co2Limits;
                RowStart();
                GUILayout.BeginVertical();
                GUILayout.BeginHorizontal();
                GUILayout.Label(d.Label, text!, GUILayout.Width(300));
                if (d.Key == "performance.workerThreads")
                {
                    GUILayout.Label(Display(d, value), text!, GUILayout.Width(170));
                    workerInput = GUILayout.TextField(workerInput, 3, field!, GUILayout.Width(55));
                    if (GUILayout.Button("Set", button!, GUILayout.Width(44), GUILayout.Height(24)))
                    {
                        if (int.TryParse(workerInput, NumberStyles.Integer, CultureInfo.InvariantCulture, out var count) && count >= 1 && count <= 256)
                            Change(d, (double)count);
                        else Say("Enter a whole worker count from 1 to 256, or choose Auto for physical cores.", true);
                    }
                    if (GUILayout.Button("Auto", button!, GUILayout.Width(48), GUILayout.Height(24))) { Change(d, 0.0); workerInput = ""; }
                }
                else if (d.Kind == SettingKind.Toggle)
                {
                    GUI.enabled = !rowLocked && (!ReadOnlyColony || !colony || ModSettingsRuntime.Colony != null);
                    if (GUILayout.Button((bool)value ? "On" : "Off", button!, GUILayout.Width(170), GUILayout.Height(26))) Change(d, !(bool)value);
                    GUI.enabled = true;
                }
                else
                {
                    if (!rowLocked && GUILayout.Button("<", button!, GUILayout.Width(30), GUILayout.Height(24))) Change(d, Step(d, value, -1));
                    GUILayout.Label(Display(d, value), text!, GUILayout.Width(170));
                    if (!rowLocked && GUILayout.Button(">", button!, GUILayout.Width(30), GUILayout.Height(24))) Change(d, Step(d, value, 1));
                }
                if (d.IsExperimental(value)) { GUI.contentColor = Warn; GUILayout.Label("experimental", tagStyle!, GUILayout.ExpandWidth(false)); GUI.contentColor = Color.white; }
                else if (d.Key != "performance.workerThreads" && !Same(value, d.Default)) { GUI.contentColor = Muted; GUILayout.Label("default " + Display(d, d.Default), tagStyle!, GUILayout.ExpandWidth(false)); GUI.contentColor = Color.white; }
                GUILayout.FlexibleSpace();
                GUILayout.EndHorizontal();
                GUI.contentColor = Muted; GUILayout.Label(d.Description, small!); GUI.contentColor = Color.white;
                GUILayout.EndVertical();
                RowEnd();
            }
            if (group == SettingCatalog.Breathing && advanced && values.Number("breathing.maximumOxygenFraction") < 1)
            {
                GUI.contentColor = Warn;
                GUILayout.Label("An oxygen ceiling below 100% makes pure generated oxygen unbreathable. Starting air is not diluted automatically.", small!);
                GUI.contentColor = Color.white;
            }
            if (locked && (advanced || group == SettingCatalog.PhaseChange || group == SettingCatalog.Simulation)) GUILayout.Label("Read-only: this colony keeps the values above. Set them from the main menu for new colonies.", small!);
        }

        private void Change(SettingDefinition d, object value)
        {
            try
            {
                if (d.Key == SettingCatalog.Co2Limits)
                {
                    ModSettingsRuntime.SetCarbonDioxideLimits((bool)value);
                    Say("CO₂ breathing limits: " + ((bool)value ? "On" : "Off") + (ReadOnlyColony ? " (this colony)." : " (new colonies)."));
                    return;
                }
                var values = ModSettingsRuntime.Current.With(d.Key, value);
                var id = d.Scope == SettingScope.Colony ? Matching(values) : ModSettingsRuntime.CurrentPresetId;
                ModSettingsRuntime.Set(values, id);
                Say(d.Label + ": " + Display(d, value) + (d.Scope == SettingScope.Colony ? " (for new colonies)." : "."));
            }
            catch (FormatException error) { Say(error.Message, true); }
        }
        // The preset the colony settings now equal, or "custom".
        private string Matching(SettingValues values)
        {
            foreach (var p in BuiltInPresets.All) if (p.Values.ScopeEquals(SettingScope.Colony, values)) return p.Id;
            foreach (var e in userPresets) if (e.Preset != null && e.Preset.Values.ScopeEquals(SettingScope.Colony, values)) return e.Id;
            return "custom";
        }

        private static object Step(SettingDefinition d, object value, int direction)
        {
            switch (d.Kind)
            {
                case SettingKind.Toggle: return !(bool)value;
                case SettingKind.Choice:
                    var i = Array.IndexOf(d.Choices, (string)value);
                    return d.Choices[((i < 0 ? 0 : i) + direction + d.Choices.Length) % d.Choices.Length];
                default:
                    var x = (double)value; var steps = d.Steps;
                    if (direction > 0) { foreach (var s in steps) if (s > x * (1 + 1e-12) && s > x) return s; return steps[steps.Length - 1]; }
                    for (var k = steps.Length - 1; k >= 0; --k) if (steps[k] < x * (1 - 1e-12) && steps[k] < x) return steps[k];
                    return steps[0];
            }
        }
        private static bool Same(object a, object b) => a is double x && b is double y ? x == y : a.Equals(b);

        internal static string Display(SettingDefinition d, object value)
        {
            if (d.Key == "performance.workerThreads" && (double)value == 0)
                return ModSettingsRuntime.PhysicalCores == 0 ? "Physical cores" : "Physical cores (" + ModSettingsRuntime.PhysicalCores + ")";
            switch (value)
            {
                case bool b: return b ? "On" : "Off";
                case string s: return d.ChoiceLabels.Length == d.Choices.Length && Array.IndexOf(d.Choices, s) >= 0 ? d.ChoiceLabels[Array.IndexOf(d.Choices, s)] : s;
            }
            var v = (double)value;
            if (d.Key.EndsWith("Fraction", StringComparison.Ordinal) && d.Group == SettingCatalog.Breathing)
                return d.Key == "breathing.maximumOxygenFraction" && v >= 1 ? "No limit" : (v * 100).ToString("0.###", CultureInfo.InvariantCulture) + "%";
            if (d.Key == "display.traceMoleFraction" || d.Key == "solver.interfaceDetail") return (v * 100).ToString("0.###", CultureInfo.InvariantCulture) + "%" + (d.Unit.Length != 0 ? " " + d.Unit : "");
            if (d.Key == "display.patternContrast" && v == 0) return "Off";
            if (d.Unit == "Pa") return v >= 1000 ? (v / 1000).ToString("0.###", CultureInfo.InvariantCulture) + " kPa" : v.ToString("0", CultureInfo.InvariantCulture) + " Pa";
            return ModSettingsText.Number(v) + (d.Unit.Length != 0 ? " " + d.Unit : "");
        }

        private void Section(string name)
        {
            GUILayout.Space(8);
            GUI.contentColor = Accent; GUILayout.Label(name.ToUpperInvariant(), tagStyle!); GUI.contentColor = Color.white;
        }
        private static void RowStart()
        {
            GUILayout.BeginHorizontal();
            GUILayout.Space(4);
        }
        private static void RowEnd()
        {
            GUILayout.Space(4);
            GUILayout.EndHorizontal();
            var last = GUILayoutUtility.GetLastRect();
            if (Event.current.type == EventType.Repaint) { var c = GUI.color; GUI.color = Row; GUI.DrawTexture(new Rect(last.x, last.yMax + 2, last.width, 1), Texture2D.whiteTexture); GUI.color = c; }
            GUILayout.Space(6);
        }
    }

    // The modal uGUI layer under the settings window: it takes clicks,
    // scrolling and keys (Escape closes) so they never reach the game, and
    // pauses a running colony while the window is open, like ONI's own dialogs.
    internal sealed class ModSettingsBlocker : KModalScreen
    {
        private ModSettingsScreen? owner;
        private bool released;
        internal static ModSettingsBlocker? Create(ModSettingsScreen owner, bool pause)
        {
            try
            {
                var parent = GameScreenManager.Instance != null && GameScreenManager.Instance.ssOverlayCanvas != null ? GameScreenManager.Instance.ssOverlayCanvas :
                    FrontEndManager.Instance != null ? FrontEndManager.Instance.gameObject : null;
                if (parent == null || KScreenManager.Instance == null) return null;
                var go = new GameObject("Mixed Atmospheres settings blocker", typeof(RectTransform));
                go.SetActive(false);
                go.transform.SetParent(parent.transform, false);
                var rect = (RectTransform)go.transform;
                rect.anchorMin = Vector2.zero; rect.anchorMax = Vector2.one; rect.offsetMin = rect.offsetMax = Vector2.zero;
                var canvas = go.AddComponent<Canvas>(); canvas.overrideSorting = true; canvas.sortingOrder = 30000;
                var shield = go.AddComponent<Image>();
                shield.color = Color.clear; shield.raycastTarget = true;
                go.AddComponent<GraphicRaycaster>();
                var screen = go.AddComponent<ModSettingsBlocker>();
                screen.owner = owner; screen.pause = pause;
                go.SetActive(true);
                return screen;
            }
            catch (Exception error)
            {
                Debug.LogWarning("[Mixed Atmospheres] The settings window could not block game input: " + error.Message);
                return null;
            }
        }
        public override float GetSortKey() => 150f;
        internal void Release()
        {
            if (released) return;
            released = true;
            try { Deactivate(); } catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] Settings blocker: " + error.Message); if (gameObject != null) Destroy(gameObject); }
        }
        protected override void OnDeactivate()
        {
            base.OnDeactivate();
            if (!released) { released = true; owner?.BlockerClosed(); }
        }
    }

    // A "Settings" button on the mod's own row in ONI's Mods screen.
    [HarmonyPatch(typeof(ModsScreen), "BuildDisplay")]
    internal static class ModsScreenSettingsButtonPatch
    {
        private static bool logged;
        private static readonly AccessTools.FieldRef<ModsScreen, IList> Displayed = AccessTools.FieldRefAccess<ModsScreen, IList>("displayedMods");
        private static void Postfix(ModsScreen __instance)
        {
            try
            {
                var mods = Global.Instance?.modManager?.mods;
                if (mods == null || Mod.StaticId.Length == 0) return;
                foreach (var row in Displayed(__instance))
                {
                    var type = row.GetType();
                    var index = (int)AccessTools.Field(type, "mod_index").GetValue(row);
                    if (index < 0 || index >= mods.Count || mods[index].staticID != Mod.StaticId) continue;
                    var transform = (RectTransform)AccessTools.Field(type, "rect_transform").GetValue(row);
                    var manage = transform.GetComponent<HierarchyReferences>().GetReference<KButton>("ManageButton");
                    var settings = Util.KInstantiateUI(manage.gameObject, manage.transform.parent.gameObject, true);
                    settings.name = "MixedAtmospheresSettingsButton";
                    settings.transform.SetSiblingIndex(manage.transform.GetSiblingIndex());
                    var label = settings.GetComponentInChildren<LocText>();
                    if (label != null) label.text = "Settings";
                    var tip = settings.GetComponent<ToolTip>();
                    if (tip != null) tip.toolTip = "Mixed Atmospheres settings and presets";
                    var button = settings.GetComponent<KButton>();
                    button.isInteractable = true;
                    button.onClick += () => ModSettingsScreen.Open(0);
                    if (!logged) { logged = true; Debug.Log("[Mixed Atmospheres] Settings button added to the Mods screen."); }
                    return;
                }
            }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] The Mods screen settings button could not be added: " + error.Message); }
        }
    }
}
