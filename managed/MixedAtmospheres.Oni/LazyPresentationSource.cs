using System.Collections.Generic;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    // Readings whose cells are built on demand (ColonyLazyReadings): the
    // presentation runtime publishes them without visiting every cell.
    internal interface ILazyReadings : IReadOnlyDictionary<int, MixtureThermodynamicReading>
    {
        // The matching presentations, described per cell on first use.
        IReadOnlyDictionary<int, MixturePresentation> Describe(MixturePresentationModel model);
    }
}
