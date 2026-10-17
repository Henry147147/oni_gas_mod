using System;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool RadialConsumptionMode => ColonyRuntimeValidation.Settings?.Mode == "native-radius-domain";
        private static float FixtureConsumptionKg => RadialConsumptionMode || RectangularConsumptionMode ? .4f : .02f;
        private static double radialSourceCellOxygen;
        private static ulong radialSourceFrame;
        private static Receipt? radialReceipt;
        private static int radialReceipts;
        private static bool RadialConsumptionVerified => RadialConsumptionMode && transfersVerified && radialConsumptionMessagesRouted==1 &&
            radialReceipts==1 && radialReceipt.HasValue && radialReceipt.Value.Kind==2 && radialReceipt.Value.Species==1 &&
            radialReceipt.Value.PublicCell==target && radialReceipt.Value.Callback==transferConsumptionCallback &&
            radialReceipt.Value.MassKg==(double)FixtureConsumptionKg && radialReceipt.Value.CompletedFrame>radialSourceFrame &&
            radialReceipt.Value.MassKg>radialSourceCellOxygen+(double).05f;
        private static void ObserveRadialConsumptionReceipt(Receipt receipt)
        {
            if(!RadialConsumptionMode || receipt.Kind!=2 || receipt.Callback!=transferConsumptionCallback) return;
            Require(!radialReceipt.HasValue,"Radius consumption produced more than one native game receipt.");
            radialReceipt=receipt;++radialReceipts;
        }
        private static object RadialConsumptionReport() => new {
            Required=RadialConsumptionMode,Verified=RadialConsumptionVerified,Source="actual-SimMessages.ConsumeMass-radius-3",
            Radius=3,PublicCell=target,SourceFrame=radialSourceFrame,SourceCellOxygenKg=radialSourceCellOxygen,
            InjectedOxygenKg=(double).05f,RequestedMassKg=(double)FixtureConsumptionKg,
            Receipts=radialReceipts,Receipt=radialReceipt,SingleCallbackForMultipleSourceCells=RadialConsumptionVerified,
            ActualDuplicantMetabolismVerified=false
        };
    }
}
