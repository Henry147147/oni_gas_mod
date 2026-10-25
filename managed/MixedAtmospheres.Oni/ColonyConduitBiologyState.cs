using System;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // A pipe culture's allocation mass and cached material rule do not follow
    // its current contents. They belong to the saved state just like the
    // population clock; re-deriving them from a loaded packet changes biology.
    internal readonly struct PipeGrowthRule
    {
        internal readonly float UnderPopulationDeathRate,PopulationHalfLife,OverPopulationHalfLife,DiffusionScale,MinimumCountPerKg,MaximumCountPerKg;
        internal readonly int MinimumDiffusionCount;
        internal readonly byte MinimumDiffusionTicks;
        internal PipeGrowthRule(float death,float halfLife,float overHalfLife,float diffusion,float minimum,float maximum,int minimumDiffusion,byte diffusionTicks)
        {
            if(float.IsNaN(death)||float.IsNaN(halfLife)||float.IsNaN(overHalfLife)||float.IsNaN(diffusion)||float.IsNaN(minimum)||float.IsNaN(maximum))
                throw new InvalidDataException("Invalid saved pipe growth rule.");
            UnderPopulationDeathRate=death;PopulationHalfLife=halfLife;OverPopulationHalfLife=overHalfLife;DiffusionScale=diffusion;
            MinimumCountPerKg=minimum;MaximumCountPerKg=maximum;MinimumDiffusionCount=minimumDiffusion;MinimumDiffusionTicks=diffusionTicks;
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(UnderPopulationDeathRate);writer.Write(PopulationHalfLife);writer.Write(OverPopulationHalfLife);writer.Write(DiffusionScale);
            writer.Write(MinimumCountPerKg);writer.Write(MaximumCountPerKg);writer.Write(MinimumDiffusionCount);writer.Write(MinimumDiffusionTicks);
        }
        internal static PipeGrowthRule Read(BinaryReader reader)=>new PipeGrowthRule(reader.ReadSingle(),reader.ReadSingle(),reader.ReadSingle(),reader.ReadSingle(),
            reader.ReadSingle(),reader.ReadSingle(),reader.ReadInt32(),reader.ReadByte());
        internal bool SameBits(PipeGrowthRule other)=>
            BitConverter.SingleToInt32Bits(UnderPopulationDeathRate)==BitConverter.SingleToInt32Bits(other.UnderPopulationDeathRate)&&
            BitConverter.SingleToInt32Bits(PopulationHalfLife)==BitConverter.SingleToInt32Bits(other.PopulationHalfLife)&&
            BitConverter.SingleToInt32Bits(OverPopulationHalfLife)==BitConverter.SingleToInt32Bits(other.OverPopulationHalfLife)&&
            BitConverter.SingleToInt32Bits(DiffusionScale)==BitConverter.SingleToInt32Bits(other.DiffusionScale)&&
            BitConverter.SingleToInt32Bits(MinimumCountPerKg)==BitConverter.SingleToInt32Bits(other.MinimumCountPerKg)&&
            BitConverter.SingleToInt32Bits(MaximumCountPerKg)==BitConverter.SingleToInt32Bits(other.MaximumCountPerKg)&&
            MinimumDiffusionCount==other.MinimumDiffusionCount&&MinimumDiffusionTicks==other.MinimumDiffusionTicks;
    }
    internal sealed class PipeNativeCulture
    {
        internal readonly byte Phase,Disease;
        internal readonly int Cell,Count;
        internal readonly ushort CapturedElement;
        internal readonly float CapturedMass,Residue;
        internal readonly PipeGrowthRule Growth;
        internal PipeNativeCulture(byte phase,int cell,ushort element,float mass,byte disease,int count,float residue,PipeGrowthRule growth)
        {
            if(phase!=1&&phase!=2||cell<0||mass<0||float.IsNaN(mass)||float.IsInfinity(mass)||count<0||disease==byte.MaxValue&&count!=0||
                float.IsNaN(residue)||float.IsInfinity(residue)||Math.Abs(residue)>=1)
                throw new InvalidDataException("Invalid saved native pipe culture.");
            Phase=phase;Cell=cell;CapturedElement=element;CapturedMass=mass;Disease=disease;Count=count;Residue=residue;Growth=growth;
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(Phase);writer.Write(Cell);writer.Write(CapturedElement);writer.Write(CapturedMass);writer.Write(Disease);writer.Write(Count);writer.Write(Residue);Growth.Write(writer);
        }
        internal bool SameBits(PipeNativeCulture other)=>Phase==other.Phase&&Cell==other.Cell&&CapturedElement==other.CapturedElement&&Disease==other.Disease&&Count==other.Count&&
            BitConverter.SingleToInt32Bits(CapturedMass)==BitConverter.SingleToInt32Bits(other.CapturedMass)&&
            BitConverter.SingleToInt32Bits(Residue)==BitConverter.SingleToInt32Bits(other.Residue)&&Growth.SameBits(other.Growth);
        internal static PipeNativeCulture Read(BinaryReader reader,PipeGeometry geometry)
        {
            var culture=new PipeNativeCulture(reader.ReadByte(),reader.ReadInt32(),reader.ReadUInt16(),reader.ReadSingle(),reader.ReadByte(),reader.ReadInt32(),reader.ReadSingle(),PipeGrowthRule.Read(reader));
            if(!geometry.Contains(culture.Cell))throw new InvalidDataException("Saved native pipe culture leaves its world.");
            return culture;
        }
    }
}
