using System;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // Storage's native rule differs from the conduit rule: material mass is
    // current, but element/tag-growth and overpopulation context are cached.
    // Keep that cache independently for every physical identity.
    internal readonly struct StoredGermContext
    {
        internal readonly ushort Element;
        internal readonly int Overpopulation;
        internal readonly float TagGrowthBase;
        internal readonly bool NativeSingle;
        internal StoredGermContext(ushort element,int overpopulation,float tagGrowthBase,bool nativeSingle)
        {
            if(tagGrowthBase<0||float.IsNaN(tagGrowthBase)||float.IsInfinity(tagGrowthBase))
                throw new InvalidDataException("Invalid stored culture tag-growth context.");
            Element=element;Overpopulation=overpopulation;TagGrowthBase=tagGrowthBase;NativeSingle=nativeSingle;
        }
        internal void Write(BinaryWriter writer)
        {writer.Write(Element);writer.Write(Overpopulation);writer.Write(TagGrowthBase);writer.Write(NativeSingle);}
        internal static StoredGermContext Read(BinaryReader reader)
        {
            var element=reader.ReadUInt16();var overpopulation=reader.ReadInt32();var tagGrowthBase=reader.ReadSingle();var native=reader.ReadByte();
            if(native>1)throw new InvalidDataException("Invalid stored culture mode.");
            return new StoredGermContext(element,overpopulation,tagGrowthBase,native!=0);
        }
    }
    internal static class ColonyStorageGermRounding
    {
        // DiseaseContainers accumulates its complete public CalculateDelta
        // result once, then truncates. Inserting another temperature term can
        // change signed zero or rounding and is not native storage parity.
        internal static bool TryStep(int population,float residue,float completeDelta,out NativeGermStep next)
        {
            if(population<0||float.IsNaN(residue)||float.IsInfinity(residue)||Math.Abs(residue)>=1||float.IsNaN(completeDelta)||float.IsInfinity(completeDelta))
                throw new ArgumentOutOfRangeException("Invalid native stored culture rounding context.");
            next=default;var accumulated=completeDelta+residue;
            if(float.IsNaN(accumulated)||float.IsInfinity(accumulated)||accumulated < -2147483648f||accumulated >= 2147483648f)return false;
            var change=(int)accumulated;var count=(long)population+change;
            if(count>int.MaxValue)return false;
            next=count<=0?new NativeGermStep(0,0,-population):new NativeGermStep((int)count,accumulated-(float)change,change);
            return true;
        }
    }
}
