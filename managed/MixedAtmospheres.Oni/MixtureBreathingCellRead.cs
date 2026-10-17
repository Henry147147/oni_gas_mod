using System;
using System.Linq;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Oni
{
    internal static class MixtureBreathingCellRead
    {
        internal static float OxygenMass(MixturePresentation reading, string oxygenId)
        {
            if (reading == null) throw new ArgumentNullException(nameof(reading));
            if (string.IsNullOrWhiteSpace(oxygenId)) throw new ArgumentException("An oxygen identity is required.", nameof(oxygenId));
            if (!reading.IsBreathable) return 0;
            var oxygen = reading.Constituents.SingleOrDefault(item => item.MaterialId == oxygenId);
            if (oxygen == null || oxygen.Phase != NativeMatterPhase.Gas || oxygen.MassKg <= 0)
                throw new InvalidOperationException("A breathable reading must contain its declared gaseous oxygen.");
            var projected = (float)oxygen.MassKg;
            if (float.IsInfinity(projected)) throw new NotSupportedException("Breathable oxygen mass exceeds the game's scalar range.");
            // Never advertise a binary32 mass larger than the physical source.
            if (projected > oxygen.MassKg)
                projected = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(projected) - 1);
            return projected;
        }
    }
}
