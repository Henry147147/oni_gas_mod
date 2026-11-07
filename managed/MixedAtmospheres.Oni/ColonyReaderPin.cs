using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    // What the game's readers see under overlapped pacing
    // (docs/colony-owner.md#overlapped-pacing). Every game-thread reader of a
    // publication sees one owner frame: the state the previous positive native
    // frame started from, the same at every publication until the next
    // positive tick, whatever the owner's timing. Save barriers read the
    // latest frame and pin the readers at it when they end.
    internal static class ColonyReaderPin
    {
        private static bool pinned;
        private static int following;
        private static ulong routedFrame;
        internal static bool Pinned => pinned;
        // How many frames the owner's latest completed frame can be ahead of
        // the readers' frame: two with overlapped pacing, one with serial.
        internal static int OwnerLead => pinned ? 2 : 1;
        internal static bool FollowingOwner => following != 0;
        internal static void Reset() { pinned = false; following = 0; routedFrame = 0; ColonyRouting.Reset(); }

        // Called before a publication's reads.
        internal static void Show(ulong session)
        {
            if (following != 0) return;
            if (!ColonyPacing.Overlapped)
            {
                if (pinned) { OniNativeColony.Check(OniNativeColony.FollowFrames(session, 1)); pinned = false; ColonyRouting.Reset(); }
                return;
            }
            var shown = ColonyActivity.Frames.Shown;
            OniNativeColony.Check(OniNativeColony.ShowFrame(session, shown, out var found));
            pinned = true;
            if (found != 0 && shown != routedFrame) { ColonyRouting.NativeChanges(session, shown); routedFrame = shown; }
            ColonyRouting.Expire(shown);
        }
        // A save barrier's end: the readers stay at the frame it settled, whose
        // ownership the routing then reads directly.
        private static void PinAtLatest()
        { OniNativeColony.Check(OniNativeColony.FollowFrames(ColonyOwner.Status.Session, 0)); pinned = true; ColonyRouting.Reset(); }

        internal readonly struct FollowScope : IDisposable
        {
            private readonly bool overlapped;
            internal FollowScope(bool overlapped) { this.overlapped = overlapped; }
            public void Dispose()
            {
                if (--following != 0 || !overlapped || !ColonyOwner.Active) return;
                // Never mask the barrier's own failure; the next publication pins the readers instead.
                try { PinAtLatest(); }
                catch (Exception error) { UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Readers could not be pinned after a save barrier: " + error.Message); }
            }
        }
        // Inside a barrier every game-thread reader follows the owner's latest
        // frame, so the barrier delivers each completed frame and its receipts.
        internal static FollowScope FollowOwner()
        {
            var overlapped = following == 0 && ColonyPacing.Overlapped;
            if (overlapped) { OniNativeColony.Check(OniNativeColony.FollowFrames(ColonyOwner.Status.Session, 1)); ColonyRouting.Reset(); }
            ++following;
            return new FollowScope(overlapped);
        }
    }

    // Which cells' messages the owner takes (docs/colony-owner.md#overlapped-pacing).
    // Readers lag the owner by two frames, but an edit the game makes now is
    // applied by the frame after the one it posts next: routing it by the
    // readers' ownership alone would send an edit for a cell opened or closed
    // since then to the wrong side. Routing adds to that ownership every change
    // the frames after it make that is known here: the open and close edits
    // this adapter queued, and those the native frame that started the
    // readers' next frame captured (solids landing, tiles melting, freezes).
    // Each entry lasts until the readers' frame includes it.
    internal static class ColonyRouting
    {
        private static readonly Dictionary<int, (bool Owned, ulong Frame)> changes = new Dictionary<int, (bool, ulong)>();
        private static OniNativeColony.Terrain[] buffer = new OniNativeColony.Terrain[64];
        internal static int Pending => changes.Count;
        internal static void Reset() => changes.Clear();
        // The owner's view of a cell for routing, or null when no change is known since the readers' frame.
        internal static bool? Owned(int cell) => changes.Count != 0 && changes.TryGetValue(cell, out var change) ? change.Owned : (bool?)null;
        // An open or close this adapter queues joins the activity frame posted next.
        internal static void Queued(int cell, uint kind)
        {
            if (!ColonyReaderPin.Pinned || ColonyReaderPin.FollowingOwner) return;
            if (kind == OniNativeColony.Open || kind == OniNativeColony.Close) Record(cell, kind == OniNativeColony.Open, ColonyActivity.Frames.Posted + 1);
        }
        internal static void NativeChanges(ulong session, ulong frame)
        {
            uint count;
            while (true)
            {
                OniNativeColony.Check(OniNativeColony.HandoffTerrain(session, frame, buffer, (uint)buffer.Length, out count));
                if (count <= buffer.Length) break;
                buffer = new OniNativeColony.Terrain[count];
            }
            for (var k = 0; k < count; ++k) Record((int)buffer[k].PublicCell, buffer[k].Kind == OniNativeColony.Open, frame);
        }
        // A frame applies its adapter edits before its native ones (recorded
        // later here), and a cell's last open or close in a frame decides it,
        // as does a later frame's.
        private static void Record(int cell, bool owned, ulong frame)
        {
            if (changes.TryGetValue(cell, out var known) && known.Frame > frame) return;
            changes[cell] = (owned, frame);
        }
        private static readonly List<int> expired = new List<int>();
        // The readers' frame includes every activity frame before `shown`.
        internal static void Expire(ulong shown)
        {
            if (changes.Count == 0) return;
            expired.Clear();
            foreach (var pair in changes) if (pair.Value.Frame < shown) expired.Add(pair.Key);
            foreach (var cell in expired) changes.Remove(cell);
        }
    }
}
