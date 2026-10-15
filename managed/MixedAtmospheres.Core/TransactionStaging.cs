using System;

namespace MixedAtmospheres.Core
{
    public sealed partial class FluidTransaction
    {
        private int failAfterStagedWrite;

        // A bounded, transaction-local test seam. No arbitrary callback runs
        // inside a write group or under the authority publication lock.
        internal void InjectStagedWriteFailureForTesting(int completedWrites)
        {
            CheckOpen();
            if (completedWrites < 1 || completedWrites > 3) throw new ArgumentOutOfRangeException(nameof(completedWrites));
            failAfterStagedWrite = completedWrites;
        }

        private void StagedWriteCompleted()
        {
            if (failAfterStagedWrite > 0 && --failAfterStagedWrite == 0)
                throw new StagedFluidWriteException();
        }

        // Once a staged write begins, an exception may leave an incomplete
        // multi-endpoint mutation. Poison the whole transaction before it can
        // be caught by a caller and later prepared/published. Validation and
        // inventory construction happen before these helpers and remain retryable.
        private void StageCell(int cell, FluidInventory inventory)
        {
            try { changes[cell] = inventory; StagedWriteCompleted(); }
            catch { closed = true; throw; }
        }

        private void StageGeometry(int cell, int element)
        {
            try { geometryChanges[cell] = element; StagedWriteCompleted(); }
            catch { closed = true; throw; }
        }

        private void StageContainer(string id, FluidContainerSnapshot? container)
        {
            // The lazy dictionary allocation belongs inside the same guard.
            try { ContainerChanges[id] = container; StagedWriteCompleted(); }
            catch { closed = true; throw; }
        }
    }

    internal sealed class StagedFluidWriteException : InvalidOperationException
    {
        internal StagedFluidWriteException() : base("Injected failure after a staged transaction write.") { }
    }
}
