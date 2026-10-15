using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Threading;

namespace MixedAtmospheres.Oni
{
    // Exists before any nonempty selected-path inspection/callback. The public
    // invocation must unwind even when its header is rejected before native load.
    internal sealed class OwnedReservoirSelectedCall
    {
        internal object Identity { get; } = new object();
        internal string SelectedPath { get; }
        internal OwnedReservoirSelectedCall(string selectedPath)
        {
            if (string.IsNullOrEmpty(selectedPath)) throw new ArgumentException("A selected call requires a nonempty path.", nameof(selectedPath));
            SelectedPath = selectedPath;
        }
    }

    internal sealed class OwnedReservoirSelectedLoad
    {
        internal object Identity { get; }
        internal OwnedReservoirLoadHeaderInspection Header { get; }
        internal bool ActualHeaderVerified { get; }
        internal OwnedReservoirSelectedLoad(OwnedReservoirLoadHeaderInspection header)
            : this(new object(), header, false)
        { if (header.Kind != OwnedReservoirLoadInputKind.Unmarked) throw new ArgumentException("Only ordinary unmarked input enters native selected loading.", nameof(header)); }
        private OwnedReservoirSelectedLoad(object identity, OwnedReservoirLoadHeaderInspection header, bool verified)
        { Identity = identity; Header = header; ActualHeaderVerified = verified; }
        internal OwnedReservoirSelectedLoad WithVerifiedHeader()
        {
            if (ActualHeaderVerified) throw new InvalidOperationException("The selected native header was read more than once.");
            return new OwnedReservoirSelectedLoad(Identity, Header, true);
        }
    }

    internal sealed class OwnedReservoirLoadObservation
    {
        internal string Boundary { get; }
        internal string Detail { get; }
        internal string RecordedUtc { get; }
        internal OwnedReservoirLoadObservation(string boundary, string detail)
        {
            Boundary = boundary ?? throw new ArgumentNullException(nameof(boundary));
            Detail = detail.Length <= 2048 ? detail : detail.Substring(0, 2048);
            RecordedUtc = System.DateTime.UtcNow.ToString("O", System.Globalization.CultureInfo.InvariantCulture);
        }
    }

    // Completion capability for exactly one begun native teardown invocation.
    // The tracker also checks reference identity; an attempt snapshot is not a
    // reusable completion token, and a failed completion consumes this token.
    internal sealed class OwnedReservoirLoadTeardown
    {
        internal object AttemptIdentity { get; }
        internal OwnedReservoirLoadTeardown(object attemptIdentity) { AttemptIdentity = attemptIdentity; }
    }

    // Every instance is immutable. Updated observations share an opaque attempt
    // identity, so a stale teardown cannot release a different load attempt.
    internal sealed class OwnedReservoirLoadAttempt
    {
        internal object Identity { get; }
        internal Guid Id { get; }
        internal long Sequence { get; }
        internal string Origin { get; }
        internal OwnedReservoirLoadHeaderInspection Input { get; }
        internal bool Fatal => true;
        internal bool FrontendRequested { get; }
        internal bool TeardownStarted { get; }
        internal bool TeardownCompleted { get; }
        internal bool TeardownFailed { get; }
        internal bool SelectedCallPending { get; }
        internal int DroppedObservations { get; }
        internal IReadOnlyList<OwnedReservoirLoadObservation> Observations { get; }
        private OwnedReservoirLoadAttempt(object identity, Guid id, long sequence, string origin,
            OwnedReservoirLoadHeaderInspection input, bool frontend, bool teardown, bool completed,
            int dropped, OwnedReservoirLoadObservation[] observations, bool selectedCallPending, bool teardownFailed)
        {
            Identity = identity; Id = id; Sequence = sequence; Origin = origin; Input = input;
            FrontendRequested = frontend; TeardownStarted = teardown; TeardownCompleted = completed;
            DroppedObservations = dropped; Observations = new ReadOnlyCollection<OwnedReservoirLoadObservation>(observations);
            SelectedCallPending = selectedCallPending;
            TeardownFailed = teardownFailed;
        }
        internal static OwnedReservoirLoadAttempt Reject(long sequence, string origin, OwnedReservoirLoadHeaderInspection input, bool selectedCallPending)
        {
            if (!input.MustReject) throw new ArgumentException("A valid unmarked/no-selection input must not acquire a fatal load attempt.", nameof(input));
            return new OwnedReservoirLoadAttempt(new object(), Guid.NewGuid(), sequence, origin, input, false, false, false, 0,
                new[] { new OwnedReservoirLoadObservation(origin, input.Kind + ": " + input.Error) }, selectedCallPending, false);
        }
        internal OwnedReservoirLoadAttempt Record(string boundary, string detail, bool? frontend = null, bool? teardown = null, bool? completed = null, bool? selectedCallPending = null, bool? teardownFailed = null)
        {
            var count = Math.Min(Observations.Count + 1, 128);
            var values = new OwnedReservoirLoadObservation[count];
            var skip = Observations.Count == 128 ? 1 : 0;
            for (var i = 0; i < count - 1; ++i) values[i] = Observations[i + skip];
            values[count - 1] = new OwnedReservoirLoadObservation(boundary, detail);
            return new OwnedReservoirLoadAttempt(Identity, Id, Sequence, Origin, Input,
                FrontendRequested || frontend == true, TeardownStarted || teardown == true, TeardownCompleted || completed == true,
                checked(DroppedObservations + skip), values, selectedCallPending ?? SelectedCallPending, TeardownFailed || teardownFailed == true);
        }
    }

