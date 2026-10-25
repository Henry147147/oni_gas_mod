using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // Settings files (docs/settings.md): <root>/config.json and
    // <root>/presets/<id>.json, where the game passes
    // <ONI user folder>/mods/config/MixedAtmospheres. The folder lies outside
    // the mod's own folder, so reinstalling or updating the mod keeps it.
    // Nothing here ever throws to the game for a bad file: the caller gets
    // Default and a notice instead.
    internal sealed class ModSettingsStore
    {
        internal const string SettingsFormat = "MixedAtmospheres.Settings", PresetFormat = "MixedAtmospheres.Preset";
        internal const int FileVersion = 1, MaximumNameLength = 48;
        internal string Root { get; }
        internal string ConfigPath => Path.Combine(Root, "config.json");
        internal string PresetDirectory => Path.Combine(Root, "presets");
        private bool backupInvalidConfig;

        internal ModSettingsStore(string root)
        {
            if (string.IsNullOrWhiteSpace(root)) throw new ArgumentException("A settings folder is required.", nameof(root));
            Root = root;
        }

        internal sealed class Loaded
        {
            internal SettingValues Values = SettingValues.Defaults();
            internal string PresetId = BuiltInPresets.DefaultId;
            // Shown to the player: the file could not be used.
            internal readonly List<string> Notices = new List<string>();
            // Logged only: entries this build does not know were ignored.
            internal readonly List<string> Warnings = new List<string>();
        }

        // config.json, or Default with a notice when it cannot be used. A
        // first start without config.json takes the pattern contrast from
        // the older appearance.json.
        internal Loaded Load()
        {
            var result = new Loaded();
            try
            {
                if (!File.Exists(ConfigPath)) { result.Values = MigrateAppearance(result.Values, result.Notices); return result; }
                var parsed = ReadDocument(File.ReadAllText(ConfigPath, Encoding.UTF8), SettingsFormat, SettingValues.Defaults(), result.Warnings);
                result.Values = parsed.values;
                result.PresetId = parsed.preset ?? BuiltInPresets.DefaultId;
            }
            catch (Exception error) when (error is IOException || error is UnauthorizedAccessException || error is FormatException || error is NotSupportedException)
            {
                result.Values = SettingValues.Defaults(); result.PresetId = BuiltInPresets.DefaultId; backupInvalidConfig = File.Exists(ConfigPath);
                result.Notices.Add("Your settings file could not be used, so Default applies (" + error.Message + ")" +
                    (backupInvalidConfig ? " It is kept as config.json.invalid when you next change a setting." : ""));
            }
            return result;
        }

        internal void Save(SettingValues values, string presetId)
        {
            Directory.CreateDirectory(Root);
            if (backupInvalidConfig && File.Exists(ConfigPath))
            {
                var backup = ConfigPath + ".invalid";
                if (File.Exists(backup)) File.Delete(backup);
                File.Move(ConfigPath, backup);
            }
            backupInvalidConfig = false;
            WriteAtomic(ConfigPath, ModSettingsJson.Write(new[]
            {
                Member("Format", SettingsFormat), Member("Version", (double)FileVersion), Member("Preset", presetId),
                Member("Colony", values.Members(SettingScope.Colony)), Member("Preferences", values.Members(SettingScope.Preference))
            }));
        }

        // A saved or imported preset that cannot be read is listed with its error.
        internal sealed class PresetEntry
        {
            internal string Id = "";
            internal SettingsPreset? Preset;
            internal string Error = "";
        }

        internal List<PresetEntry> ListPresets()
        {
            var list = new List<PresetEntry>();
            if (!Directory.Exists(PresetDirectory)) return list;
            string[] files;
            try { files = Directory.GetFiles(PresetDirectory, "*.json"); }
            catch (Exception error) when (error is IOException || error is UnauthorizedAccessException)
            { list.Add(new PresetEntry { Id = "presets", Error = error.Message }); return list; }
            foreach (var file in files.OrderBy(f => f, StringComparer.OrdinalIgnoreCase))
            {
                var id = Path.GetFileNameWithoutExtension(file);
                var entry = new PresetEntry { Id = id };
                try { entry.Preset = ParsePreset(id, File.ReadAllText(file, Encoding.UTF8), new List<string>()); }
                catch (Exception error) when (error is IOException || error is UnauthorizedAccessException || error is FormatException || error is NotSupportedException)
                { entry.Error = error.Message; }
                list.Add(entry);
            }
            return list;
        }

        internal SettingsPreset? FindUserPreset(string id) =>
            ListPresets().FirstOrDefault(p => string.Equals(p.Id, id, StringComparison.OrdinalIgnoreCase))?.Preset;

        // A user preset's text: built-in ids are reserved; names are kept as given.
        internal static SettingsPreset ParsePreset(string id, string text, List<string> warnings)
        {
            var parsed = ReadDocument(text, PresetFormat, SettingValues.Defaults(), warnings);
            var name = parsed.name?.Trim() ?? "";
            if (name.Length == 0) name = id;
            if (name.Length > MaximumNameLength) name = name.Substring(0, MaximumNameLength);
            return new SettingsPreset(id, name, parsed.description ?? "", false, parsed.values, parsed.hasPreferences);
        }

        internal static string PresetText(string name, string description, SettingValues values, bool includePreferences = true)
        {
            var members = new List<KeyValuePair<string, object?>>
            {
                Member("Format", PresetFormat), Member("Version", (double)FileVersion), Member("Name", name), Member("Description", description),
                Member("Colony", values.Members(SettingScope.Colony))
            };
            if (includePreferences) members.Add(Member("Preferences", values.Members(SettingScope.Preference)));
            return ModSettingsJson.Write(members);
        }

        // Writes (or, with overwrite, replaces) a preset; returns its id.
        internal string SavePreset(string name, string description, SettingValues values, bool overwrite, string? id = null)
        {
            name = CleanName(name);
            id ??= Slug(name);
            if (BuiltInPresets.IsBuiltInId(id)) throw new ArgumentException("\"" + name + "\" is the name of a built-in preset.");
            Directory.CreateDirectory(PresetDirectory);
            var path = PathOf(id);
            if (File.Exists(path) && !overwrite) throw new ArgumentException("A preset named \"" + name + "\" already exists.");
            WriteAtomic(path, PresetText(name, description, values));
            return id;
        }

        internal string RenamePreset(string id, string newName)
        {
            var preset = FindUserPreset(id) ?? throw new ArgumentException("The preset \"" + id + "\" could not be read.");
            newName = CleanName(newName);
            var next = Slug(newName);
            if (BuiltInPresets.IsBuiltInId(next)) throw new ArgumentException("\"" + newName + "\" is the name of a built-in preset.");
            if (!string.Equals(next, id, StringComparison.OrdinalIgnoreCase) && File.Exists(PathOf(next)))
                throw new ArgumentException("A preset named \"" + newName + "\" already exists.");
            WriteAtomic(PathOf(next), PresetText(newName, preset.Description, preset.Values, preset.HasPreferences));
            if (!string.Equals(next, id, StringComparison.Ordinal)) File.Delete(PathOf(id));
            return next;
        }

        internal void DeletePreset(string id)
        {
            if (BuiltInPresets.IsBuiltInId(id)) throw new ArgumentException("Built-in presets cannot be deleted.");
            var path = PathOf(id);
            if (File.Exists(path)) File.Delete(path);
        }

        // Imports preset text (e.g. pasted from the clipboard) under a free id.
        internal string ImportPreset(string text, List<string> warnings)
        {
            var parsed = ParsePreset("import", text, warnings);
            var name = CleanName(parsed.Name);
            var id = Slug(name);
            if (BuiltInPresets.IsBuiltInId(id)) { name = "My " + name; id = Slug(name); }
            for (var n = 2; File.Exists(PathOf(id)); ++n) id = Slug(name) + "-" + n;
            Directory.CreateDirectory(PresetDirectory);
            WriteAtomic(PathOf(id), PresetText(name, parsed.Description, parsed.Values, parsed.HasPreferences));
            return id;
        }

        internal string PathOf(string id)
        {
            if (Slug(id) != id) throw new ArgumentException("Invalid preset id \"" + id + "\".");
            return Path.Combine(PresetDirectory, id + ".json");
        }
        internal static string CleanName(string name)
        {
            name = new string((name ?? "").Where(c => !char.IsControl(c)).ToArray()).Trim();
            if (name.Length == 0) throw new ArgumentException("Give the preset a name.");
            return name.Length > MaximumNameLength ? name.Substring(0, MaximumNameLength) : name;
        }
        // File-system-safe on Windows and Linux: lower-case ASCII letters, digits and '-'.
        internal static string Slug(string name)
        {
            var output = new StringBuilder();
            foreach (var c in (name ?? "").ToLowerInvariant())
            {
                if (c >= 'a' && c <= 'z' || c >= '0' && c <= '9') output.Append(c);
                else if (output.Length != 0 && output[output.Length - 1] != '-') output.Append('-');
                if (output.Length >= 40) break;
            }
            var slug = output.ToString().Trim('-');
            return slug.Length == 0 ? "preset" : slug;
        }

        private static (SettingValues values, string? preset, string? name, string? description, bool hasPreferences) ReadDocument(
            string text, string format, SettingValues basis, List<string> warnings)
        {
            if (!(ModSettingsJson.Parse(text) is Dictionary<string, object?> root)) throw new FormatException("The file is not a settings object.");
            if (!root.TryGetValue("Format", out var f) || !(f is string formatName) || formatName != format)
                throw new FormatException("The file is not a Mixed Atmospheres " + (format == PresetFormat ? "preset" : "settings file") + ".");
            if (!root.TryGetValue("Version", out var v) || !(v is double version) || version != FileVersion)
                throw new NotSupportedException("The file's version is not supported by this build.");
            string? Text(string key)
            {
                if (!root.TryGetValue(key, out var value) || value == null) return null;
                return value as string ?? throw new FormatException("\"" + key + "\" must be text.");
            }
            foreach (var key in root.Keys)
                if (key != "Format" && key != "Version" && key != "Preset" && key != "Name" && key != "Description" && key != "Colony" && key != "Preferences")
                    warnings.Add("Ignored unknown entry \"" + key + "\".");
            root.TryGetValue("Colony", out var colony); root.TryGetValue("Preferences", out var preferences);
            var values = SettingValues.Read(basis, SettingScope.Colony, colony, warnings);
            values = SettingValues.Read(values, SettingScope.Preference, preferences, warnings);
            return (values, Text("Preset"), Text("Name"), Text("Description"), preferences != null);
        }

        private SettingValues MigrateAppearance(SettingValues values, List<string> notices)
        {
            var path = Path.Combine(Root, "appearance.json");
            if (!File.Exists(path)) return values;
            try
            {
                if (ModSettingsJson.Parse(File.ReadAllText(path, Encoding.UTF8)) is Dictionary<string, object?> old &&
                    old.TryGetValue("PatternContrast", out var contrast) && contrast is double c)
                {
                    var enabled = !old.TryGetValue("PatternEnabled", out var e) || !(e is bool b) || b;
                    return values.With("display.patternContrast", enabled ? c : 0.0);
                }
            }
            catch (Exception error) when (error is IOException || error is UnauthorizedAccessException || error is FormatException)
            { notices.Add("The older appearance.json could not be read; the pattern contrast is the default (" + error.Message + ")."); }
            return values;
        }

        private static KeyValuePair<string, object?> Member(string key, object? value) => new KeyValuePair<string, object?>(key, value);

        internal static void WriteAtomic(string path, string text)
        {
            var temporary = path + ".tmp";
            File.WriteAllText(temporary, text, new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temporary, path, null);
            else File.Move(temporary, path);
        }
    }
}
