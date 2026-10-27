using System;
using System.IO;
using System.Linq;
using System.Text;

namespace MixedAtmospheres.Oni
{
    internal sealed class ColonyNativeClockState
    {
        private static readonly byte[] Magic=Encoding.ASCII.GetBytes("MACNRC01");
        internal readonly int Width,Height,Cells,Iterate,Displacement;
        internal readonly uint Random;
        internal readonly ushort Tick;
        internal ColonyNativeClockState(int width,int height,int cells,uint random,int iterate,int displacement,ushort tick)
        {
            if(width<3||height<3||cells<=0||(long)(width-2)*(height-2)!=cells||
                (iterate!=-1&&iterate!=1)||(displacement!=-1&&displacement!=1))throw new InvalidDataException("Invalid native loop clock state.");
            Width=width;Height=height;Cells=cells;Random=random;Iterate=iterate;Displacement=displacement;Tick=tick;
        }
        internal bool Same(ColonyNativeClockState other)=>Width==other.Width&&Height==other.Height&&Cells==other.Cells&&Random==other.Random&&
            Iterate==other.Iterate&&Displacement==other.Displacement&&Tick==other.Tick;
        internal byte[] Encode()
        {
            using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
            writer.Write(Magic);writer.Write(1);writer.Write(Width);writer.Write(Height);writer.Write(Cells);
            writer.Write(Random);writer.Write(Iterate);writer.Write(Displacement);writer.Write(Tick);writer.Write((ushort)0);return stream.ToArray();
        }
        internal static ColonyNativeClockState Decode(byte[] bytes)
        {
            if(bytes.Length!=40)throw new InvalidDataException("Invalid native loop clock record length.");
            using var stream=new MemoryStream(bytes);using var reader=new BinaryReader(stream);
            if(!reader.ReadBytes(8).SequenceEqual(Magic)||reader.ReadUInt32()!=1)throw new InvalidDataException("Unsupported native loop clock record.");
            var state=new ColonyNativeClockState(reader.ReadInt32(),reader.ReadInt32(),reader.ReadInt32(),reader.ReadUInt32(),reader.ReadInt32(),reader.ReadInt32(),reader.ReadUInt16());
            if(reader.ReadUInt16()!=0)throw new InvalidDataException("Noncanonical native loop clock record.");return state;
        }
    }
}
