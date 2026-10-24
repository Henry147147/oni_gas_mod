using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct WorldPassiveStamp
    {
        internal uint Size,Version;
        internal ulong Session,CompletedFrame;
        internal double TimeSeconds;
        internal static WorldPassiveStamp Empty=>new WorldPassiveStamp{Size=32,Version=1};
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct WorldPassiveRow
    {
        internal uint PublicCell,Reserved;
        internal ulong Identity;
        internal double Quantity;
    }
    internal sealed class WorldPassiveSnapshot
    {
        internal WorldPassiveStamp Source { get; }
        internal IReadOnlyList<uint> Cells { get; }
        internal IReadOnlyList<WorldPassiveRow> Rows { get; }
        internal WorldPassiveSnapshot(WorldPassiveStamp source,uint[] cells,WorldPassiveRow[] rows)
        {Source=source;Cells=Array.AsReadOnly(cells);Rows=Array.AsReadOnly(rows);}
    }
    internal delegate int WorldPassiveRead(ulong session,ulong frame,uint[] cells,uint cellCount,ref WorldPassiveStamp stamp,
        WorldPassiveRow[]? rows,uint capacity,out uint count);
    // Two calls share an explicit native publication label. An advancing frame
    // returns BUSY; no partially collected or mixed-frame snapshot escapes.
    internal static class ColonyWorldPassives
    {
        internal const int Busy=3;
        internal static int Read(WorldPassiveRead reader,ulong session,ulong frame,uint[] cells,out WorldPassiveSnapshot? snapshot)
        {
            snapshot=null;
            if(reader==null || cells==null || cells.Length==0 || cells.Length>4096)throw new ArgumentException("Invalid passive selection.");
            var selected=(uint[])cells.Clone();
            for(var i=1;i<selected.Length;++i)if(selected[i]<=selected[i-1])throw new ArgumentException("Passive selection must be sorted and unique.");
            var first=WorldPassiveStamp.Empty;
            var result=reader(session,frame,selected,(uint)selected.Length,ref first,null,0,out var count);
            if(result!=0)return result;
            ValidateStamp(first,session,frame);
            if(count>1u<<20)throw new InvalidDataException("Too many passive rows.");
            var values=new WorldPassiveRow[count];
            if(count!=0)
            {
                var copied=WorldPassiveStamp.Empty;
                result=reader(first.Session,first.CompletedFrame,selected,(uint)selected.Length,ref copied,values,count,out var actual);
                if(result!=0)return result;
                ValidateStamp(copied,first.Session,first.CompletedFrame);
                if(actual!=count || BitConverter.DoubleToInt64Bits(copied.TimeSeconds)!=BitConverter.DoubleToInt64Bits(first.TimeSeconds))
                    throw new InvalidDataException("Passive snapshot changed within one publication.");
            }
            var cursor=0;uint previousCell=0;ulong previousId=0;
            for(var i=0;i<values.Length;++i)
            {
                var row=values[i];
                while(cursor<selected.Length && selected[cursor]<row.PublicCell)++cursor;
                var bits=unchecked((ulong)BitConverter.DoubleToInt64Bits(row.Quantity));
                if(cursor==selected.Length || selected[cursor]!=row.PublicCell || row.Reserved!=0 || row.Identity==0 ||
                    bits==0 || bits>0x7fefffffffffffffUL || (i!=0 && (row.PublicCell<previousCell || row.PublicCell==previousCell && row.Identity<=previousId)))
                    throw new InvalidDataException("Invalid passive observation row.");
                previousCell=row.PublicCell;previousId=row.Identity;
            }
            snapshot=new WorldPassiveSnapshot(first,selected,values);return 0;
        }
        private static void ValidateStamp(WorldPassiveStamp source,ulong session,ulong frame)
        {
            var time=unchecked((ulong)BitConverter.DoubleToInt64Bits(source.TimeSeconds));
            if(source.Size!=32 || source.Version!=1 || source.Session==0 || time>0x7fefffffffffffffUL ||
                session!=0 && (source.Session!=session || source.CompletedFrame!=frame))
                throw new InvalidDataException("Invalid passive publication label.");
        }
        internal static void ValidateLegacyProjection(WorldPassiveSnapshot snapshot,uint cell,uint disease,int count)
        {
            double maximum=0,selected=0;
            foreach(var row in snapshot.Rows)
                if(row.PublicCell==cell && row.Identity<=256)
                {
                    maximum=Math.Max(maximum,row.Quantity);
                    if(row.Identity==disease)selected=row.Quantity;
                }
            // Native preserves stored-order ties; the observation sorts IDs.
            // Either tied maximum is a valid legacy projection.
            if(maximum<.5 ? disease!=0 || count!=0 : disease==0 || selected!=maximum || count!=(int)Math.Min(maximum,int.MaxValue))
                throw new InvalidDataException("World passive inventory disagrees with its legacy germ projection.");
        }
    }
}
