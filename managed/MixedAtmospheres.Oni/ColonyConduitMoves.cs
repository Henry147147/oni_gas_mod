using System;

namespace MixedAtmospheres.Oni
{
    // How much of a pipe packet moves from one owned cell to the next, and
    // the exact move that follows (docs/mixed-pipes.md#throughput). Game-free,
    // so the rule is unit tested. Used by the network move and the staged
    // edges (bridges, valves, overflow, preferential flow): ColonyConduits.Limit,
    // Move and Remove.
    //
    // ONI decides each move in binary32 from the masses the two cells show. An
    // owned cell shows its exact mass rounded to binary64 and then to binary32
    // (the value ONI's packet holds), and never more than its capacity. The
    // rule keeps ONI's decision and the exact move of 1.10.2's code,
    // with one change:
    // - ONI's decision: at most the request, the packet the source shows and
    //   the room the target shows (capacity minus its shown mass, rounded as
    //   ONI rounds it);
    // - a move of the whole packet the source shows takes the whole exact
    //   inventory if the target then holds at most its capacity plus
    //   ExcessBound, so a cell ONI empties is empty;
    // - every other move takes exactly the float decided, capped by the exact
    //   inventory of the source and the exact room of the target (capacity
    //   minus exact content), as before; a staged edge is now also held to
    //   the shown masses, where 1.10.2 used the exact amounts alone. What
    //   stays behind is a packet ONI also shows (unless it is below half the
    //   smallest binary32 mass, which projects as empty as before). A
    //   partial move never takes a target over its capacity.
    // ExcessBound: with s the binary32 step just below the capacity, a whole
    // move between cells within their capacity exceeds it by at most 1.25 s
    // (each shown mass is within half a step of its exact mass, a target
    // below half the capacity within a quarter, and ONI's free space rounds
    // by at most half a step, only for such a target); 7.45e-8 kg in a 1 kg
    // gas pipe, 1.19e-6 kg in a 10 kg liquid pipe, plus binary64 rounding.
    // A cell over its capacity shows exactly its capacity, so ONI gives it no
    // room and moves it whole only into a cell that shows room for a full
    // packet; if that would pass the bound the move is partial instead (a
    // shown remainder stays), so the bound always holds and every move ONI
    // decides moves mass.
    internal static class PipeMoves
    {
        private static float Previous(float value)=>BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(value)-1);
        // The binary32 step just below the capacity.
        internal static double StepBelow(float capacity)=>capacity-(double)Previous(capacity);
        // How far an exact cell inventory may exceed its capacity.
        internal static double ExcessBound(float capacity)=>1.25*StepBelow(capacity)+capacity*8.8817841970012523e-16; // 2^-50 of the capacity
        internal static PipeAmount Excess(PipeInventory inventory,float capacity)=>inventory.Total.Minus(new PipeAmount(capacity));
        internal static bool WithinBound(PipeInventory inventory,float capacity)=>Excess(inventory,capacity).Minus(new PipeAmount(ExcessBound(capacity))).Sign<=0;
        // The packet a cell shows ONI: ConduitFlow.GetContents.
        internal static float Shown(PipeInventory inventory,float capacity){var mass=(float)inventory.Mass;return mass>capacity?capacity:mass;}
        // The room a target shows ONI: ConduitContents.GetEffectiveCapacity.
        internal static float Room(PipeInventory target,float capacity){var room=capacity-Shown(target,capacity);return room>0?room:0;}
        // Whether a move of `amount` takes the whole source.
        internal static bool Whole(float amount,PipeInventory source,PipeInventory target,float capacity)=>
            amount>0&&amount==Shown(source,capacity)&&WithinBound(target.Joined(source),capacity);
        // The mass ONI moves for a request (the float the native hook returns).
        internal static float Accepted(float request,PipeInventory source,PipeInventory target,float capacity)
        {
            var amount=Math.Min(request,Math.Min(Shown(source,capacity),Room(target,capacity)));
            if(!(amount>0))return 0;
            if(Whole(amount,source,target,capacity))return amount;
            var available=source.Total;var room=new PipeAmount(capacity).Minus(target.Total);
            if(available.Sign<=0||room.Sign<=0)return 0;
            amount=Math.Min(amount,(float)Math.Min(available.Value,room.Value));
            while(amount>0&&(new PipeAmount(amount).Minus(available).Sign>0||new PipeAmount(amount).Minus(room).Sign>0))
                amount=Previous(amount);
            return amount;
        }
        // The exact move for a mass ONI moves.
        internal static (PipeInventory Source,PipeInventory Target,PipeInventory Parcel) Move(PipeInventory source,PipeInventory target,float amount,float capacity,
            Func<int,PipeMaterial> material)
        {
            if(!(amount>0))return (source,target,PipeInventory.Empty);
            if(Whole(amount,source,target,capacity))return (PipeInventory.Empty,target.Joined(source),source);
            var transfer=PipeInventory.Transfer(source,target,amount,capacity,material);
            if(transfer.Parcel.Mass!=amount)throw new InvalidOperationException("Native pipe move exceeded owned inventory/capacity.");
            return (transfer.Source,transfer.Target,transfer.Parcel);
        }
    }
}
