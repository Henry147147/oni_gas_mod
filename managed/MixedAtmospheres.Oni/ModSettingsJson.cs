using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // Strict, bounded JSON for the settings and preset files (docs/settings.md).
    // Engine-free so the standalone tests and both game platforms share one
    // reader; values are objects (ordinal-keyed, duplicates refused), strings,
    // finite numbers, booleans and null. Arrays are refused: no file needs one.
    internal static class ModSettingsJson
    {
        internal const int MaximumCharacters = 256 * 1024, MaximumDepth = 8;

        internal static object? Parse(string text)
        {
            if (text == null) throw new ArgumentNullException(nameof(text));
            if (text.Length > MaximumCharacters) throw new FormatException("The settings file is too large.");
            var reader = new Reader(text);
            reader.Space();
            if (reader.AtEnd) throw new FormatException("The settings file is empty.");
            var value = reader.Value(0);
            reader.Space();
            if (!reader.AtEnd) throw new FormatException("Unexpected text after the settings object at " + reader.Where + ".");
            return value;
        }

        private sealed class Reader
        {
            private readonly string text; private int at;
            internal Reader(string text) { this.text = text; if (text.Length > 0 && text[0] == '﻿') at = 1; }
            internal bool AtEnd => at >= text.Length;
            internal string Where => "character " + at.ToString(CultureInfo.InvariantCulture);
            internal void Space() { while (at < text.Length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) ++at; }
            private char Next() { if (at >= text.Length) throw new FormatException("The settings file ends early."); return text[at++]; }
            private void Expect(string word)
            {
                if (string.CompareOrdinal(text, at, word, 0, word.Length) != 0) throw new FormatException("Invalid value at " + Where + ".");
                at += word.Length;
            }
            internal object? Value(int depth)
            {
                if (depth > MaximumDepth) throw new FormatException("The settings file nests too deeply.");
                Space();
                if (AtEnd) throw new FormatException("The settings file ends early.");
                switch (text[at])
                {
                    case '{': return Object(depth);
                    case '"': return String();
                    case 't': Expect("true"); return true;
                    case 'f': Expect("false"); return false;
                    case 'n': Expect("null"); return null;
                    case '[': throw new FormatException("Lists are not used in settings files (" + Where + ").");
                    default: return Number();
                }
            }
            private Dictionary<string, object?> Object(int depth)
            {
                ++at; var result = new Dictionary<string, object?>(StringComparer.Ordinal);
                Space();
                if (!AtEnd && text[at] == '}') { ++at; return result; }
                while (true)
                {
                    Space();
                    if (AtEnd || text[at] != '"') throw new FormatException("Expected a quoted name at " + Where + ".");
                    var name = String();
                    Space();
                    if (Next() != ':') throw new FormatException("Expected ':' after \"" + name + "\".");
                    var value = Value(depth + 1);
                    if (result.ContainsKey(name)) throw new FormatException("Duplicate setting \"" + name + "\".");
                    result.Add(name, value);
                    Space();
                    var c = Next();
                    if (c == '}') return result;
                    if (c != ',') throw new FormatException("Expected ',' or '}' at " + Where + ".");
                }
            }
            private string String()
            {
                ++at; var result = new StringBuilder();
                while (true)
                {
                    var c = Next();
                    if (c == '"') return result.ToString();
                    if (c < 0x20) throw new FormatException("Control character in a string at " + Where + ".");
                    if (c != '\\') { result.Append(c); continue; }
                    c = Next();
                    switch (c)
                    {
                        case '"': case '\\': case '/': result.Append(c); break;
                        case 'b': result.Append('\b'); break;
                        case 'f': result.Append('\f'); break;
                        case 'n': result.Append('\n'); break;
                        case 'r': result.Append('\r'); break;
                        case 't': result.Append('\t'); break;
                        case 'u':
                            if (at + 4 > text.Length || !int.TryParse(text.Substring(at, 4), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var code))
                                throw new FormatException("Invalid escape at " + Where + ".");
                            result.Append((char)code); at += 4; break;
                        default: throw new FormatException("Invalid escape at " + Where + ".");
                    }
                }
            }
            private double Number()
            {
                var start = at;
                if (at < text.Length && text[at] == '-') ++at;
                var digits = at;
                while (at < text.Length && char.IsDigit(text[at])) ++at;
                if (at == digits || text[digits] == '0' && at - digits > 1) throw new FormatException("Invalid number at character " + start.ToString(CultureInfo.InvariantCulture) + ".");
                if (at < text.Length && text[at] == '.') { ++at; var f = at; while (at < text.Length && char.IsDigit(text[at])) ++at; if (at == f) throw new FormatException("Invalid number."); }
                if (at < text.Length && (text[at] == 'e' || text[at] == 'E'))
                {
                    ++at; if (at < text.Length && (text[at] == '+' || text[at] == '-')) ++at;
                    var e = at; while (at < text.Length && char.IsDigit(text[at])) ++at; if (at == e) throw new FormatException("Invalid number.");
                }
                var value = double.Parse(text.Substring(start, at - start), NumberStyles.Float, CultureInfo.InvariantCulture);
                if (double.IsNaN(value) || double.IsInfinity(value)) throw new FormatException("Numbers must be finite.");
                return value;
            }
        }

        // Writes an object of objects, strings, numbers and booleans with
        // two-space indentation, round-trip numbers and '\n' line ends.
        internal static string Write(IEnumerable<KeyValuePair<string, object?>> root)
        {
            var output = new StringBuilder();
            WriteObject(output, root, 0);
            output.Append('\n');
            return output.ToString();
        }
        private static void WriteObject(StringBuilder output, IEnumerable<KeyValuePair<string, object?>> members, int indent)
        {
            output.Append('{');
            var first = true;
            foreach (var member in members)
            {
                output.Append(first ? "\n" : ",\n"); first = false;
                output.Append(' ', 2 * (indent + 1)); WriteString(output, member.Key); output.Append(": ");
                WriteValue(output, member.Value, indent + 1);
            }
            if (!first) { output.Append('\n'); output.Append(' ', 2 * indent); }
            output.Append('}');
        }
        private static void WriteValue(StringBuilder output, object? value, int indent)
        {
            switch (value)
            {
                case null: output.Append("null"); break;
                case bool b: output.Append(b ? "true" : "false"); break;
                case string s: WriteString(output, s); break;
                case double d: output.Append(Number(d)); break;
                case int i: output.Append(i.ToString(CultureInfo.InvariantCulture)); break;
                case IEnumerable<KeyValuePair<string, object?>> o: WriteObject(output, o, indent); break;
                default: throw new ArgumentException("Unsupported settings value " + value.GetType().Name + ".");
            }
        }
        internal static string Number(double value)
        {
            if (double.IsNaN(value) || double.IsInfinity(value)) throw new ArgumentException("Settings numbers must be finite.");
            return value.ToString("R", CultureInfo.InvariantCulture);
        }
        private static void WriteString(StringBuilder output, string value)
        {
            output.Append('"');
            foreach (var c in value)
            {
                switch (c)
                {
                    case '"': output.Append("\\\""); break;
                    case '\\': output.Append("\\\\"); break;
                    case '\n': output.Append("\\n"); break;
                    case '\r': output.Append("\\r"); break;
                    case '\t': output.Append("\\t"); break;
                    default:
                        if (c < 0x20) output.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        else output.Append(c);
                        break;
                }
            }
            output.Append('"');
        }
    }
}
