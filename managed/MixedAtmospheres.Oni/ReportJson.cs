using System.Globalization;
using System.IO;
using Newtonsoft.Json;

namespace MixedAtmospheres.Oni
{
    internal static class ReportJson
    {
        internal static string Serialize(object value) => Serialize(value, Formatting.Indented);
        // Reports have one writer and concurrent external readers. Replace a
        // complete sibling file so a poll never observes a truncated document.
        internal static void WriteAtomic(string path, object value)
            => WriteAtomicText(path, Serialize(value));

        internal static void WriteAtomicText(string path, string json)
        {
            var temporary = path + ".tmp";
            try
            {
                File.WriteAllText(temporary, json);
                if (File.Exists(path)) File.Replace(temporary, path, null);
                else File.Move(temporary, path);
            }
            finally
            {
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }
        // One JSON value without line breaks, for append-only line journals.
        internal static string SerializeLine(object value) => Serialize(value, Formatting.None);

        private static string Serialize(object value, Formatting formatting)
        {
            // Use the game's existing assembly, with per-call settings instead
            // of JsonConvert's process-wide defaults shared with other mods.
            var serializer = new JsonSerializer
            {
                TypeNameHandling = TypeNameHandling.None,
                Formatting = formatting,
                Culture = CultureInfo.InvariantCulture
            };
            using var output = new StringWriter(CultureInfo.InvariantCulture);
            using (var writer = new JsonTextWriter(output)) serializer.Serialize(writer, value);
            return output.ToString();
        }
    }
}
