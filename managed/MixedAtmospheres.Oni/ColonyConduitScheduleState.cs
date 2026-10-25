using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // Schema5's native compatibility/scheduling tail. List order is physical
    // state for networks and callbacks; only the cell lookup is canonicalized.
    internal sealed class PipeNativeScheduleState
    {
        internal sealed class Contents
        {
            internal int Element, Germs;
            internal byte Disease;
            internal float Initial, Added, Removed, Temperature;
            internal void Validate()
            {
                foreach (var value in new[] { Initial, Added, Removed, Temperature })
                    if (float.IsNaN(value) || float.IsInfinity(value) || value < 0)
                        throw new InvalidDataException("Invalid saved native conduit contents.");
                if (Germs < 0 || Disease == byte.MaxValue && Germs != 0)
                    throw new InvalidDataException("Invalid saved native conduit germ projection.");
            }
            internal void Write(BinaryWriter w)
            { Validate(); w.Write(Element); w.Write(Initial); w.Write(Added); w.Write(Removed); w.Write(Temperature); w.Write(Disease); w.Write(Germs); }
            internal static Contents Read(BinaryReader r)
            {
                var value = new Contents { Element = r.ReadInt32(), Initial = r.ReadSingle(), Added = r.ReadSingle(), Removed = r.ReadSingle(),
                    Temperature = r.ReadSingle(), Disease = r.ReadByte(), Germs = r.ReadInt32() };
                value.Validate(); return value;
            }
        }
        internal sealed class Cell
        {
            internal int Index, Permitted, Source, Pull, Target;
            internal Contents Current = new Contents(), Initial = new Contents();
        }
        internal sealed class Updater
        {
            // 0 static, 1 Component, 2 StateMachine.Instance; no delegates or
            // live handles are serialized. Ordinal distinguishes components.
            internal byte Kind;
            internal int Priority, Prefab, Ordinal;
            internal string TargetType = "", DeclaringType = "", Method = "";
            internal (byte, int, int, int, string, string, string) Key =>
                (Kind, Priority, Prefab, Ordinal, TargetType, DeclaringType, Method);
            internal void Validate()
            {
                if (TargetType == null || DeclaringType == null || Method == null)
                    throw new InvalidDataException("Missing saved conduit callback identity.");
                if (Kind > 2 || Ordinal < 0 || Kind == 0 && (Prefab != 0 || Ordinal != 0 || TargetType.Length != 0) ||
                    Kind != 0 && (Prefab <= 0 || TargetType.Length == 0) || DeclaringType.Length == 0 || Method.Length == 0)
                    throw new InvalidDataException("Invalid saved conduit callback identity.");
                foreach (var text in new[] { TargetType, DeclaringType, Method })
                    if (Utf8.GetByteCount(text) > MaximumTextBytes)
                        throw new InvalidDataException("Native conduit callback identity is too long.");
            }
        }
        internal sealed class Flow
        {
            internal byte Phase;
            internal float Elapsed;
            internal bool UpdatersDirty;
            internal List<Cell> Cells = new List<Cell>();
            internal List<int[]> Networks = new List<int[]>();
            internal List<Updater> Updaters = new List<Updater>();
            internal void Validate(PipeGeometry geometry)
            {
                if (Phase != 1 && Phase != 2 || float.IsNaN(Elapsed) || float.IsInfinity(Elapsed) || Elapsed < 0)
                    throw new InvalidDataException("Invalid saved native conduit clock.");
                if (Cells.Count > MaximumRecords || Networks.Count > MaximumRecords || Updaters.Count > MaximumRecords)
                    throw new InvalidDataException("Native conduit schedule has too many records.");
                var seen = new HashSet<int>();
                foreach (var cell in Cells)
                {
                    if (!geometry.Contains(cell.Index) || !seen.Add(cell.Index) ||
                        (cell.Permitted | cell.Source | cell.Pull | cell.Target) < 0 ||
                        ((cell.Permitted | cell.Source | cell.Pull | cell.Target) & ~15) != 0)
                        throw new InvalidDataException("Invalid or duplicate native conduit schedule cell.");
                    cell.Current.Validate(); cell.Initial.Validate();
                }
                var scheduled = new HashSet<int>();
                foreach (var network in Networks)
                {
                    if (network.Length > MaximumRecords) throw new InvalidDataException("Native conduit network is too large.");
                    var membership = new HashSet<int>(network);
                    if (network.Any(cell => !seen.Contains(cell)) || membership.Any(cell => !scheduled.Add(cell)))
                        throw new InvalidDataException("Saved native conduit networks overlap or leave their cells.");
                }
                foreach (var updater in Updaters) updater.Validate();
            }
        }

        internal readonly List<Flow> Flows = new List<Flow>();
        private const int MaximumRecords = 1 << 20, MaximumTextBytes = 4096;
        private static readonly UTF8Encoding Utf8 = new UTF8Encoding(false, true);

        internal void Validate(PipeGeometry geometry)
        {
            if (Flows.Count != 2 || Flows[0].Phase != 1 || Flows[1].Phase != 2)
                throw new InvalidDataException("A native schedule requires both gas and liquid clocks.");
            foreach (var flow in Flows) flow.Validate(geometry);
        }
        internal void Write(BinaryWriter w, PipeGeometry geometry)
        {
            Validate(geometry);
            foreach (var flow in Flows)
            {
                w.Write(flow.Phase); w.Write(flow.Elapsed); w.Write(flow.UpdatersDirty);
                w.Write(flow.Cells.Count);
                foreach (var cell in flow.Cells.OrderBy(c => c.Index))
                {
                    w.Write(cell.Index); cell.Current.Write(w); cell.Initial.Write(w);
                    w.Write(cell.Permitted); w.Write(cell.Source); w.Write(cell.Pull); w.Write(cell.Target);
                }
                w.Write(flow.Networks.Count);
                foreach (var network in flow.Networks)
                { w.Write(network.Length); foreach (var cell in network) w.Write(cell); }
                w.Write(flow.Updaters.Count);
                foreach (var updater in flow.Updaters)
                {
                    w.Write(updater.Kind); w.Write(updater.Priority); w.Write(updater.Prefab); w.Write(updater.Ordinal);
                    Text(w, updater.TargetType); Text(w, updater.DeclaringType); Text(w, updater.Method);
                }
            }
        }
        internal static PipeNativeScheduleState Read(BinaryReader r, PipeGeometry geometry)
        {
            var result = new PipeNativeScheduleState();
            for (var phase = 1; phase <= 2; ++phase)
            {
                var flow = new Flow { Phase = r.ReadByte(), Elapsed = r.ReadSingle() };
                var dirty = r.ReadByte(); if (dirty > 1) throw new InvalidDataException("Invalid saved conduit update flag.");
                flow.UpdatersDirty = dirty != 0;
                if (flow.Phase != phase) throw new InvalidDataException("Noncanonical native conduit phase order.");
                var count = Count(r, 70); var previous = -1;
                for (var i = 0; i < count; ++i)
                {
                    var cell = new Cell { Index = r.ReadInt32(), Current = Contents.Read(r), Initial = Contents.Read(r),
                        Permitted = r.ReadInt32(), Source = r.ReadInt32(), Pull = r.ReadInt32(), Target = r.ReadInt32() };
                    if (cell.Index <= previous) throw new InvalidDataException("Noncanonical native conduit cell order.");
                    previous = cell.Index; flow.Cells.Add(cell);
                }
                count = Count(r, 4);
                for (var i = 0; i < count; ++i)
                {
                    var network = new int[Count(r, 4)]; for (var j = 0; j < network.Length; ++j) network[j] = r.ReadInt32();
                    flow.Networks.Add(network);
                }
                count = Count(r, 25);
                for (var i = 0; i < count; ++i)
                    flow.Updaters.Add(new Updater { Kind = r.ReadByte(), Priority = r.ReadInt32(), Prefab = r.ReadInt32(), Ordinal = r.ReadInt32(),
                        TargetType = Text(r), DeclaringType = Text(r), Method = Text(r) });
                result.Flows.Add(flow);
            }
            result.Validate(geometry); return result;
        }
        private static int Count(BinaryReader r, int minimumBytes)
        {
            var count = r.ReadInt32();
            if (count < 0 || count > MaximumRecords || count > (r.BaseStream.Length - r.BaseStream.Position) / minimumBytes)
                throw new InvalidDataException("Invalid saved native conduit schedule count.");
            return count;
        }
        private static void Text(BinaryWriter w, string value)
        {
            var bytes = Utf8.GetBytes(value);
            if (bytes.Length > MaximumTextBytes) throw new InvalidDataException("Native conduit callback identity is too long.");
            w.Write(bytes.Length); w.Write(bytes);
        }
        private static string Text(BinaryReader r)
        {
            var count = r.ReadInt32();
            if (count < 0 || count > MaximumTextBytes || count > r.BaseStream.Length - r.BaseStream.Position)
                throw new InvalidDataException("Invalid native conduit callback identity length.");
            try { return Utf8.GetString(r.ReadBytes(count)); }
            catch (DecoderFallbackException error) { throw new InvalidDataException("Invalid native conduit callback text.", error); }
        }
    }
}
