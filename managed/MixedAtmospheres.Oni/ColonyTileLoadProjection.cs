using System;
using System.Buffers.Binary;

namespace MixedAtmospheres.Oni
{
    // A native solid cell already restored by the save. Recreating its tile
    // must retain the cell's thermal/culture state, not the prefab cache.
    internal readonly struct ColonyTileLoadProjection
    {
        internal readonly int Cell,TemperatureBits,MassBits,Germs;
        internal readonly ushort Element;
        internal readonly byte Disease;
        internal ColonyTileLoadProjection(int cell,ushort element,float mass,float temperature,byte disease,int germs)
        {
            if(cell<0||!(mass>0)||float.IsInfinity(mass)||!(temperature>0)||float.IsInfinity(temperature)||
                germs<0||(disease==byte.MaxValue&&germs!=0))throw new ArgumentException("Invalid saved tile projection.");
            Cell=cell;Element=element;MassBits=BitConverter.SingleToInt32Bits(mass);TemperatureBits=BitConverter.SingleToInt32Bits(temperature);Disease=disease;Germs=germs;
        }
        internal bool Rewrite(Span<byte> message)
        {
            // Only the inspected replace-and-displace record from OnSpawn.
            // Leave every byte untouched when it describes another operation.
            if(message.Length!=28||message[22]!=2||BinaryPrimitives.ReadInt32LittleEndian(message)!=Cell||
                BinaryPrimitives.ReadUInt16LittleEndian(message.Slice(20))!=Element||
                BinaryPrimitives.ReadInt32LittleEndian(message.Slice(12))!=MassBits)return false;
            BinaryPrimitives.WriteInt32LittleEndian(message.Slice(8),TemperatureBits);
            BinaryPrimitives.WriteInt32LittleEndian(message.Slice(16),Germs);message[23]=Disease;
            return true;
        }
    }
}
