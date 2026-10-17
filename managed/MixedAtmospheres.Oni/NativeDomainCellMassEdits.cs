using System;
using System.Linq;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static long cellMassEditsRouted;
        private static bool cellMassEditsAvoidedNativeDispatch = true;
        internal static bool TryHandleCellMassEdits(int message, int length, int count, byte* payload)
        {
            if (!active || writingProjection || message != -1252920804 || count == 0) return false;
            try
            {
                if (payload == null) throw new ArgumentException("Native cell mass-edit payload is missing.");
                var plan = NativeCellMassEditPlan.Decode(length, count, new ReadOnlySpan<byte>(payload, checked(length * count)),
                    OwnsPublicCell, ActorFluidSpecies);
                if (plan.Length == 0) return false;
                var requests = new TransferCommand[plan.Length];
                for (var attempt = 0; attempt < 8; ++attempt)
                {
                    var source = Status.Empty; Check(Copy(ref source, new Cell[cells.Length], (uint)cells.Length));
                    for (var index = 0; index < plan.Length; ++index)
                    {
                        var edit = plan[index];
                        requests[index] = new TransferCommand { Size = 64, Version = 2, Session = source.Session,
                            ExpectedFrame = source.CompletedFrames, PublicCell = checked((uint)edit.Cell), Species = edit.Species,
                            Kind = 1, Flags = 1, Callback = -1, MassKg = edit.MassKg, Temperature = edit.TemperatureK };
                    }
                    var before = NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "message").Entries;
                    var result = TransferStatus.Empty; var code = QueueTransfers(requests, (uint)requests.Length, ref result);
                    if (code == 3) continue;
                    Check(code); transferStatus = result; cellMassEditsRouted += requests.Length;
                    cellMassEditsAvoidedNativeDispatch &= before == NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "message").Entries;
                    return true;
                }
                throw new InvalidOperationException("Native cell mass-edit frame kept changing during reservation.");
            }
            catch (Exception caught) { Fail(caught); throw; }
        }
    }
}
