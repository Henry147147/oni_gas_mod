using System;
using System.Linq;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool ConfiguredVacuumMode => ColonyRuntimeValidation.Settings?.Mode == "native-configured-vacuum-domain" ||
            RestoredConfigured && (restoredConfiguredPhysics.Flags & PhysicsVacuum) != 0;
        private static bool vacuumFixtureQueued, vacuumRestVerified, vacuumVentSubmitted, vacuumFrontObserved;
        private static ulong vacuumRestFrames, vacuumVentFrame;
        private static bool vacuumFrontCaptured;
        private static int vacuumFrontRenderFrame = -1;
        private static Cell[] vacuumRestState = Array.Empty<Cell>();
        private static SubcellStatus subcellStatus;
        private static double[] occupiedVolumes = Array.Empty<double>();
        private static float CompatibilityMassAt(int index) => ConfiguredVacuumMode ? index / Width == 0 ? 500 : 0 : ProjectionMass(index % Width);
        private static float CompatibilityTemperatureAt(int index) => ConfiguredVacuumMode ? index / Width == 0 ? 300 : 0 : ProjectionTemperature(index % Width);
        private static SimHashes CompatibilityElementAt(int index) => ConfiguredVacuumMode ? index / Width == 0 ? SimHashes.Water : SimHashes.Vacuum : Liquid ? SimHashes.Water : SimHashes.Oxygen;
        private static SimHashes PublishedElementAt(int index) => !ConfiguredVacuumMode ? Liquid ? SimHashes.Water : SimHashes.Oxygen :
            cells[index].TotalMass == 0 ? SimHashes.Vacuum : cells[index].Water > 0 &&
            cells[index].Water >= cells[index].Oxygen + cells[index].Nitrogen + cells[index].Helium + cells[index].CarbonDioxide ? SimHashes.Water : SimHashes.Oxygen;
        private static void ResetVacuumFixture()
        {
            vacuumFixtureQueued = vacuumRestVerified = vacuumVentSubmitted = vacuumFrontObserved = vacuumFrontCaptured = false;
            vacuumRestFrames = vacuumVentFrame = 0; vacuumRestState = Array.Empty<Cell>();
            subcellStatus = default; vacuumFrontRenderFrame = -1; occupiedVolumes = Array.Empty<double>();
        }
        private static void QueueVacuumFixture()
        {
            vacuumFixtureQueued = true;
            foreach (var index in ConfiguredRoom.FluidIndices)
                SimMessages.ReplaceElement(Grid.XYToCell(x + index % Width, y + index / Width), CompatibilityElementAt(index),
                    CellEventLogger.Instance.SandBoxTool, CompatibilityMassAt(index), CompatibilityTemperatureAt(index));
        }
        private static void TickVacuumFixture()
        {
            if (!ConfiguredVacuumMode || RestoredConfigured || !projectionPrimed) return;
            if (vacuumRestState.Length == 0) vacuumRestState = (Cell[])cells.Clone();
            if (!vacuumVentSubmitted)
            {
                Require(ConfiguredRoom.FluidIndices.All(index => cells[index].Equals(vacuumRestState[index])),
                    "Water under vacuum moved during the resting control.");
                vacuumRestFrames = latest.CompletedFrames - projectionPrimedAtFrame;
                if (vacuumRestFrames < 10) return;
                vacuumRestVerified = true;
                ColonyRuntimeValidation.CaptureScreenshot("vacuum-rest.png");
                vacuumVentFrame = latest.CompletedFrames;
                SimMessages.EmitMass(Grid.XYToCell(x, y + 2), (ushort)ElementLoader.FindElementByHash(SimHashes.Oxygen).idx,
                    .03125F, 300, 255, 0, -1);
                vacuumVentSubmitted = true;
            }
            if (vacuumVentSubmitted && !vacuumFrontCaptured && renderedRevision > vacuumVentFrame)
            {
                if (vacuumFrontRenderFrame < 0) vacuumFrontRenderFrame = UnityEngine.Time.frameCount;
                if (UnityEngine.Time.frameCount - vacuumFrontRenderFrame >= 3)
                { vacuumFrontCaptured = true; ColonyRuntimeValidation.CaptureScreenshot("vacuum-front.png"); }
            }
            vacuumFrontObserved |= ConfiguredRoom.FluidIndices.Any(index => index / Width > 0 &&
                index != 2 * Width && cells[index].Oxygen > 0);
        }
        private static void VerifyVacuumCompletion()
        {
            var room = ConfiguredRoom;
            configuredCopiesVerified = room.CopyMatchesMask(cells, true) && room.FluidIndices.All(index =>
                publishedReadings!.ContainsKey(Grid.XYToCell(x + index % Width, y + index / Width)));
            configuredSolidEvidence = room.SolidIndices.Select(SolidEvidence).ToArray();
            configuredSolidsVerified = room.SolidIndices.All(index =>
                InsulatingStaticSolid(Grid.XYToCell(x + index % Width, y + index / Width)) &&
                !OwnsPublicCell(Grid.XYToCell(x + index % Width, y + index / Width)));
            configuredThermalVerified = thermalStatus.Enabled == 0 && thermalStatus.Stages == 0;
            configuredGravityVerified = gravityStatus.Enabled == 1 && gravityStatus.Gravity == 9.81;
            configuredMotionObserved = room.FluidIndices.Any(index => cells[index].ResolvedKineticEnergy > 0);
            Require(configuredCopiesVerified && configuredSolidsVerified && configuredThermalVerified && configuredGravityVerified &&
                configuredMotionObserved && viscosityStatus.Enabled == 0, "Vacuum scene publication, solids or disabled physics differs.");
            Require(RestoredConfigured || vacuumRestVerified && vacuumFrontObserved && vacuumVentSubmitted,
                "Vacuum scene did not exercise rest and an actual game vent packet.");
            Require(RestoredConfigured || (ConfiguredPhysics.Flags & PhysicsUnresolvedGas) == 0 ||
                subcellStatus.Policy == 1 && subcellStatus.Events > 0 && subcellStatus.EntropyIncrease > 0,
                "The unresolved gas opt-in did not expose its accepted entropy account.");
        }
        private static object? VacuumReport() => !ConfiguredVacuumMode ? null : new
        {
            Required = true, Restored = RestoredConfigured, PhaseChangeEnabled = false, RestFrames = vacuumRestFrames,
            WaterRestVerified = vacuumRestVerified, VentSubmitted = vacuumVentSubmitted, FrontObserved = vacuumFrontObserved,
            VentMassKg = .03125, RestState = vacuumRestState, Cells = cells,
            EmptyCells = ConfiguredRoom.FluidIndices.Count(index => cells[index].TotalMass == 0),
            PartialLiquidCells = ConfiguredRoom.FluidIndices.Count(index => cells[index].Water > 0 && cells[index].LiquidFraction > 0 && cells[index].LiquidFraction < 1),
            SubcellAccountsSinceActivation = subcellStatus,
            Source = "completed-native-void-phase-publication"
        };
    }
}
