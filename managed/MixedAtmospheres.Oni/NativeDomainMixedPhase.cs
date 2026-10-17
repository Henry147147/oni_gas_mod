using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool MixedHeatMode => restoredScenarioSelected ? restoredScenario == 5 :
            ColonyRuntimeValidation.Settings?.Mode == "native-mixed-heat-domain";
        private static bool MixedConductionMode => MixedHeatMode || ViscousMode;
        private static bool MixedPhaseMode => MixedConductionMode || (restoredScenarioSelected ? restoredScenario == 4 :
            ColonyRuntimeValidation.Settings?.Mode == "native-mixed-domain");
        private static bool MixedPhaseVerified => MixedPhaseMode && publishedReadings != null && publishedReadings.Count == cells.Length &&
            cells.All(cell => cell.Water > 0 && cell.LiquidFraction > 0 && cell.LiquidFraction < 1 &&
                cell.Nitrogen + cell.Oxygen + cell.Helium + cell.CarbonDioxide > 0) &&
            publishedReadings.Values.All(value => value.GasVolumeCubicMetres > 0 && value.LiquidVolumeCubicMetres > 0 &&
                value.VacuumVolumeCubicMetres == 0 && value.GasPressurePa == value.MechanicalPressurePa && value.Revision == (long)latest.CompletedFrames);
        private static object MixedPhaseReport() => new {
            Required = MixedPhaseMode, Verified = MixedPhaseVerified,
            Source = "completed-native-EOS-partial-volumes", NativeScenario = ViscousMode ? 6 : MixedHeatMode ? 5 : 4,
            ConductionModel = MixedConductionMode ? "complete-EOS-contact-split-backward-Euler-v1" : "disabled",
            SinglePhaseConductionDisabled = MixedPhaseMode && thermalStatus.Enabled == 0,
            LiquidWorldRenderingVerified = false, FreeSurfaceDynamicsVerified = false
        };
    }
}
