using System;
using System.Linq;
using System.Threading.Tasks;
using MixedAtmospheres.Core;

internal static class PreparedPresentationPublicationTests
{
    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<ColonyFluidState> fixture, Func<MixturePresentationModel> model, FluidMaterialCatalog catalog)
    {
        test("prepared presentation binds the exact immutable source model and owned container view before commit", () =>
        {
            var state = fixture(); var prior = state.CapturePublication(); var presentationModel = model();
            using var transaction = state.BeginTransaction();
            var handle = transaction.CreateContainer("buffer", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas));
            transaction.TransferCellToContainer(5, handle, .25);
            using var candidate = transaction.PrepareFrame(7, 1.4);
            var publication = MixturePresentationPublication.Prepare(candidate.Snapshot, presentationModel);
            check(ReferenceEquals(publication.Source, candidate.Snapshot) && ReferenceEquals(publication.Model, presentationModel));
            var frame = publication.Frame;
            check(frame.ColonyId == candidate.Snapshot.ColonyId && frame.Revision == candidate.Snapshot.Revision && frame.CompletedFrame == 7 && frame.SimulationTimeSeconds == 1.4);
            check(frame.MaterialModelVersion == catalog.Version && frame.MaterialModelFingerprint == catalog.Fingerprint);
            check(frame.SampleContainer(handle).TotalMassKg == .25 && frame.SampleCell(5).TotalMassKg == 1.75 && ReferenceEquals(state.CapturePublication(), prior));
            candidate.Commit(publication);
            check(ReferenceEquals(state.CapturePublication().Companion, publication) && ReferenceEquals(state.CaptureSnapshot(), publication.Source));
        });
        test("failed prepared presentation leaves authority unchanged and permits capture of the same candidate", () =>
        {
            var state = fixture(); var prior = state.CapturePublication(); var presentationModel = model();
            var wrongCatalog = new FluidMaterialCatalog("wrong-prepared-model", catalog.Materials.Values);
            var wrongModel = new MixturePresentationModel(wrongCatalog, presentationModel.Styles.Values, presentationModel.BreathingSettings,
                presentationModel.OxygenMaterialId, presentationModel.CarbonDioxideMaterialId);
            using var transaction = state.BeginTransaction(); transaction.Transfer(5, 6, .25);
            using var candidate = transaction.PrepareFrame(7, 1.4);
            reject(() => MixturePresentationPublication.Prepare(candidate.Snapshot, wrongModel));
            reject(() => MixturePresentationPublication.Prepare((ColonyFrameSnapshot)null!, presentationModel));
            reject(() => MixturePresentationPublication.Prepare((CoupledColonyFrame)null!, presentationModel));
            reject(() => MixturePresentationPublication.Prepare(candidate.Snapshot, null!));
            check(ReferenceEquals(state.CapturePublication(), prior));
            var publication = MixturePresentationPublication.Prepare(candidate.Snapshot, presentationModel);
            check(ReferenceEquals(publication.Source, candidate.Snapshot) && publication.Frame.SampleCell(6).TotalMassKg == .25);
        });
        test("prepared publication remains coherent for worker reads after authority and presentation options change", () =>
        {
            var state = fixture(); var presentationModel = model();
            var publication = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), presentationModel);
            using (var transaction = state.BeginTransaction()) { transaction.Transfer(5, 6, .25); transaction.CommitFrame(7, 1.4); }
            var changedModel = presentationModel.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false, PatternContrast = 0 });
            var next = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), changedModel);
            Parallel.For(0, 32, _ => check(publication.Frame.Revision == publication.Source.Revision &&
                publication.Frame.SampleCell(5).TotalMassKg == 2 && next.Frame.SampleCell(5).TotalMassKg == 1.75));
            check(ReferenceEquals(publication.Model, presentationModel) && ReferenceEquals(next.Model, changedModel));
            check(!ReferenceEquals(publication.Source, next.Source) && publication.Source.ColonyId == next.Source.ColonyId);
        });
    }
}
