using System.Runtime.InteropServices;
using MixedAtmospheres.Oni;

internal static unsafe partial class Program
{
    private static void VerifyLiquidPointTransfers(NativeSim sim, Contract contract, Dictionary<string, object?> report)
    {
        var cells=new OniNativeDomain.Cell[6]; var status=OniNativeDomain.Status.Empty;
        Require(OniNativeDomain.Copy(ref status,cells,6)==0 && status.State==1,"Liquid source did not copy.");
        var before=(OniNativeDomain.Cell[])cells.Clone();
        var account=OniNativeDomain.TransferStatus.Empty;
        var injection=new OniNativeDomain.TransferCommand { Size=64,Version=2,Session=status.Session,ExpectedFrame=status.CompletedFrames,
            PublicCell=(uint)DomainCell(0),Species=3,Kind=1,Flags=1,Callback=71,MassKg=.05f,Temperature=300 };
        Require(OniNativeDomain.QueueTransfers([injection],1,ref account)==0 && account.PendingBatches==1 && account.CommittedBatches==0,
            "Liquid injection did not reserve one unpublished command.");
        var held=account; var removal=injection; removal.Kind=2; removal.Callback=72; removal.MassKg=.02f; removal.Temperature=0;
        var voidRemoval=removal; voidRemoval.MassKg=200;
        Require(OniNativeDomain.QueueTransfers([removal,voidRemoval],2,ref account)!=0 && account.AcceptedBatches==held.AcceptedBatches &&
            account.PendingBatches==held.PendingBatches,
            "A later void-creating request changed pending commands or published state.");
        var receipts=new OniNativeDomain.Receipt[8];
        Require(OniNativeDomain.CopyReceipts(ref status,cells,6,receipts,8,out var count)==0 && count==0,"Pending liquid receipt was published early.");
        Require(cells.Select(c=>c.Water).SequenceEqual(before.Select(c=>c.Water)) && cells.Select(c=>c.InternalEnergy).SequenceEqual(before.Select(c=>c.InternalEnergy)),
            "Pending liquid command changed the completed physical publication.");
        sim.Send(-775326397,Frame(.2f)); sim.PrepareAfterFrame(contract);
        Require(OniNativeDomain.CopyReceipts(ref status,cells,6,receipts,8,out count)==0 && status.State==1 && count==1,
            "Native liquid injection frame failed: "+status.Error);
        var injected=receipts[0];
        Require(OniNativeDomain.Definition(3,out var species)==0,"Liquid caloric identity did not copy.");
        var q=before[0].Pressure/2.2e9;
        var elastic=2.2e9/1000*(Math.Log(1+q)+1/(1+q)-1);
        var expectedU=(double).05f*(species.Offset+species.Cv*300+elastic);
        Require(injected.Species==3 && injected.Kind==1 && injected.Callback==71 && injected.MassKg==(double).05f &&
            injected.KineticEnergy==0 && Math.Abs(injected.InternalEnergy-expectedU)<=128*2.2204460492503131e-16*Math.Abs(expectedU),
            "Injected native liquid parcel omitted its declared compression energy.");
        Require(OniNativeDomain.AckReceipts(status.Session,injected.Sequence)==0,"Liquid injection receipt was not acknowledged.");
        var localSpecific=cells[0].InternalEnergy/cells[0].Water;
        removal.ExpectedFrame=status.CompletedFrames;
        Require(OniNativeDomain.QueueTransfers([removal],1,ref account)==0,"Liquid extraction did not reserve.");
        sim.Send(-775326397,Frame(.2f)); sim.PrepareAfterFrame(contract);
        Require(OniNativeDomain.CopyReceipts(ref status,cells,6,receipts,8,out count)==0 && status.State==1 && count==1,
            "Native liquid extraction frame failed: "+status.Error);
        var extracted=receipts[0];
        Require(extracted.Species==3 && extracted.Kind==2 && extracted.Callback==72 && extracted.MassKg==(double).02f &&
            extracted.KineticEnergy>0 && (extracted.MomentumX!=0 || extracted.MomentumY!=0) &&
            Math.Abs(extracted.InternalEnergy-(double).02f*localSpecific)<=128*2.2204460492503131e-16*Math.Abs(extracted.InternalEnergy),
            "Liquid extraction lost local elastic U or carried face mechanics.");
        Require(OniNativeDomain.CopyTransferStatus(ref account)==0 && account.AcceptedBatches==2 && account.CommittedBatches==2 &&
            account.PendingBatches==0 && account.MassChangesKg[3]==(double).05f-.02f,"Native liquid transfer account differs.");
        var energyResidual=cells.Sum(c=>c.InternalEnergy+c.ResolvedKineticEnergy)-before.Sum(c=>c.InternalEnergy+c.ResolvedKineticEnergy)-
            account.MaterialEnergyChange-account.KineticEnergyChange;
        var energyBound=128*2.2204460492503131e-16*(cells.Sum(c=>Math.Abs(c.InternalEnergy))+before.Sum(c=>Math.Abs(c.InternalEnergy)));
        Require(Math.Abs(cells.Sum(c=>c.Water)-before.Sum(c=>c.Water)-account.MassChangesKg[3])<1e-9 &&
            Math.Abs(energyResidual)<=energyBound,"Native liquid ledger disagrees with the complete physical state.");
        Require(OniNativeDomain.AckReceipts(status.Session,extracted.Sequence)==0 && OniNativeDomain.AckReceipts(status.Session,extracted.Sequence)==0,
            "Liquid extraction acknowledgment was not idempotent.");
        var exactFrames=0;
        for (var i=0;i<12;++i) {
            var image=ReadPhysicalCheckpoint(status.Session,status.CompletedFrames);
            var expected=ResumePhysicalCheckpoint(image,(double).2f);
            sim.Send(-775326397,Frame(.2f)); sim.PrepareAfterFrame(contract);
            Require(OniNativeDomain.CopyReceipts(ref status,cells,6,receipts,8,out count)==0 && status.State==1 && count==0 &&
                ReadPhysicalCheckpoint(status.Session,status.CompletedFrames).SequenceEqual(expected),
                "Liquid transfer checkpoint did not resume every physical field exactly, or redelivered a receipt.");
            ++exactFrames;
        }
        report["liquidPointTransfersPassed"]=true;
        report["liquidTransferElasticSpecificEnergyJPerKg"]=elastic;
        report["liquidTransferEnergyResidualJoules"]=energyResidual;
        report["liquidTransferEnergyRoundoffBoundJoules"]=energyBound;
        report["liquidTransferExactContinuationFrames"]=exactFrames;
        report["liquidTransferReceiptCount"]=2;
    }
}
