using System;

namespace MixedAtmospheres.Core
{
    public sealed partial class ColonyFluidState
    {
        /// <summary>Pins exactly the expected immutable publication without creating a transaction, revision or companion.</summary>
        public ColonyPublicationReservation ReservePublication(ColonyFramePublication expectedPublication)
        {
            if (expectedPublication == null) throw new ArgumentNullException(nameof(expectedPublication));
            lock (sync)
            {
                if (!ReferenceEquals(current, expectedPublication)) throw new InvalidOperationException("The expected publication is stale or belongs to another authority.");
                if (reservedWriter != null || reservedPublication != null) throw new InvalidOperationException("The colony already has a writer or read/save reservation.");
                var reservation = new ColonyPublicationReservation(this, expectedPublication);
                reservedPublication = reservation; return reservation;
            }
        }

        internal void RequireReservation(ColonyPublicationReservation reservation)
        {
            lock (sync)
                if (!ReferenceEquals(reservedPublication, reservation) || !ReferenceEquals(current, reservation.Source))
                    throw new InvalidOperationException("The read/save reservation was released or its publication is no longer current.");
        }

        internal void ReleaseReservation(ColonyPublicationReservation reservation)
        {
            lock (sync)
                if (ReferenceEquals(reservedPublication, reservation)) reservedPublication = null;
        }

        internal static byte[] SavePinnedSnapshot(ColonyFrameSnapshot snapshot) => SaveSnapshot(snapshot, SaveMagic);
    }

    /// <summary>
    /// Exclusive immutable-publication read/save lease. Readers and optimistic staging remain available;
    /// commits and prepared-writer reservations fail until disposal. This supplies no native save protocol.
    /// </summary>
    public sealed class ColonyPublicationReservation : IDisposable
    {
        private readonly ColonyFluidState owner;
        public ColonyFramePublication Source { get; }
        public ColonyFrameSnapshot Snapshot => Source.Snapshot;
        internal ColonyPublicationReservation(ColonyFluidState owner, ColonyFramePublication source)
        { this.owner = owner; Source = source; }

        /// <summary>Revalidates this exact lease and source identity. No external delegate is invoked under the publication lock.</summary>
        public void EnsureCurrent() => owner.RequireReservation(this);

        /// <summary>Serializes the pinned source, never a resampled publication; release during serialization rejects the result.</summary>
        public byte[] SaveCheckpoint()
        {
            EnsureCurrent();
            var checkpoint = ColonyFluidState.SavePinnedSnapshot(Snapshot);
            EnsureCurrent(); return checkpoint;
        }

        /// <summary>Idempotently releases only this lease. It can never release a subsequently acquired reservation.</summary>
        public void Dispose() => owner.ReleaseReservation(this);
    }
}
