using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    // Proposed validation accounting only. No game inventory or input is edited.
    internal static class ColonyBreathingCo2Accounting
    {
        internal readonly struct Source : IEquatable<Source>
        {
            internal readonly int Id;
            internal readonly long Lifetime;
            internal Source(int id, long lifetime) { Id=id; Lifetime=lifetime; }
            public bool Equals(Source other) => Id==other.Id && Lifetime==other.Lifetime;
            public override bool Equals(object? other) => other is Source value && Equals(value);
            public override int GetHashCode() => HashCode.Combine(Id,Lifetime);
        }
        internal readonly struct Sample
        {
            internal readonly Source Source;
            internal readonly double HeldKg;
            internal Sample(Source source,double heldKg) { Source=source; HeldKg=heldKg; }
        }
        internal readonly struct Row
        {
            internal readonly Source Source;
            internal readonly double InitialHeldKg,FinalHeldKg,OriginalPuffKg,ProducedKg;
            internal Row(Source source,double initial,double final,double puff,double produced)
            { Source=source;InitialHeldKg=initial;FinalHeldKg=final;OriginalPuffKg=puff;ProducedKg=produced; }
        }
        internal sealed class Result
        {
            internal bool Valid { get; }
            internal bool EverySourcePositive { get; }
            internal string Error { get; }
            internal IReadOnlyList<Row> Rows { get; }
            internal Result(string error,Row[] rows)
            {
                Error=error;Valid=error.Length==0;Rows=Array.AsReadOnly(rows);
                var positive=Valid && rows.Length>0;
                foreach(var row in rows)positive &= row.ProducedKg>0;
                EverySourcePositive=positive;
            }
        }
        private static bool Finite(double value) => !double.IsNaN(value) && !double.IsInfinity(value);
        private static bool Held(double value) => Finite(value) && value>=0;
        internal static double Produced(double initialHeldKg,double finalHeldKg,double originalPuffKg)
        {
            if(!Held(initialHeldKg)||!Held(finalHeldKg)||!Held(originalPuffKg))
                throw new ArgumentException("CO2 custody inputs must be finite and nonnegative.");
            // Never count a puff of CO2 already held at the source as new production.
            var produced=(finalHeldKg-initialHeldKg)+originalPuffKg;
            if(!Finite(produced))throw new OverflowException("CO2 production exceeds reporting range.");
            return produced;
        }
        internal sealed class Window
        {
            private sealed class Entry
            {
                internal readonly Source Source;
                internal readonly double Initial;
                internal double Puff;
                internal Entry(Sample sample) { Source=sample.Source;Initial=sample.HeldKg; }
            }
            private readonly ulong session;
            private readonly Dictionary<int,Entry> sources=new Dictionary<int,Entry>();
            private readonly List<Entry> order=new List<Entry>();
            private readonly HashSet<long> puffEvents=new HashSet<long>();
            private bool active=true;
            private string error="";
            internal bool Active => active;
            internal Window(ulong session,IEnumerable<Sample> initial)
            {
                if(session==0)throw new ArgumentException("A source session is required.");
                this.session=session;
                foreach(var sample in initial)
                {
                    if(sample.Source.Id<0||sample.Source.Lifetime==0||!Held(sample.HeldKg))throw new ArgumentException("Invalid source identity or held CO2.");
                    if(sources.ContainsKey(sample.Source.Id))throw new ArgumentException("Duplicate source duplicant.");
                    var entry=new Entry(sample);sources.Add(sample.Source.Id,entry);order.Add(entry);
                }
                if(order.Count==0)throw new ArgumentException("At least one original source duplicant is required.");
            }
            internal void Invalidate(string reason) { if(active && error.Length==0)error=reason.Length==0?"Missing accounting evidence.":reason; }
            private bool Refuse(string reason) { Invalidate(reason);return false; }
            internal bool OriginalPuff(long eventId,ulong observedSession,Source? source,bool exactBreatherScope,bool originalRan,double massKg)
            {
                if(!active)throw new InvalidOperationException("The breathing window is closed.");
                if(observedSession!=session)return Refuse("Puff session differs from the source session.");
                if(!source.HasValue||!exactBreatherScope||!originalRan)return Refuse("Original puff attribution is incomplete.");
                if(!sources.TryGetValue(source.Value.Id,out var entry))return Refuse("Puff source is not an original duplicant.");
                if(!entry.Source.Equals(source.Value))return Refuse("Puff source lifetime changed.");
                if(eventId<=0||!puffEvents.Add(eventId))return Refuse("Puff event is missing or duplicated.");
                if(!Finite(massKg)||massKg<=0)return Refuse("Original puff mass is invalid.");
                var next=entry.Puff+massKg;
                if(!Finite(next))return Refuse("Original puff total exceeds reporting range.");
                entry.Puff=next;return true;
            }
            internal Result Complete(ulong observedSession,IEnumerable<Sample> final)
            {
                if(!active)throw new InvalidOperationException("The breathing window is already closed.");
                if(observedSession!=session)Invalidate("Final session differs from the source session.");
                var held=new Dictionary<int,Sample>();
                foreach(var sample in final)
                {
                    if(held.ContainsKey(sample.Source.Id)){Invalidate("Duplicate final source duplicant.");continue;}
                    held.Add(sample.Source.Id,sample);
                    if(!Held(sample.HeldKg))Invalidate("Final held CO2 is invalid.");
                    if(!sources.TryGetValue(sample.Source.Id,out var entry))Invalidate("Final source is not an original duplicant.");
                    else if(!entry.Source.Equals(sample.Source))Invalidate("Final source lifetime changed.");
                }
                if(held.Count!=sources.Count)Invalidate("The original source cohort changed.");
                foreach(var entry in order)if(!held.ContainsKey(entry.Source.Id))Invalidate("An original source duplicant is missing.");
                var rows=new List<Row>();
                if(error.Length==0)foreach(var entry in order)
                {
                    try
                    {
                        var end=held[entry.Source.Id].HeldKg;
                        rows.Add(new Row(entry.Source,entry.Initial,end,entry.Puff,Produced(entry.Initial,end,entry.Puff)));
                    }
                    catch(Exception caught) when(caught is ArgumentException || caught is OverflowException) { Invalidate(caught.Message); }
                }
                active=false;
                return new Result(error,rows.ToArray());
            }
        }
    }
}
