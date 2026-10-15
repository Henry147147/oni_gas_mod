using System;

namespace MixedAtmospheres.Core
{
    /// <summary>
    /// An immutable completed source/model/view prepared before publication.
    /// Construction performs all fallible frame capture before this object can
    /// escape; publishing it requires no second independently sampled provider.
    /// </summary>
    public sealed class MixturePresentationPublication
    {
        public ColonyFrameSnapshot Source { get; }
        public MixturePresentationModel Model { get; }
        public MixturePresentationFrame Frame { get; }

        private MixturePresentationPublication(ColonyFrameSnapshot source, MixturePresentationModel model)
        {
            Source = source ?? throw new ArgumentNullException(nameof(source));
            Model = model ?? throw new ArgumentNullException(nameof(model));
            Frame = model.CaptureFrame(source);
        }

        public static MixturePresentationPublication Prepare(ColonyFrameSnapshot source, MixturePresentationModel model)
            => new MixturePresentationPublication(source, model);
    }
}
