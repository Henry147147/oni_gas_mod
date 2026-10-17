using System;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static OniNativeDomain.ViscosityStatus viscosityStatus;
        private static bool ViscousMode => restoredScenarioSelected ? restoredScenario == 6 :
            ColonyRuntimeValidation.Settings?.Mode == "native-viscous-domain";
        private static bool ViscosityVerified => ViscousMode && viscosityStatus.Enabled == 1 && viscosityStatus.State == 1 &&
            viscosityStatus.Session == latest.Session && viscosityStatus.CompletedFrame == latest.CompletedFrames &&
            viscosityStatus.ShearPaSeconds == .5 && viscosityStatus.BulkPaSeconds == .25 &&
            viscosityStatus.Stages == 2 * latest.Substeps && viscosityStatus.Modes > 0 &&
            viscosityStatus.DissipatedKineticEnergy > 0 && viscosityStatus.StoredHeat > 0 && viscosityStatus.EntropyLowerBound > 0 &&
            FiniteViscosityValue(viscosityStatus.DissipatedKineticEnergy) && FiniteViscosityValue(viscosityStatus.StoredHeat) &&
            FiniteViscosityValue(viscosityStatus.EntropyLowerBound) && FiniteViscosityValue(viscosityStatus.WallImpulseX) &&
            FiniteViscosityValue(viscosityStatus.WallImpulseY);
        private static bool FiniteViscosityValue(double value) => !double.IsNaN(value) && !double.IsInfinity(value);
        private static object ViscosityReport() => new {
            Required = ViscousMode, Verified = ViscosityVerified, Source = "completed-native-worker-physical-stress",
            Model = "plane-strain-Newtonian-exact-mode-split-v1", Boundary = "impermeable-free-slip",
            Status = viscosityStatus, CompleteMaterialViscosityModel = false
        };
    }
}
