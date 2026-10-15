using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using MixedAtmospheres.Core;

internal static class PreparedFluidCommitTests
{
    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<ColonyFluidState> create, int source, int destination)
    {
        test("prepared frames remain unpublished and cancellation releases their entire candidate", () =>
        {
            var state = create(); var before = state.CapturePublication(); var bytes = state.Save();
            using var transaction = state.BeginTransaction(); transaction.Transfer(source, destination, 0.5);
            var candidate = transaction.PrepareFrame(7, 2.5);
            check(ReferenceEquals(before, state.CapturePublication()) && bytes.SequenceEqual(state.Save()));
            check(candidate.Snapshot.CompletedFrame == 7 && candidate.Snapshot.SampleCell(destination).Inventory.TotalMassKg == 0.5);
            reject(() => transaction.Commit()); candidate.ReserveWriter(); candidate.Dispose();
            reject(() => candidate.Commit());
            using var replacement = state.BeginTransaction(); replacement.Transfer(source, destination, 0.25); replacement.Commit();
            check(before.Snapshot.SampleCell(destination).Inventory.TotalMassKg == 0 && state.CaptureSnapshot().SampleCell(destination).Inventory.TotalMassKg == 0.25);
        });
        test("stale prepared candidates reject before writer reservation or companion publication", () =>
        {
            var state = create(); using var old = state.BeginTransaction(); old.Transfer(source, destination, 0.5);
            using var candidate = old.PrepareFrame(1, 1);
            using (var fresh = state.BeginTransaction()) { fresh.Transfer(source, destination, 0.25); fresh.Commit(); }
            var before = state.CapturePublication(); reject(candidate.ReserveWriter); reject(candidate.EnsureCurrent);
            reject(() => candidate.Commit(new object())); check(ReferenceEquals(before, state.CapturePublication()));
        });
        test("writer reservation excludes competing threads through explicit consumption release", () =>
        {
            var state = create(); using var transaction = state.BeginTransaction(); transaction.Transfer(source, destination, 0.5);
            using var candidate = transaction.PrepareFrame(1, 1); candidate.ReserveWriter();
            var blocked = Task.Run(() =>
            {
                using var competing = state.BeginTransaction(); competing.Transfer(source, destination, 0.25);
                reject(() => competing.Commit());
            });
            check(blocked.Wait(TimeSpan.FromSeconds(5)));
            var companion = new Companion(candidate.Snapshot);
            var published = candidate.Commit(companion, retainReservationUntilDisposed: true);
            check(ReferenceEquals(published, state.CapturePublication()) && ReferenceEquals(published.Companion, companion));
            using (var competing = state.BeginTransaction()) { competing.Transfer(source, destination, 0.25); reject(() => competing.Commit()); }
            candidate.Dispose();
            using (var next = state.BeginTransaction()) { next.Transfer(source, destination, 0.25); next.Commit(); }
            check(state.CaptureSnapshot().SampleCell(destination).Inventory.TotalMassKg == 0.75);
        });
        test("readers observe one coherent state companion pair during repeated publication", () =>
        {
            var state = create(); var finished = 0; var reads = 0;
            var reader = Task.Run(() =>
            {
                while (Volatile.Read(ref finished) == 0)
                {
                    var publication = state.CapturePublication();
                    if (publication.Companion is Companion companion) check(ReferenceEquals(companion.Snapshot, publication.Snapshot));
                    Interlocked.Increment(ref reads);
                }
            });
            check(SpinWait.SpinUntil(() => Volatile.Read(ref reads) != 0, TimeSpan.FromSeconds(5)));
            try
            {
                for (var frame = 1; frame <= 100; ++frame)
                {
                    using var transaction = state.BeginTransaction();
                    using var candidate = transaction.PrepareFrame(frame, frame * 0.2);
                    candidate.Commit(new Companion(candidate.Snapshot));
                }
            }
            finally { Volatile.Write(ref finished, 1); }
            check(reader.Wait(TimeSpan.FromSeconds(5)) && state.CaptureSnapshot().CompletedFrame == 100);
            // The opaque live companion is deliberately excluded from custom persistence.
            var saved = state.Save();
            using var ordinary = state.BeginTransaction(); ordinary.Commit();
            check(state.CapturePublication().Companion == null && saved.Length == state.Save().Length);
        });
    }

    private sealed class Companion
    {
        internal ColonyFrameSnapshot Snapshot { get; }
        internal Companion(ColonyFrameSnapshot snapshot) { Snapshot = snapshot; }
    }
}
