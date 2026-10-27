using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Private observation only. No scheduler field, actor, clock or grid value
    // is changed; this is deliberately separate from persisted physical state.
    internal static class ColonyContinuationContext
    {
        internal static readonly bool Enabled=Environment.GetEnvironmentVariable("MA_COLONY_CONTINUATION_CONTEXT")=="1";
        private static object? Value(object owner,string name)=>AccessTools.Field(owner.GetType(),name)?.GetValue(owner)
            ??throw new MissingFieldException(owner.GetType().FullName,name);
        private static object Instance(Type type)=>AccessTools.Field(type,"_instance")?.GetValue(null)
            ??throw new InvalidOperationException("No live scheduler: "+type.FullName);
        private static int Bits(object value)=>BitConverter.SingleToInt32Bits((float)value);
        private static object Actor(object data,float last)
        {
            GameObject? go=null;var ordinal=-1;
            var unavailable=false;
            try
            {
                if(data is Component component)
                {
                    if(component==null)unavailable=true;
                    else{go=component.gameObject;ordinal=Array.FindIndex(go.GetComponents(component.GetType()),peer=>ReferenceEquals(peer,component));}
                }
                else if(data is StateMachine.Instance smi)go=smi.gameObject;
            }
            catch(Exception error)when(error is MissingReferenceException||error is NullReferenceException){unavailable=true;}
            return new {Type=data.GetType().FullName,Prefab=go==null?-1:go.GetComponent<KPrefabID>()?.InstanceID??-1,Ordinal=ordinal,Unavailable=unavailable,LastUpdateBits=BitConverter.SingleToInt32Bits(last)};
        }
        private static object Bucket(object bucket)
        {
            var entryField=AccessTools.Field(bucket.GetType(),"entries");var actors=new List<object>();
            if(entryField?.GetValue(bucket) is object entries)
                foreach(var entry in (IEnumerable)Value(entries,"data")!)
                    actors.Add(Actor(Value(entry,"data")!, (float)Value(entry,"lastUpdateTime")!));
            return new {Type=bucket.GetType().FullName,Name=Value(bucket,"<name>k__BackingField"),Frame=Value(bucket,"frame"),Actors=actors};
        }
        private static object Group(object group)
        {
            var frames=new List<object>();
            foreach(var frame in (IEnumerable)Value(group,"bucketFrames")!)frames.Add(((IEnumerable)frame).Cast<object>().Select(Bucket).ToArray());
            return new {Name=Value(group,"name"),AccumulatedBits=Bits(Value(group,"accumulatedTime")!),
                NextUpdate=Value(group,"nextUpdateIndex"),NextFrame=Value(group,"nextBucketFrame"),
                SecondsPerSubTickBits=Bits(Value(group,"<secondsPerSubTick>k__BackingField")!),UseSubticks=Value(group,"useSubticks"),Frames=frames};
        }
        internal static void Write(string stem)
        {
            if(!Enabled)return;
            var updater=Instance(typeof(StateMachineUpdater));var scheduler=Instance(typeof(SimAndRenderScheduler));
            var table=(IDictionary)Value(scheduler,"bucketTable")!;
            var registrations=new List<object>();
            foreach(DictionaryEntry entry in table)
                registrations.Add(new {Name=entry.Key,NextBucket=Value(entry.Value!,"nextBucketIdx")});
            var groups=((IEnumerable)Value(updater,"simBucketGroups")!).Cast<object>().Select(Group).ToArray();
            var json=ReportJson.Serialize(new {Schema=1,GameTimeBits=BitConverter.DoubleToInt64Bits(GameClock.Instance.GetTime()),Groups=groups,Registrations=registrations});
            using(var file=new FileStream(stem+".context.json",FileMode.CreateNew))using(var writer=new StreamWriter(file))writer.Write(json);
            // Private grid projection, cell-index order, exact binary32 bits.
            // This is evidence only, never an input to restoration.
            using(var file=new FileStream(stem+".grid",FileMode.CreateNew))using(var writer=new BinaryWriter(file))
            {
                writer.Write(Grid.WidthInCells);writer.Write(Grid.HeightInCells);
                for(var cell=0;cell<Grid.CellCount;++cell)
                {writer.Write(Grid.Element[cell]==null?int.MinValue:(int)Grid.Element[cell].id);writer.Write(Grid.Mass[cell]);writer.Write(Grid.Temperature[cell]);}
            }
        }
    }
}