    // Pure lifecycle holder. No callbacks, native code or file writes under its
    // lock. There is deliberately no Dispose/reset/success/activation API.
    internal sealed class OwnedReservoirLoadAttemptTracker
    {
        private readonly object gate = new object();
        private readonly int thread = Thread.CurrentThread.ManagedThreadId;
        private long sequence;
        private OwnedReservoirLoadAttempt? current, last;
        private OwnedReservoirLoadTeardown? activeTeardown;
        private IReadOnlyList<OwnedReservoirLoadAttempt> completed = Array.Empty<OwnedReservoirLoadAttempt>();
        internal OwnedReservoirLoadAttempt? Current { get { lock (gate) return current; } }
        internal OwnedReservoirLoadAttempt? Last { get { lock (gate) return last; } }
        internal IReadOnlyList<OwnedReservoirLoadAttempt> Completed { get { lock (gate) return completed; } }
        internal bool IsFatal { get { lock (gate) return current != null; } }
        private void RequireThread()
        { if (Thread.CurrentThread.ManagedThreadId != thread) throw new InvalidOperationException("Load lifecycle mutation requires its activation thread."); }
        internal OwnedReservoirLoadAttempt Reject(string origin, OwnedReservoirLoadHeaderInspection input, bool selectedCallPending = false)
        {
            RequireThread();
            if (!input.MustReject) throw new ArgumentException("Ordinary/new-colony input cannot become a fatal attempt.", nameof(input));
            lock (gate)
            {
                if (current != null)
                {
                    current = current.Record("reentrant-load-rejected", origin + ": " + input.SelectedPath);
                    last = current; return current;
                }
                var created = OwnedReservoirLoadAttempt.Reject(checked(sequence + 1), origin, input, selectedCallPending);
                sequence = created.Sequence; current = last = created; return created;
            }
        }
        internal OwnedReservoirLoadAttempt? ObserveSelectedResult(OwnedReservoirSelectedCall? selected, string origin, bool result)
        {
            RequireThread();
            if (selected == null || result) return Current;
            var input = new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.ReadError,
                selected.SelectedPath, -1, false,
                "Selected native loader returned false at " + origin + ".", null, Array.Empty<byte>());
            return Reject(origin + ":false-result", input, selectedCallPending: true);
        }
        internal OwnedReservoirLoadAttempt HoldSelectedCall(OwnedReservoirLoadAttempt expected)
        {
            RequireThread();
            lock (gate)
            {
                if (current == null || !ReferenceEquals(current.Identity, expected.Identity))
                    throw new InvalidOperationException("The selected call cannot hold a stale or foreign fatal attempt.");
                current = current.Record("selected-load:held", "The exact public selected call must unwind before fatal release.", selectedCallPending: true);
                last = current; return current;
            }
        }
        internal bool Record(OwnedReservoirLoadAttempt expected, string boundary, string detail)
        {
            lock (gate)
            {
                if (current == null || !ReferenceEquals(current.Identity, expected.Identity)) return false;
                current = current.Record(boundary, detail); last = current; return true;
            }
        }
        internal bool TryRequestFrontend(OwnedReservoirLoadAttempt expected)
        {
            RequireThread();
            lock (gate)
            {
                if (current == null || !ReferenceEquals(current.Identity, expected.Identity) || current.FrontendRequested || current.TeardownStarted) return false;
                current = current.Record("frontend-request", "Requested once; native shutdown is not called by this boundary.", frontend: true);
                last = current; return true;
            }
        }
        internal OwnedReservoirLoadTeardown? BeginTeardown(OwnedReservoirLoadAttempt expected)
        {
            RequireThread();
            lock (gate)
            {
                if (current == null || !ReferenceEquals(current.Identity, expected.Identity) || current.TeardownStarted) return null;
                var token = new OwnedReservoirLoadTeardown(current.Identity);
                current = current.Record("Game.DestroyInstances:begin", "Fatal gates stay active throughout actual teardown.", teardown: true);
                activeTeardown = token;
                last = current; return token;
            }
        }
        internal bool CompleteTeardown(OwnedReservoirLoadTeardown expected, Exception? error)
        {
            RequireThread();
            lock (gate)
            {
                if (expected == null || current == null || !ReferenceEquals(activeTeardown, expected) || !ReferenceEquals(current.Identity, expected.AttemptIdentity)) return false;
                // One completion only, including failure. No stale successful
                // finalizer can change a failed outer teardown into success.
                activeTeardown = null;
                if (error != null)
                {
                    current = current.Record("Game.DestroyInstances:failed", error.GetType().Name + ": " + error.Message, teardownFailed: true);
                    last = current; return false;
                }
                current = current.Record("Game.DestroyInstances:complete", "Actual teardown completed; any outstanding selected call must still unwind.", completed: true);
                last = current;
                if (current.SelectedCallPending) return false;
                Finish(); return true;
            }
        }
        internal bool ReleaseSelectedCall(OwnedReservoirLoadAttempt expected)
        {
            RequireThread();
            lock (gate)
            {
                if (current == null || !ReferenceEquals(current.Identity, expected.Identity) || !current.SelectedCallPending) return false;
                current = current.Record("selected-load:unwound", "The exact original selected call has returned/finalized.", selectedCallPending: false);
                last = current;
                if (!current.TeardownCompleted || current.TeardownFailed) return false;
                Finish(); return true;
            }
        }
        private void Finish()
        {
                var finished = current!;
                var count = Math.Min(completed.Count + 1, 16);
                var values = new OwnedReservoirLoadAttempt[count];
                var skip = completed.Count == 16 ? 1 : 0;
                for (var i = 0; i < count - 1; ++i) values[i] = completed[i + skip];
                values[count - 1] = finished;
                completed = new ReadOnlyCollection<OwnedReservoirLoadAttempt>(values);
                last = finished; current = null;
        }
    }
}
