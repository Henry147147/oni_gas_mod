using System;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool RectangularConsumptionMode => ColonyRuntimeValidation.Settings?.Mode == "native-rectangular-domain";
        private static long rectangularConsumptionMessagesRouted;
        private static double rectangularSourceCellOxygen;
        private static ulong rectangularSourceFrame;
        private static Receipt? rectangularReceipt;
        private static int rectangularReceipts;
        private static bool RectangularConsumptionVerified => RectangularConsumptionMode && transfersVerified &&
            rectangularConsumptionMessagesRouted==1 && rectangularReceipts==1 && rectangularReceipt.HasValue &&
            rectangularReceipt.Value.Kind==2 && rectangularReceipt.Value.Species==1 && rectangularReceipt.Value.PublicCell==target &&
            rectangularReceipt.Value.Callback==transferConsumptionCallback && rectangularReceipt.Value.MassKg==(double)FixtureConsumptionKg &&
            rectangularReceipt.Value.CompletedFrame>rectangularSourceFrame &&
            rectangularReceipt.Value.MassKg>rectangularSourceCellOxygen+(double).05f;
        private static void ObserveRectangularConsumptionReceipt(Receipt receipt)
        {
            if(!RectangularConsumptionMode || receipt.Kind!=2 || receipt.Callback!=transferConsumptionCallback) return;
            Require(!rectangularReceipt.HasValue,"Rectangular consumption produced more than one native game receipt.");
            rectangularReceipt=receipt;++rectangularReceipts;
        }
        private static object RectangularConsumptionReport() => new {
            Required=RectangularConsumptionMode,Verified=RectangularConsumptionVerified,AbiVersion=1,
            Source="actual-SimMessages.ConsumeMass-width-height",Order="bottom-to-top-left-to-right",
            Width,Height,PublicCell=target,SourceFrame=rectangularSourceFrame,SourceCellOxygenKg=rectangularSourceCellOxygen,
            InjectedOxygenKg=(double).05f,RequestedMassKg=(double)FixtureConsumptionKg,
            MessagesRouted=rectangularConsumptionMessagesRouted,Receipts=rectangularReceipts,Receipt=rectangularReceipt,
            SingleCallbackForMultipleSourceCells=RectangularConsumptionVerified,WholeColonyDeviceConsumption=false
        };
        private static void ResetRectangularConsumption()
        {
            rectangularConsumptionMessagesRouted=0;rectangularSourceCellOxygen=0;rectangularSourceFrame=0;
            rectangularReceipt=null;rectangularReceipts=0;
        }
    }
}
