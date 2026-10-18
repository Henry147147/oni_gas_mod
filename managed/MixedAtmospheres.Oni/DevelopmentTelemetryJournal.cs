using System;
using System.IO;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // Append-only JSON lines, flushed through to the file after every line.
    // An earlier session in the same output directory is preserved.
    internal sealed class DevelopmentTelemetryJournal : IDisposable
    {
        internal const int DefaultMaximumLines = 20000, MaximumLineBytes = 16384;
        private readonly int maximumLines;
        private FileStream? stream;
        internal int Lines { get; private set; }
        internal bool Truncated { get; private set; }
        internal string Path { get; }
        internal DevelopmentTelemetryJournal(string path, int maximumLines = DefaultMaximumLines)
        {
            if (maximumLines < 1) throw new ArgumentOutOfRangeException(nameof(maximumLines));
            Path = path; this.maximumLines = maximumLines;
            stream = new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.Read);
        }
        // Accepts one already serialized JSON value (ReportJson.SerializeLine).
        internal bool Append(string line)
        {
            if (stream == null) throw new ObjectDisposedException(nameof(DevelopmentTelemetryJournal));
            if (line == null) throw new ArgumentNullException(nameof(line));
            if (line.Length == 0 || line.IndexOf('\n') >= 0 || line.IndexOf('\r') >= 0 || line[0] != '{' || line[line.Length - 1] != '}')
                throw new InvalidDataException("A development telemetry line must be one JSON object without line breaks.");
            var bytes = new UTF8Encoding(false, true).GetBytes(line + "\n");
            if (bytes.Length > MaximumLineBytes) throw new InvalidDataException("A development telemetry line exceeds " + MaximumLineBytes + " bytes.");
            if (Lines >= maximumLines) { Truncated = true; return false; }
            stream.Write(bytes, 0, bytes.Length);
            stream.Flush(true);
            ++Lines;
            return true;
        }
        public void Dispose() { stream?.Dispose(); stream = null; }
    }
}
