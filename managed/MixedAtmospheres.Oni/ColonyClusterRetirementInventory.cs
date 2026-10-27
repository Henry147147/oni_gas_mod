using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // Transient exact owner -> managed ownership handoff. The native prepare
    // leaves physical state unchanged until these inventories and accounts
    // have been decoded, validated and allocated successfully.
    internal sealed class ColonyRetirementInventory
    {
        internal sealed class Phase
        {
            internal PipeInventory Inventory=PipeInventory.Empty;
            internal PipeAmount Remainder;
            internal readonly PipeScaledAccount Kinetic=new PipeScaledAccount(),Potential=new PipeScaledAccount(),
                MomentumX=new PipeScaledAccount(),MomentumY=new PipeScaledAccount();
        }
        internal readonly Phase[] Phases={new Phase(),new Phase()};
        internal static ColonyRetirementInventory Read(byte[] bytes,ulong session,ulong frame,uint origin,uint width,uint height,
            Func<int,byte> materialPhase,Func<int,PipeMaterial> material)
        {
            if(bytes.Length<100||bytes.Length>16*1024*1024)throw new InvalidDataException("Invalid retirement inventory size.");
            using var stream=new MemoryStream(bytes,false);using var reader=new BinaryReader(stream);
            try
            {
                if(Encoding.ASCII.GetString(reader.ReadBytes(8))!="MARET001"||reader.ReadUInt64()!=session||reader.ReadUInt64()!=frame||
                    reader.ReadUInt32()!=origin||reader.ReadUInt32()!=width||reader.ReadUInt32()!=height)
                    throw new InvalidDataException("Rocket retirement source changed before ownership handoff.");
                var result=new ColonyRetirementInventory();
                for(var i=0;i<2;++i)
                {
                    var phase=result.Phases[i];phase.Inventory=PipeInventory.Read(reader);
                    foreach(var part in phase.Inventory.Parts)
                        if(materialPhase(part.Id)!=i+1)throw new InvalidDataException("Rocket retirement changed a constituent's phase.");
                    if(phase.Inventory.Count>0)phase.Inventory.Temperature(material);
                    ReadMechanics(phase.Kinetic);ReadMechanics(phase.Potential);ReadMechanics(phase.MomentumX);ReadMechanics(phase.MomentumY);
                    phase.Remainder=PipeAmount.Read(reader);
                }
                if(stream.Position!=stream.Length)throw new InvalidDataException("Trailing rocket retirement inventory data.");
                return result;
            }
            catch(EndOfStreamException error){throw new InvalidDataException("Truncated rocket retirement inventory.",error);}
            void ReadMechanics(PipeScaledAccount account)
            {
                var count=reader.ReadUInt32();
                if(count>65536||count>(stream.Length-stream.Position)/13)throw new InvalidDataException("Invalid retirement mechanical expansion.");
                for(var i=0U;i<count;++i)
                {
                    var value=PipeScaledValue.Read(reader,bytes.Length);
                    if(Math.Abs(value.Mantissa)<.5||Math.Abs(value.Mantissa)>=1)throw new InvalidDataException("Noncanonical retirement mechanical component.");
                    account.Add(value);
                }
            }
        }
    }
}
