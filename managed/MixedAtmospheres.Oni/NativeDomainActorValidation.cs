using System;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static GameObject? actorConsumerObject;
        private static ElementConsumer? actorConsumer;
        private static int actorFixtureStage, fixtureEmitter = -1, fixtureRegistrations, fixtureBlockedCallbacks, fixtureUnblockedCallbacks;
        private static int fixtureConsumptionCallbacks;
        private static ulong actorStageFrame, actorRemovalFrame, removalConsumerSamples, removalEmitterSamples;
        private static double actorBeforeEnergy, actualConsumerMass, actualEmitterMass, actorEnergyResidual, actorMassResidual;
        private static double[] actorBeforeMasses = Array.Empty<double>();
        private static bool actorFixtureVerified, emitterGameStatusObserved;
        private static float consumerAccumulatedMass;
        private static HandleVector<Game.CallbackInfo>.Handle fixtureBlockedHandle, fixtureUnblockedHandle;
        private static Game? actorFixtureGame;

        private static void ConfigureFixtureEmitter(float maximumDensity, float mass)
        {
            SimMessages.ModifyElementEmitter(fixtureEmitter, target + 1, 0, SimHashes.Oxygen, .2f, mass, 300,
                maximumDensity, 255, 0);
        }
        private static void TickActorFixture()
        {
            if (actorFixtureStage == 0)
            {
                actorBeforeEnergy = cells.Sum(cell => cell.InternalEnergy + cell.ResolvedKineticEnergy);
                actorBeforeMasses = Enumerable.Range(0, Ids.Length).Select(k => cells.Sum(cell => cell.Mass(k))).ToArray();
                actorFixtureGame = Game.Instance;
                fixtureBlockedHandle = actorFixtureGame.callbackManager.Add(new Game.CallbackInfo(() => ++fixtureBlockedCallbacks, manually_release: true));
                fixtureUnblockedHandle = actorFixtureGame.callbackManager.Add(new Game.CallbackInfo(() => ++fixtureUnblockedCallbacks, manually_release: true));
                var registration = actorFixtureGame.simComponentCallbackManager.Add((handle, _) =>
                { fixtureEmitter = handle; ++fixtureRegistrations; }, new object(), "MA registered gas boundary fixture");
                SimMessages.AddElementEmitter(10000, registration.index, fixtureBlockedHandle.index, fixtureUnblockedHandle.index);
                actorConsumerObject = new GameObject("MA private real ElementConsumer");
                actorConsumerObject.SetActive(false);
                actorConsumerObject.transform.position = Grid.CellToPosCCC(target, Grid.SceneLayer.Ore);
                actorConsumer = actorConsumerObject.AddComponent<ElementConsumer>();
                actorConsumer.elementToConsume = SimHashes.Oxygen; actorConsumer.configuration = ElementConsumer.Configuration.Element;
                actorConsumer.consumptionRadius = 0; actorConsumer.consumptionRate = .05f;
                actorConsumer.showInStatusPanel = false; actorConsumer.showDescriptor = false; actorConsumer.ignoreActiveChanged = true;
                actorConsumer.storeOnConsume = false;
                actorConsumer.OnElementConsumed += sample =>
                {
                    Require(sample.mass > 0 && sample.removedElemIdx == ElementLoader.FindElementByHash(SimHashes.Oxygen).idx &&
                        sample.temperature > 0 && sample.diseaseIdx == 255 && sample.diseaseCount == 0,
                        "Real ElementConsumer received an incorrect custom physical sample.");
                    ++fixtureConsumptionCallbacks; actualConsumerMass += sample.mass;
                };
                actorConsumer.EnableConsumption(true); actorConsumerObject.SetActive(true);
                actorConsumer.InitializeComponent(); actorConsumer.Spawn();
                Require(actorConsumer.isSpawned, "The real ElementConsumer did not spawn.");
                actorStageFrame = latest.CompletedFrames; actorFixtureStage = 1; return;
            }
            if (actorFixtureStage == 1)
            {
                var handle = (int)AccessTools.Field(typeof(SimComponent), "simHandle").GetValue(actorConsumer);
                Require(latest.CompletedFrames - actorStageFrame <= 15, "Native actor registration did not reach the real components.");
                if (fixtureEmitter == -1 || !actorBindings.TryGetValue((1U, handle), out var binding) || !binding.Configured) return;
                ConfigureFixtureEmitter(10000, .03125f); actorStageFrame = latest.CompletedFrames; actorFixtureStage = 2; return;
            }
            if (fixtureEmitter != -1 && currentSnapshot != null && actorFixtureStage < 6)
            {
                var index = fixtureEmitter & 0x00ffffff;
                if (index < currentSnapshot.Pointer->numEmittedMassEntries && Game.Instance.simData.emittedMassEntries == currentSnapshot.Pointer->emittedMassEntries)
                {
                    var slot = Game.Instance.simData.emittedMassEntries[index];
                    emitterGameStatusObserved |= slot.mass > 0 && slot.elemIdx == ElementLoader.FindElementByHash(SimHashes.Oxygen).idx && slot.temperature > 0;
                }
            }
            if (latest.CompletedFrames - actorStageFrame < 8) return;
            if (actorFixtureStage == 2)
            {
                actorConsumer!.consumptionRate = .025f; actorConsumer.RefreshConsumptionRate();
                ConfigureFixtureEmitter(10000, .0625f);
            }
            else if (actorFixtureStage == 3) ConfigureFixtureEmitter(.1f, .0625f);
            else if (actorFixtureStage == 4) ConfigureFixtureEmitter(10000, .0625f);
            else if (actorFixtureStage == 5)
            {
                Require(fixtureBlockedCallbacks == 1 && fixtureUnblockedCallbacks == 1,
                    "Actual game blocked/unblocked callbacks were missing or duplicated.");
                actorConsumer!.EnableConsumption(false);
                ConfigureFixtureEmitter(10000, 0);
            }
            else if (actorFixtureStage == 6)
            {
                consumerAccumulatedMass = actorConsumer!.consumedMass;
                UnityEngine.Object.Destroy(actorConsumerObject);
                SimMessages.RemoveElementEmitter(-1, fixtureEmitter);
                actorRemovalFrame = latest.CompletedFrames;
            }
            else if (actorFixtureStage == 7)
            {
                if (actorStatus.LiveActors != 0) return;
                removalConsumerSamples = actorStatus.ConsumerSamples; removalEmitterSamples = actorStatus.EmitterSamples;
            }
            else return;
            actorStageFrame = latest.CompletedFrames; ++actorFixtureStage;
        }
        private static void VerifyActorFixture()
        {
            actorEnergyResidual = cells.Sum(cell => cell.InternalEnergy + cell.ResolvedKineticEnergy) - actorBeforeEnergy -
                transferStatus.MaterialEnergyChange - transferStatus.KineticEnergyChange;
            actorMassResidual = 0;
            for (var k = 0; k < Ids.Length; ++k)
            {
                var current = cells.Sum(cell => cell.Mass(k)); var change = transferStatus.MassChangesKg[k];
                var scale = Math.Abs(actorBeforeMasses[k]) + Math.Abs(change);
                if (scale == 0) Require(current == 0, "An actor created an unaccounted constituent.");
                else actorMassResidual = Math.Max(actorMassResidual, Math.Abs(current - actorBeforeMasses[k] - change) / scale);
            }
            actorFixtureVerified = actorFixtureStage == 8 && actorConsumerObject == null && fixtureRegistrations == 1 &&
                fixtureConsumptionCallbacks > 10 && fixtureBlockedCallbacks == 1 && fixtureUnblockedCallbacks == 1 &&
                emitterGameStatusObserved && actorStatus.LiveActors == 0 && latest.CompletedFrames - actorRemovalFrame >= 8 &&
                actorStatus.ConsumerSamples == removalConsumerSamples && actorStatus.EmitterSamples == removalEmitterSamples &&
                actorStatus.ConsumerSamples == (ulong)fixtureConsumptionCallbacks && actorConsumerPublications == fixtureConsumptionCallbacks &&
                actorEmitterPublications == (long)actorStatus.EmitterSamples && actorBlockedPublications == 1 && actorUnblockedPublications == 1 &&
                Math.Abs(actualEmitterMass - actualConsumerMass - transferStatus.MassChangesKg[1]) <= 1e-12 &&
                Math.Abs(consumerAccumulatedMass - actualConsumerMass) <= 2e-6 &&
                transferStatus.UnacknowledgedReceipts == 0 && transferStatus.KineticEnergyChange < 0 &&
                Math.Abs(actorEnergyResidual) <= 1e-6 && actorMassResidual <= 1e-10;
            Require(actorFixtureVerified, "Registered actors, actual ElementConsumer, game callbacks, removal or physical accounts failed.");
            actorFixtureGame!.ManualReleaseHandle(fixtureBlockedHandle); actorFixtureGame.ManualReleaseHandle(fixtureUnblockedHandle);
            actorFixtureGame = null;
        }
        private static object ActorReport() => new
        {
            Verified = actorFixtureVerified, Stage = actorFixtureStage, Native = actorStatus,
            MessagesRouted = actorMessagesRouted, ConsumerPublications = actorConsumerPublications, EmitterPublications = actorEmitterPublications,
            BlockedPublications = actorBlockedPublications, UnblockedPublications = actorUnblockedPublications,
            ActualConsumerCallbacks = fixtureConsumptionCallbacks, ActualEmitterRegistrations = fixtureRegistrations,
            ActualBlockedCallbacks = fixtureBlockedCallbacks, ActualUnblockedCallbacks = fixtureUnblockedCallbacks,
            ActualConsumerMass = actualConsumerMass, ActualEmitterMass = actualEmitterMass, ConsumerAccumulatedMass = consumerAccumulatedMass,
            EmitterGameStatusObserved = emitterGameStatusObserved, EnergyResidualJoules = actorEnergyResidual, MaximumRelativeMassError = actorMassResidual,
            RemovedAtFrame = actorRemovalFrame, RemovalConsumerSamples = removalConsumerSamples, RemovalEmitterSamples = removalEmitterSamples
        };
    }
}
