namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        // Development telemetry view of the most recent completed native frame
        // already copied by Capture(); reading it performs no native call.
        internal sealed class TelemetryReading
        {
            public bool Active, Completed, Failed, ProjectionPrimed;
            public uint State;
            public ulong Session, CompletedFrames, ValidationFrames, Substeps, RejectedAttempts;
            public double PhysicalTimeSeconds, LastTimestepSeconds, MaximumRelativeMassError, MaximumRelativeEnergyError;
            public int Publications;
            public string NativeError = "";
        }
        internal static TelemetryReading? CaptureTelemetry() => !IsMode ? null : new TelemetryReading
        {
            Active = active, Completed = completed, Failed = failed, ProjectionPrimed = projectionPrimed,
            State = latest.State, Session = latest.Session, CompletedFrames = latest.CompletedFrames,
            ValidationFrames = projectionPrimed && latest.CompletedFrames >= projectionPrimedAtFrame ? latest.CompletedFrames - projectionPrimedAtFrame : 0,
            Substeps = latest.Substeps, RejectedAttempts = latest.RejectedAttempts,
            PhysicalTimeSeconds = latest.TimeSeconds, LastTimestepSeconds = latest.LastTimestepSeconds,
            MaximumRelativeMassError = latest.MaximumRelativeMassError, MaximumRelativeEnergyError = latest.MaximumRelativeEnergyError,
            Publications = publications, NativeError = latest.Error
        };
    }
}
