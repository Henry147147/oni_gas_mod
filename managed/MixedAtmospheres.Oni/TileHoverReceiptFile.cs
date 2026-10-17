using System.IO;
using System.Text;

namespace MixedAtmospheres.Oni
{
    internal static class TileHoverReceiptFile
    {
        internal static string Read(string path)
        {
            // A completed same-directory rename can be visible before Windows
            // closes its DELETE handle. Share deletion, never writing: the
            // closed immutable receipt remains readable through that window.
            using var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read|FileShare.Delete);
            if (stream.Length>4096) throw new InvalidDataException("Hover continuation exceeds its byte limit.");
            using var reader=new StreamReader(stream,new UTF8Encoding(false,true),true);
            return reader.ReadToEnd();
        }
    }
}
