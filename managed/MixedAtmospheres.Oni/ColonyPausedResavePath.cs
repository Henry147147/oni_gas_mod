using System;
using System.Globalization;
using System.IO;

namespace MixedAtmospheres.Oni
{
    internal static class ColonyPausedResavePath
    {
        internal static string Destination(string directory, ulong session)
        {
            if (session == 0) throw new InvalidDataException("Paused resave requires an active owner session.");
            var path = Path.Combine(directory, "owner-frame-zero-resave-" + session.ToString(CultureInfo.InvariantCulture) + ".sav");
            if (File.Exists(path) || Directory.Exists(path))
                throw new InvalidOperationException("Paused resave destination already exists.");
            return path;
        }
    }
}
