using System.Collections.Generic;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    // Readings whose cells are built on demand (ColonyLazyReadings): the
    // presentation runtime publishes them without visiting every cell.
    internal interface ICompactTextureSource
    {
        bool PrepareTextureScalars(int width,int x0,int y0,int x1,int y1,out int count);
        bool TrySampleTexture(int key,double x,double y,out MixtureTextureScalars value);
    }
    internal interface ILazyReadings : IReadOnlyDictionary<int, MixtureThermodynamicReading>
    {
        // The matching presentations, described per cell on first use.
        IReadOnlyDictionary<int, MixturePresentation> Describe(MixturePresentationModel model);
    }
}
