using System.Globalization;
using System.IO;
using Newtonsoft.Json;

namespace MixedAtmospheres.Oni
{
    internal static class ReportJson
    {
        internal static string Serialize(object value)
        {
            // Use the game's existing assembly, with per-call settings instead
            // of JsonConvert's process-wide defaults shared with other mods.
            var serializer = new JsonSerializer
            {
                TypeNameHandling = TypeNameHandling.None,
                Formatting = Formatting.Indented,
                Culture = CultureInfo.InvariantCulture
            };
            using var output = new StringWriter(CultureInfo.InvariantCulture);
            using (var writer = new JsonTextWriter(output)) serializer.Serialize(writer, value);
            return output.ToString();
        }
    }
}
