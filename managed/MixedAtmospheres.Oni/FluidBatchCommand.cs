using System;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    // Closed, immutable commands only. There is deliberately no delegate or
    // native Storage/actor operation hidden inside an internal inventory move.
    internal sealed class FluidBatchCommand
    {
        private enum CommandKind { NativeMessage, CellToContainer, ContainerToCell, ContainerToContainer }
        private enum Selection { Bulk, Species, All }
        private readonly CommandKind kind;
        private readonly Selection selection;
        private readonly FluidBatchMessage? message;
        private readonly int cell;
        private readonly FluidContainerHandle source, destination;
        private readonly string? materialId;
        private readonly double massKg;

        private FluidBatchCommand(FluidBatchMessage message)
        { kind = CommandKind.NativeMessage; this.message = message ?? throw new ArgumentNullException(nameof(message)); }

        private FluidBatchCommand(CommandKind kind, Selection selection, int cell,
            FluidContainerHandle source, FluidContainerHandle destination, double massKg = 0, string? materialId = null)
        {
            this.kind = kind; this.selection = selection; this.cell = cell;
            this.source = source; this.destination = destination; this.massKg = massKg; this.materialId = materialId;
        }

        internal static FluidBatchCommand NativeMessage(FluidBatchMessage message) => new FluidBatchCommand(message);
        internal static FluidBatchCommand CellToContainer(int sourceCell, FluidContainerHandle destination, double massKg)
            => new FluidBatchCommand(CommandKind.CellToContainer, Selection.Bulk, sourceCell, default, destination, massKg);
        internal static FluidBatchCommand CellToContainer(int sourceCell, FluidContainerHandle destination, string materialId, double massKg)
            => new FluidBatchCommand(CommandKind.CellToContainer, Selection.Species, sourceCell, default, destination, massKg, materialId);
        internal static FluidBatchCommand AllCellToContainer(int sourceCell, FluidContainerHandle destination)
            => new FluidBatchCommand(CommandKind.CellToContainer, Selection.All, sourceCell, default, destination);
        internal static FluidBatchCommand ContainerToCell(FluidContainerHandle source, int destinationCell, double massKg)
            => new FluidBatchCommand(CommandKind.ContainerToCell, Selection.Bulk, destinationCell, source, default, massKg);
        internal static FluidBatchCommand ContainerToCell(FluidContainerHandle source, int destinationCell, string materialId, double massKg)
            => new FluidBatchCommand(CommandKind.ContainerToCell, Selection.Species, destinationCell, source, default, massKg, materialId);
        internal static FluidBatchCommand AllContainerToCell(FluidContainerHandle source, int destinationCell)
            => new FluidBatchCommand(CommandKind.ContainerToCell, Selection.All, destinationCell, source, default);
        internal static FluidBatchCommand ContainerToContainer(FluidContainerHandle source, FluidContainerHandle destination, double massKg)
            => new FluidBatchCommand(CommandKind.ContainerToContainer, Selection.Bulk, 0, source, destination, massKg);
        internal static FluidBatchCommand ContainerToContainer(FluidContainerHandle source, FluidContainerHandle destination, string materialId, double massKg)
            => new FluidBatchCommand(CommandKind.ContainerToContainer, Selection.Species, 0, source, destination, massKg, materialId);
        internal static FluidBatchCommand AllContainerToContainer(FluidContainerHandle source, FluidContainerHandle destination)
            => new FluidBatchCommand(CommandKind.ContainerToContainer, Selection.All, 0, source, destination);

        // Validation belongs to the same transaction as every earlier command:
        // stale handles, phase/capacity limits and unavailable mass fail before
        // a prepared frame or any native intent can escape the coordinator.
        internal FluidMutationCommit? StageInto(FluidTransaction transaction, ExperimentalFluidMessageRouter router)
        {
            switch (kind)
            {
                case CommandKind.NativeMessage:
                    return router.StageInto(transaction, message!.Message, message.Payload, message.DisplacementCell);
                case CommandKind.CellToContainer:
                    if (selection == Selection.All) transaction.TransferAllCellToContainer(cell, destination);
                    else if (selection == Selection.Species) transaction.TransferCellToContainer(cell, destination, materialId!, massKg);
                    else transaction.TransferCellToContainer(cell, destination, massKg);
                    break;
                case CommandKind.ContainerToCell:
                    if (selection == Selection.All) transaction.TransferAllContainerToCell(source, cell);
                    else if (selection == Selection.Species) transaction.TransferContainerToCell(source, cell, materialId!, massKg);
                    else transaction.TransferContainerToCell(source, cell, massKg);
                    break;
                case CommandKind.ContainerToContainer:
                    if (selection == Selection.All) transaction.TransferAllContainerToContainer(source, destination);
                    else if (selection == Selection.Species) transaction.TransferContainerToContainer(source, destination, materialId!, massKg);
                    else transaction.TransferContainerToContainer(source, destination, massKg);
                    break;
                default: throw new InvalidOperationException("Unknown closed fluid command kind.");
            }
            return null; // Internal moves have no external ledger/callback/native effect.
        }
    }
}
