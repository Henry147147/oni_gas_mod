// The move rule of 9a2568c, kept verbatim (class and namespace renamed) so the
// tests can show the second review's counterexamples failing on it.
using System;

namespace MixedAtmospheres.Oni.Tests.Review1
{
    // How much of a pipe packet moves, and the exact move that follows
    // (docs/mixed-pipes.md#throughput). Game-free, so the rule is unit tested.
    //
    // ONI decides every pipe move in binary32 from the masses the two cells
    // show: at most the packet the source shows, and at most the room the
    // target shows (capacity minus its shown mass, rounded as ONI rounds).
    // An owned cell holds its inventory exactly; it shows that mass in
    // binary32, and never more than its capacity. The rule:
    // - the decision is ONI's, taken from the shown masses with ONI's own
    //   float arithmetic: never more than the source shows, nothing into a
    //   target that shows no room;
    // - when ONI moves the whole packet a source (or the selected part of
    //   it) shows, the whole exact inventory moves, so a source ONI leaves
    //   empty is empty; a partial move takes exactly the float ONI moves,
    //   and what stays is a packet ONI also shows;
    // - the target may then hold a little more than its capacity: what ONI
    //   counts as fitting, plus the rounding of the two shown masses. With
    //   s the binary32 step just below the capacity, the excess is at most
    //   1.25 s (the target's and the source's shown masses are each within
    //   half a step of their exact masses, at most s/2 each and s/4 for a
    //   target below half the capacity, and ONI's free space rounds by at
    //   most s/2, exactly when the target is below half the capacity),
    //   plus binary64 rounding (ExcessBound): 7.5e-8 kg in a 1 kg gas
    //   pipe, 1.2e-6 kg in a 10 kg liquid pipe. A cell over its capacity
    //   shows exactly its capacity, so ONI gives it no room and moves it
    //   whole into a cell with room for a full packet;
    // - a move that would take a target beyond that bound does not happen
    //   (the packet waits): only a full packet still over its capacity can
    //   ask for one, landing on a trace ONI counts as no mass. So the excess
    //   never grows over successive moves.
    internal static class Review1Moves
    {
        private static float Previous(float value)=>BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(value)-1);
        // The binary32 step just below the capacity.
        internal static double StepBelow(float capacity)=>capacity-(double)Previous(capacity);
        // How far an exact cell inventory may exceed its capacity.
        internal static double ExcessBound(float capacity)=>1.25*StepBelow(capacity)+capacity*8.8817841970012523e-16; // 2^-50 of the capacity
        internal static PipeAmount Excess(PipeInventory inventory,float capacity)=>inventory.Total.Minus(new PipeAmount(capacity));
        private static bool WithinBound(PipeInventory inventory,float capacity)=>Excess(inventory,capacity).Minus(new PipeAmount(ExcessBound(capacity))).Sign<=0;
        // The packet a cell (or the part of it a filter selects) shows ONI.
        internal static float Shown(PipeInventory inventory,float capacity){var mass=(float)inventory.Mass;return mass>capacity?capacity:mass;}
        // The room a target shows ONI: ConduitContents.GetEffectiveCapacity.
        internal static float Room(PipeInventory target,float capacity){var room=capacity-Shown(target,capacity);return room>0?room:0;}
        private static (PipeInventory Others,PipeInventory Part) Select(PipeInventory source,Func<int,PipeMaterial> material,int? only,bool excluding)=>
            only==null&&!excluding?(PipeInventory.Empty,source):source.Split(double.MaxValue,material,only,excluding);
        // The mass ONI moves for a request: never more than the source (or its
        // selected part) shows, nothing into a target that shows no room.
        internal static float Accepted(float request,PipeInventory source,PipeInventory target,float capacity,Func<int,PipeMaterial> material,int? only=null,bool excluding=false)
        {
            var part=Select(source,material,only,excluding).Part;var shown=Shown(part,capacity);
            var amount=Math.Min(request,Math.Min(shown,Room(target,capacity)));
            if(!(amount>0))return 0;
            var arriving=amount==shown?part:part.Split(amount,material).Parcel;
            return WithinBound(target.Joined(arriving),capacity)?amount:0;
        }
        // The exact move for a mass ONI moves: the whole source (or selected
        // part) when it is the packet the source shows, else exactly that mass.
        internal static (PipeInventory Source,PipeInventory Target,PipeInventory Parcel) Move(PipeInventory source,PipeInventory target,float amount,float capacity,
            Func<int,PipeMaterial> material,int? only=null,bool excluding=false)
        {
            if(!(amount>0))return (source,target,PipeInventory.Empty);
            var (rest,part)=Select(source,material,only,excluding);var shown=Shown(part,capacity);
            if(amount>shown)throw new InvalidOperationException($"ONI moved {amount:R} kg from a pipe packet that shows {shown:R} kg.");
            PipeInventory parcel;
            if(amount==shown)parcel=part;
            else
            {
                var split=part.Split(amount,material);
                if(split.Parcel.Mass!=amount)throw new InvalidOperationException("Native pipe move exceeded owned inventory.");
                parcel=split.Parcel;rest=rest.Joined(split.Remaining);
            }
            var joined=target.Joined(parcel);
            if(!WithinBound(joined,capacity))
                throw new InvalidOperationException($"A pipe move would hold {Excess(joined,capacity).Value:R} kg over the capacity, more than {ExcessBound(capacity):R} kg.");
            return (rest,joined,parcel);
        }
    }
}
