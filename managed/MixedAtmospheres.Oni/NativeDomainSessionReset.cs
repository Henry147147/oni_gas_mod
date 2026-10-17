using System;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        // Called only after Sim.Shutdown has ended the original worker and
        // Shutdown has retired snapshots, tokens, bindings and callbacks.
        // The reload workflow is process-scoped evidence and remains separate.
        private static void ResetEndedNativeSession()
        {
            prepared=arming=completed=failed=active=false;
            x=y=target=hotSolid=coldSolid=preparedFrame=publications=0;
            started=nextReport=initialHot=initialCold=0;rendererWaitFrame=-1;textureWriteBaseline=0;
            nativeBaseline.Clear();currentSnapshot=null;catalog=null;latest=default;cells=new Cell[Width*Height];error="";renderedRevision=0;
            retainedFrameVerified=nativeSolidsAdvanced=nativeSnapshotVerified=false;nativeCellsUnchanged=true;
            nativeFlowControlObserved=projectionPrimed=writingProjection=nativeFlowFenceVerified=false;
            nativeRendererAdvanced=saveGuardVerified=nativeSaveGuardVerified=gameGridProjectionVerified=false;
            heatStatus=default;thermalStatus=default;gravityStatus=default;gravityVerified=thermalVerified=false;
            heatSubmitted=heatVerified=heatAvoidedNativeDispatch=false;
            heatBeforeEnergy=heatEnergyResidual=heatEnergyScale=0;heatEnergyBound=1e-6;heatPacketsRouted=0;
            transferStatus=default;completedReceipts=Array.Empty<Receipt>();deliveredReceipt=0;transferPacketsRouted=receiptsPublished=0;
            transfersSubmitted=transfersVerified=transfersAvoidedNativeDispatch=transferConsumptionSubmitted=false;
            transferConsumptionCallback=0;emissionCallbacks=consumptionCallbacks=0;emissionResult=default;consumptionResult=default;
            transfersBeforeEnergy=transferEnergyResidual=transferMassError=transferEnergyScale=0;transferEnergyBound=1e-6;
            liquidInjectionReceipt=liquidExtractionReceipt=null;transfersBeforeMasses=Array.Empty<double>();releaseTransferCallbacks=null;
            projectionPrimedAtFrame=0;nativeProjectionMasses=Array.Empty<float>();nativeProjectionTemperatures=Array.Empty<float>();publishedReadings=null;

            checkpointSource=checkpointExpected=checkpointActual=null;checkpointSourceFrame=checkpointResumedFrame=0;
            checkpointSourceTime=checkpointResumedTime=0;checkpointAttempts=0;checkpointVerified=false;
            pairedStatus=default;pairedToken=pairedRequestedFrame=0;
            pairedVerified=pairedNativeCellsVerified=pairedImmutableVerified=pairedReleaseVerified=false;pairedEncodedBytes=0;
            pairedNativeHash=pairedPhysicalHash=pairedEnvelopeHash="";

            stagedColonySave=null;pendingColonyRestore=null;restoredExpected=null;replayFrame=savedSourceFrame=restoredSubmittedFrames=0;
            savedSourceTime=replayTime=loadedSourceTime=0;loadedSourceFrame=0;loadedSourceHash="";
            frameZeroResaveVerified=frameZeroResaveScheduled=initialPauseChallengeVerified=false;
            initialInventory=Array.Empty<InitialCell>();initialInventoryElements=Array.Empty<ushort>();
            initialInventoryVerified=false;initialInventoryHash="";initialInventoryNativeFrames=0;
            initialPauseTicksHeld=initialPositiveTicksHeld=0;resumePreparationFrame=-1;
            savedPhysicalHash=restoredInitialHash=savedFileHash=savedFilePath="";
            colonyFileSaved=colonyLoaded=zeroRestoreVerified=restoredSceneStarted=restoredRendererVerified=false;
            stagedSaveWrites=exactReplayComparisons=restoredNativeBatches=0;
            restoredScenarioSelected=false;restoredScenario=0;domainManagedThread=0;
            automaticSaveAdmissions=serializedBodyCaptures=serializedBodyBytes=serializedHeaderCaptures=0;
            savedAsAutoSave=savedFileReadbackVerified=savePauseStateRestored=rejectedSaveVerified=false;
            serializedHeaderJson=Array.Empty<byte>();serializedBodyHash="";savePauseDepthBefore=savePauseDepthAfter=0;

            saveProblemDialog=null;declinedColonySaves=unsuccessfulColonyWrites=0;lastSaveProblem="";ioSaveControlVerified=false;
            ioSaveControlFrame=0;ioSaveControlStage=ioSaveInitialPauseDepth=ioSavePriorAdmissions=ioSavePriorProblems=ioSavePriorDeclines=ioSaveDialogFrame=0;
            ioSaveSourceTime=0;ioSaveSourceImage=null;ioSavePriorSelection=ioSaveBlockedPath=ioSaveBeforeHash=ioSaveAfterHash="";
            ioSaveNativeDialogObserved=ioSaveDraggingRestored=false;ioSaveNativeDialogText="";
            ioSaveGame=null;ioSavePreviousPre=null;ioSavePreviousActive=null;ioSavePreviousPost=null;
            ioSavePreCallbacks=ioSaveActiveCallbacks=ioSavePostCallbacks=0;

            actorMessagesRouted=actorConsumerPublications=actorEmitterPublications=actorBlockedPublications=actorUnblockedPublications=actorBatchesRouted=0;
            actorRegistrationBatches=actorRetirementBatches=0;
            batchFixtureEmitter=-1;batchFixtureRegistrations=0;
            actorRegistrationBatchVerified=actorRetirementBatchVerified=false;
            initialZeroEmitterVerified=false;batchActualEmitterMass=initialZeroEmitterMass=0;
            batchEmitterSamples=initialZeroEmitterSamples=0;
            actorStatus=default;emissionStatusFrame=0;
            actorConsumerObject=outsideActorObject=null;actorConsumer=outsideActorConsumer=null;
            actorConfigurationBatchVerified=nativeActorBatchVerified=false;actorFixtureStage=0;fixtureEmitter=-1;
            fixtureRegistrations=fixtureBlockedCallbacks=fixtureUnblockedCallbacks=fixtureConsumptionCallbacks=0;
            actorStageFrame=actorRemovalFrame=removalConsumerSamples=removalEmitterSamples=0;
            actorBeforeEnergy=actualConsumerMass=actualEmitterMass=actorEnergyResidual=actorMassResidual=actorEnergyScale=0;actorEnergyBound=1e-6;
            actorBeforeMasses=Array.Empty<double>();actorFixtureVerified=emitterGameStatusObserved=false;consumerAccumulatedMass=0;
            fixtureBlockedHandle=fixtureUnblockedHandle=default;actorFixtureGame=null;
        }
    }
}
