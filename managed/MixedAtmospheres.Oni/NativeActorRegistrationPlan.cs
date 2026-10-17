using System;
using System.Buffers.Binary;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    // Decode the entire native registration packet before publishing any
    // callback binding. A late duplicate/material/radius error cannot leave a
    // partial registration behind. Native registrations remain inactive until
    // their ordinary handle callbacks and custom configuration arrive.
    internal readonly struct NativeActorRegistration
    {
        internal readonly int Callback, Cell, Blocked, Unblocked;
        internal readonly uint Kind, Species;
        internal NativeActorRegistration(int callback, int cell, uint kind, uint species, int blocked, int unblocked)
        { Callback=callback; Cell=cell; Kind=kind; Species=species; Blocked=blocked; Unblocked=unblocked; }
    }

    internal static class NativeActorRegistrationPlan
    {
        internal static NativeActorRegistration[] Decode(int message, int length, int count, ReadOnlySpan<byte> payload,
            Func<int,bool> ownsCell, Func<ushort,uint> species, IEnumerable<int> pendingCallbacks)
        {
            var consumer=message==2024405073;
            if ((!consumer && message!=-505471181) || length!=(consumer ? 12 : 16) || count<0 ||
                payload.Length!=checked(length*count)) throw new ArgumentException("Invalid actor registration batch.");
            var callbacks=new HashSet<int>(pendingCallbacks);
            var result=new List<NativeActorRegistration>();
            for(var index=0;index<count;++index)
            {
                var data=payload.Slice(index*length,length);
                var cell=consumer ? BinaryPrimitives.ReadInt32LittleEndian(data) : 0;
                if(consumer && !ownsCell(cell)) continue;
                var callback=BinaryPrimitives.ReadInt32LittleEndian(data.Slice(4));
                if(consumer && (data[8]!=0 || data[9]!=0 || callback==-1))
                    throw new NotSupportedException("Owned consumers require selective, single-cell registration with a callback.");
                if(callback==-1) continue;
                if(!callbacks.Add(callback)) throw new ArgumentException("Actor registration callback is already pending or repeated in this batch.");
                var material=consumer ? species(BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(10))) : 0;
                result.Add(new NativeActorRegistration(callback,cell,consumer ? 1U : 2U,material,
                    consumer ? -1 : BinaryPrimitives.ReadInt32LittleEndian(data.Slice(8)),
                    consumer ? -1 : BinaryPrimitives.ReadInt32LittleEndian(data.Slice(12))));
                if(result.Count>256) throw new NotSupportedException("Actor registration batches admit at most 256 callback bindings.");
            }
            return result.ToArray();
        }
    }
}
