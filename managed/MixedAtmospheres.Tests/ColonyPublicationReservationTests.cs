using System;
using System.Linq;
using System.Threading.Tasks;
using MixedAtmospheres.Core;

internal static class ColonyPublicationReservationTests
{
    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<ColonyFluidState> create, int source, int destination)
    {
        test("read save reservation preserves exact publication companion revision and checkpoint", () =>
        {
            var state = create(); var companion = new object();
            using (var tx = state.BeginTransaction()) using (var prepared = tx.PrepareFrame(4, .8)) prepared.Commit(companion);
            var publication = state.CapturePublication(); var bytes = state.Save();
            using var reservation = state.ReservePublication(publication);
            check(ReferenceEquals(reservation.Source, publication) && ReferenceEquals(reservation.Snapshot, publication.Snapshot));
            reservation.EnsureCurrent(); check(bytes.SequenceEqual(reservation.SaveCheckpoint()));
            check(ReferenceEquals(state.CapturePublication(), publication) && ReferenceEquals(publication.Companion, companion));
            check(publication.Snapshot.CompletedFrame == 4 && publication.Snapshot.Revision == 1);
            reject(() => state.ReservePublication(publication));
        });
        test("read save reservation rejects stale foreign and null publication identity", () =>
        {
            var state = create(); var stale = state.CapturePublication();
            using (var tx = state.BeginTransaction()) tx.Commit();
            reject(() => state.ReservePublication(stale)); reject(() => state.ReservePublication(create().CapturePublication()));
            var clone = ColonyFluidState.Restore(state.Save(), state.CaptureSnapshot().Catalog);
            check(clone.CaptureSnapshot().ColonyId == state.CaptureSnapshot().ColonyId);
            reject(() => state.ReservePublication(clone.CapturePublication()));
            reject(() => state.ReservePublication(null!));
            using var valid = state.ReservePublication(state.CapturePublication()); valid.EnsureCurrent();
        });
        test("read save reservation excludes pending and retained prepared writer reservations", () =>
        {
            var state = create(); using var tx = state.BeginTransaction(); using var prepared = tx.PrepareFrame(1, .2);
            prepared.ReserveWriter(); reject(() => state.ReservePublication(state.CapturePublication()));
            var published = prepared.Commit(retainReservationUntilDisposed: true);
            reject(() => state.ReservePublication(published)); prepared.Dispose();
            using var reservation = state.ReservePublication(published); reservation.EnsureCurrent();
        });
        test("read save reservation blocks old and newly staged writes while readers stay available", () =>
        {
            var state = create(); var before = state.CapturePublication();
            using var oldTx = state.BeginTransaction(); oldTx.Transfer(source, destination, .5);
            using var oldPrepared = oldTx.PrepareFrame(1, .2);
            using var reservation = state.ReservePublication(before);
            reject(oldPrepared.ReserveWriter); reject(() => oldPrepared.Commit());
            var worker = Task.Run(() =>
            {
                check(ReferenceEquals(before, state.CapturePublication()));
                check(state.CaptureSnapshot().SampleCell(destination).Inventory.TotalMassKg == 0);
                using var tx = state.BeginTransaction(); tx.Transfer(source, destination, .25);
                using var prepared = tx.PrepareFrame(1, .2); reject(prepared.ReserveWriter); reject(() => prepared.Commit());
            });
            check(worker.Wait(TimeSpan.FromSeconds(5))); check(ReferenceEquals(before, state.CapturePublication()));
            reservation.Dispose(); oldPrepared.Commit();
            check(state.CaptureSnapshot().SampleCell(destination).Inventory.TotalMassKg == .5);
        });
        test("released save reservation cannot serialize or release a reentrant successor", () =>
        {
            var state = create(); var before = state.CapturePublication(); var first = state.ReservePublication(before);
            ColonyPublicationReservation? successor = null;
            void Callback()
            {
                first.Dispose(); successor = state.ReservePublication(before); first.Dispose();
                reject(first.EnsureCurrent); reject(() => first.SaveCheckpoint()); successor.EnsureCurrent();
                using var tx = state.BeginTransaction(); reject(() => tx.Commit());
            }
            Callback(); first.Dispose(); successor!.EnsureCurrent(); successor.Dispose();
            using var next = state.BeginTransaction(); next.Commit();
            check(before.Snapshot.Revision + 1 == state.CaptureSnapshot().Revision);
            // Even a valid new publication can never revive the disposed lease.
            reject(first.EnsureCurrent); reject(() => first.SaveCheckpoint()); check(ReferenceEquals(first.Source, before));
        });
        test("save reservation finally release survives callback failure without holding the core lock", () =>
        {
            var state = create(); var before = state.CapturePublication(); var callbackRan = false;
            try
            {
                using var reservation = state.ReservePublication(before);
                var reader = Task.Run(() => check(ReferenceEquals(before, state.CapturePublication())));
                check(reader.Wait(TimeSpan.FromSeconds(5))); callbackRan = true;
                throw new ApplicationException("external save callback failed");
            }
            catch (ApplicationException) { }
            check(callbackRan && ReferenceEquals(before, state.CapturePublication()));
            using var transaction = state.BeginTransaction(); transaction.Transfer(source, destination, .25); transaction.Commit();
        });
    }
}
