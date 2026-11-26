using System;

namespace MixedAtmospheres.Oni
{
    // The arithmetic of an owner-aware gas intake for another mod's component
    // (OtherModCompat.Rephysicalized.cs): how much a cell's constituent may
    // give, how much room is left while requests are in flight, and what the
    // owner's receipts settle. Pure, so tools/DistributionTests controls it.
    internal sealed class IntakeLedger
    {
        // Requested and not yet answered by a consumed-mass receipt (kg).
        internal float InFlight { get; private set; }
        internal int Pending { get; private set; }
        internal double Requested, Credited, Returned;
        internal int Expired;
        private double lastRequest;

        // Room in the component's storage: its capacity less what it holds
        // and what is on its way.
        internal static float Room(float capacity, float stored, float inFlight) => Math.Max(0f, capacity - stored - Math.Max(0f, inFlight));

        // A constituent's mass as a binary32 that never exceeds it.
        internal static float RoundDown(double mass)
        {
            if (!(mass > 0)) return 0f;
            var value = (float)mass;
            if (value > mass) value = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(value) - 1);
            return value > 0 ? value : 0f;
        }

        // What one constituent may give: the rest of this tick's rate, the
        // constituent (rounded down) and the room, whichever is least.
        internal static float Take(double constituent, float remaining, float room)
        {
            var take = Math.Min(remaining, Math.Min(RoundDown(constituent), room));
            return take > 0 ? take : 0f;
        }

        internal void Request(float amount, double now)
        {
            InFlight += amount; ++Pending; Requested += amount; lastRequest = now;
        }

        // A receipt for a request of `asked` kg that delivered `got` kg.
        internal void Settle(float asked, float got, bool credited)
        {
            InFlight = Pending <= 1 ? 0f : Math.Max(0f, InFlight - asked);
            Pending = Math.Max(0, Pending - 1);
            if (got > 0) { if (credited) Credited += got; else Returned += got; }
        }

        // Requests that were never answered (the owner stopped, a load
        // dropped its queue) must not hold the room for ever.
        internal bool Expire(double now, double after)
        {
            if (Pending == 0 || now - lastRequest < after) return false;
            InFlight = 0; Pending = 0; ++Expired; return true;
        }
    }
}
