using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal sealed class PipePortJob
    {
        internal readonly ulong LocalId,NativeToken;
        internal readonly int PipeCell;
        internal readonly byte Phase;
        internal readonly uint WorldCell,NativeIndex,Kind,Radius;
        internal readonly double RequestedMass,Temperature;
        internal readonly PipeInventory Reserved;
        internal readonly PipeReceiptPassive[] Passives;
        // NativeToken==0: prepared only. Positive: native-queued and persisted
        // there too. Committed jobs must settle before either save is captured.
        internal PipePortJob(ulong localId,ulong nativeToken,int pipeCell,byte phase,uint worldCell,uint nativeIndex,uint kind,uint radius,
            double mass,double temperature,PipeInventory reserved,IEnumerable<PipeReceiptPassive>? passives=null)
        {
            if(localId==0||phase!=1&&phase!=2||kind!=1&&kind!=3&&kind!=4||!(mass>0)||double.IsInfinity(mass)||
                double.IsNaN(temperature)||double.IsInfinity(temperature)||kind==1&&!(temperature>0)||kind!=1&&temperature!=0)
                throw new InvalidDataException("Invalid durable pipe job.");
            if(kind==1&&(reserved.Count!=1||new PipeAmount(mass).Minus(reserved.Total).Sign>0)||kind!=1&&reserved.Count!=0||
                kind==3&&phase!=1||kind==4&&phase!=2)throw new InvalidDataException("Invalid pipe job reservation.");
            LocalId=localId;NativeToken=nativeToken;PipeCell=pipeCell;Phase=phase;WorldCell=worldCell;NativeIndex=nativeIndex;Kind=kind;Radius=radius;
            RequestedMass=mass;Temperature=temperature;Reserved=reserved;Passives=(passives??Array.Empty<PipeReceiptPassive>()).ToArray();
            if(Passives.Length>65537||kind!=1&&Passives.Length!=0)throw new InvalidDataException("Invalid pipe job passive count.");
            ulong previous=0;
            foreach(var passive in Passives)
            {
                if(passive.Identity<=previous||passive.Identity>0x100000000UL||!(passive.Quantity>0)||double.IsInfinity(passive.Quantity))throw new InvalidDataException("Invalid pipe job passive.");
                previous=passive.Identity;
            }
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(LocalId);writer.Write(NativeToken);writer.Write(PipeCell);writer.Write(Phase);
            writer.Write(WorldCell);writer.Write(NativeIndex);writer.Write(Kind);writer.Write(Radius);writer.Write(RequestedMass);writer.Write(Temperature);
            Reserved.Write(writer);writer.Write(Passives.Length);
            foreach(var passive in Passives){writer.Write(passive.Identity);writer.Write(passive.Quantity);}
        }
        internal static PipePortJob Read(BinaryReader reader)
        {
            var id=reader.ReadUInt64();var token=reader.ReadUInt64();var cell=reader.ReadInt32();var phase=reader.ReadByte();
            var world=reader.ReadUInt32();var material=reader.ReadUInt32();var kind=reader.ReadUInt32();var radius=reader.ReadUInt32();
            var mass=reader.ReadDouble();var temperature=reader.ReadDouble();var reserved=PipeInventory.Read(reader);var count=reader.ReadInt32();
            if(count<0||count>65537||(long)count*16>reader.BaseStream.Length-reader.BaseStream.Position)throw new InvalidDataException("Invalid pipe job passives.");
            var passives=new PipeReceiptPassive[count];
            for(var i=0;i<count;++i)passives[i]=new PipeReceiptPassive(reader.ReadUInt64(),reader.ReadDouble());
            return new PipePortJob(id,token,cell,phase,world,material,kind,radius,mass,temperature,reserved,passives);
        }
    }
    internal readonly struct PipePassiveBalance
    {
        internal readonly ulong Identity;internal readonly PipeAmount Quantity;
        internal PipePassiveBalance(ulong identity,PipeAmount quantity){Identity=identity;Quantity=quantity;}
    }
    internal sealed class PipeBoundaryState
    {
        internal readonly PipeAmount UnreleasedHeat;
        internal readonly PipeScaledAccount Kinetic,Potential,MomentumX,MomentumY;
        internal readonly PipePassiveBalance[] PassiveRemainders;
        internal readonly ulong NextLocalId,NextNativeToken;
        internal readonly PipePortJob[] Jobs;
        internal PipeBoundaryState(PipeAmount heat,PipeScaledAccount kinetic,PipeScaledAccount potential,PipeScaledAccount momentumX,
            PipeScaledAccount momentumY,ulong nextLocalId,ulong nextNativeToken,IEnumerable<PipePortJob>? jobs=null,
            IEnumerable<PipePassiveBalance>? passiveRemainders=null)
        {
            if(nextLocalId==0||nextNativeToken==0)throw new InvalidDataException("Invalid pipe job watermark.");
            UnreleasedHeat=heat;Kinetic=kinetic;Potential=potential;MomentumX=momentumX;MomentumY=momentumY;
            NextLocalId=nextLocalId;NextNativeToken=nextNativeToken;Jobs=(jobs??Array.Empty<PipePortJob>()).OrderBy(j=>j.LocalId).ToArray();
            PassiveRemainders=(passiveRemainders??Array.Empty<PipePassiveBalance>()).Where(p=>p.Quantity.Sign!=0).OrderBy(p=>p.Identity).ToArray();
            if(PassiveRemainders.Length>65537)throw new InvalidDataException("Too many pipe passive remainders.");
            ulong previousPassive=0;
            foreach(var passive in PassiveRemainders)
            {
                if(passive.Identity<=previousPassive||passive.Identity>0x100000000UL)throw new InvalidDataException("Invalid pipe passive remainder identity.");
                previousPassive=passive.Identity;
            }
            var tokens=new HashSet<ulong>();ulong previous=0;
            foreach(var job in Jobs)
            {
                if(job.LocalId<=previous||job.LocalId>=nextLocalId||job.NativeToken>=nextNativeToken||job.NativeToken!=0&&!tokens.Add(job.NativeToken))
                    throw new InvalidDataException("Invalid or duplicate durable pipe job identity.");
                previous=job.LocalId;
            }
        }
        internal void Validate(PipeGeometry geometry,Func<int,byte>? materialPhase)
        {
            foreach(var job in Jobs)
            {
                if(!geometry.Contains(job.PipeCell)||job.WorldCell>int.MaxValue||!geometry.Contains((int)job.WorldCell))throw new InvalidDataException("Pipe job leaves its saved world.");
                if(materialPhase!=null)foreach(var part in job.Reserved.Parts)
                    if(materialPhase(part.Id)!=job.Phase)throw new InvalidDataException("Pipe job reserves the wrong phase.");
            }
        }
        internal void Write(BinaryWriter writer)
        {
            UnreleasedHeat.Write(writer);writer.Write(PassiveRemainders.Length);
            foreach(var passive in PassiveRemainders){writer.Write(passive.Identity);passive.Quantity.Write(writer);}
            Kinetic.Write(writer);Potential.Write(writer);MomentumX.Write(writer);MomentumY.Write(writer);
            writer.Write(NextLocalId);writer.Write(NextNativeToken);writer.Write(Jobs.Length);foreach(var job in Jobs)job.Write(writer);
        }
        internal static PipeBoundaryState Read(BinaryReader reader,int maximumBytes)
        {
            var heat=PipeAmount.Read(reader);var passives=reader.ReadInt32();
            if(passives<0||passives>65537||(long)passives*12>reader.BaseStream.Length-reader.BaseStream.Position)throw new InvalidDataException("Invalid pipe passive remainder count.");
            var remainders=new PipePassiveBalance[passives];
            for(var i=0;i<passives;++i)remainders[i]=new PipePassiveBalance(reader.ReadUInt64(),PipeAmount.Read(reader));
            var kinetic=PipeScaledAccount.Read(reader,maximumBytes);var potential=PipeScaledAccount.Read(reader,maximumBytes);
            var x=PipeScaledAccount.Read(reader,maximumBytes);var y=PipeScaledAccount.Read(reader,maximumBytes);
            var local=reader.ReadUInt64();var token=reader.ReadUInt64();var count=reader.ReadInt32();
            if(count<0||(long)count*69>reader.BaseStream.Length-reader.BaseStream.Position)throw new InvalidDataException("Invalid durable pipe job count.");
            var jobs=new PipePortJob[count];for(var i=0;i<count;++i)jobs[i]=PipePortJob.Read(reader);
            return new PipeBoundaryState(heat,kinetic,potential,x,y,local,token,jobs,remainders);
        }
    }
}
