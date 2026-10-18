using System;
using System.Buffers.Binary;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    // Admission of the configured (scenario 7) MADCFG01 wrapper: the exact
    // geometry, physics record and row-major solid mask around the unchanged
    // MACCLS01 solver image. These are the saved configuration a restore must
    // reproduce; they are never re-derived from the live scene. Native C++
    // still validates the complete solver model, walls, clock and phases, and
    // compares them with this wrapper, before any native load or activation.
    internal sealed class NativeConfiguredDomainImage
    {
        internal const int HeaderBytes=96,DigestBytes=64,MaximumBytes=256<<20;
        private static readonly byte[] Magic=Encoding.ASCII.GetBytes("MADCFG01");
        private readonly byte[] mask,solver;
        internal Configuration Geometry { get; }
        internal Physics Physics { get; }
        // Defensive copies for native calls and comparisons.
        internal byte[] Mask=>(byte[])mask.Clone();
        internal ReadOnlySpan<byte> SolverImage=>solver;
        private NativeConfiguredDomainImage(Configuration geometry,Physics physics,byte[] mask,byte[] solver)
        { Geometry=geometry;Physics=physics;this.mask=mask;this.solver=solver; }
        internal static bool HasMagic(ReadOnlySpan<byte> value)=>value.Length>=8 && value.Slice(0,8).SequenceEqual(Magic);
        private static void Require(bool valid,string message) { if(!valid) throw new InvalidDataException(message); }
        private static uint U(ReadOnlySpan<byte> value,int offset)=>BinaryPrimitives.ReadUInt32LittleEndian(value.Slice(offset,4));
        private static double D(ReadOnlySpan<byte> value,int offset)=>BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(value.Slice(offset,8)));
        private static bool PositiveZero(double value)=>BitConverter.DoubleToInt64Bits(value)==0;

        // The native admission rules of ma_oni_domain_start_configured for the
        // geometry and opt-in, independent of any live grid.
        internal static bool ValidGeometry(Configuration g)=>g.Size==40 && g.Version==2 && g.Flags==1 && g.Reserved==0 &&
            g.Scenario==ConfiguredScenario && g.Width>=2 && g.Height>=2 && g.Width<=ConfiguredMaximumSide && g.Height<=ConfiguredMaximumSide &&
            (ulong)g.Width*g.Height<=ConfiguredMaximumCells && g.GridWidth>=(ulong)g.Width+2 && g.X>0 && g.Y>0 &&
            (ulong)g.X+g.Width<g.GridWidth && g.GridWidth<=(uint)(int.MaxValue-2) &&
            ((ulong)g.Y+g.Height+2)*((ulong)g.GridWidth+2)<=int.MaxValue;
        // Enabled values within the admission bounds; disabled values exactly +0.
        internal static bool ValidPhysics(Physics p)
        {
            const uint known=PhysicsGravity|PhysicsConduction|PhysicsViscosity|PhysicsMixture|PhysicsLiquid;
            if(p.Size!=40 || p.Version!=ConfiguredAbiVersion || p.Reserved!=0 || (p.Flags&~known)!=0 ||
                ((p.Flags&PhysicsMixture)!=0 && (p.Flags&PhysicsLiquid)!=0)) return false;
            var g=p.GravityMetresPerSecondSquared;
            if((p.Flags&PhysicsGravity)!=0 ? !(!double.IsNaN(g) && !double.IsInfinity(g) && g>0 && g<=100) : !PositiveZero(g)) return false;
            bool Admitted(double value)=>!double.IsNaN(value) && !double.IsInfinity(value) && (value>0 || PositiveZero(value)) && value<=1000;
            var shear=p.ShearViscosityPaSeconds;var bulk=p.BulkViscosityPaSeconds;
            return (p.Flags&PhysicsViscosity)!=0 ? Admitted(shear) && Admitted(bulk) && (shear>0 || bulk>0) : PositiveZero(shear) && PositiveZero(bulk);
        }
        // Bitwise, as the native wrapper compares: -0/+0 and NaN payloads differ.
        internal static bool SamePhysics(Physics a,Physics b)=>a.Size==b.Size && a.Version==b.Version && a.Flags==b.Flags && a.Reserved==b.Reserved &&
            BitConverter.DoubleToInt64Bits(a.GravityMetresPerSecondSquared)==BitConverter.DoubleToInt64Bits(b.GravityMetresPerSecondSquared) &&
            BitConverter.DoubleToInt64Bits(a.ShearViscosityPaSeconds)==BitConverter.DoubleToInt64Bits(b.ShearViscosityPaSeconds) &&
            BitConverter.DoubleToInt64Bits(a.BulkViscosityPaSeconds)==BitConverter.DoubleToInt64Bits(b.BulkViscosityPaSeconds);
        internal static bool SameGeometry(Configuration a,Configuration b)=>a.Size==b.Size && a.Version==b.Version && a.Flags==b.Flags &&
            a.GridWidth==b.GridWidth && a.X==b.X && a.Y==b.Y && a.Width==b.Width && a.Height==b.Height && a.Scenario==b.Scenario && a.Reserved==b.Reserved;
        internal bool SameMask(ReadOnlySpan<byte> other)=>other.SequenceEqual(mask);
        // A public (unpadded) game cell inside the rectangle whose mask bit is fluid.
        internal bool IsFluidCell(uint publicCell)
        {
            var g=Geometry;var px=publicCell%g.GridWidth;var py=publicCell/g.GridWidth;
            return px>=g.X && py>=g.Y && (ulong)px<(ulong)g.X+g.Width && (ulong)py<(ulong)g.Y+g.Height &&
                mask[(int)(px-g.X)+(int)(py-g.Y)*(int)g.Width]==0;
        }

        internal static NativeConfiguredDomainImage Parse(ReadOnlySpan<byte> value)
        {
            Require(value.Length>=HeaderBytes+1+32+DigestBytes && value.Length<=MaximumBytes && HasMagic(value) && U(value,8)==1,
                "Unsupported configured domain image.");
            using(var hash=SHA256.Create())
            {
                var digest=BitConverter.ToString(hash.ComputeHash(value.Slice(0,value.Length-DigestBytes).ToArray())).Replace("-","").ToLowerInvariant();
                Require(value.Slice(value.Length-DigestBytes).SequenceEqual(Encoding.ASCII.GetBytes(digest)),"Configured domain image checksum differs.");
            }
            var cells=U(value,12);
            var g=new Configuration {Size=U(value,16),Version=U(value,20),Flags=U(value,24),GridWidth=U(value,28),X=U(value,32),Y=U(value,36),
                Width=U(value,40),Height=U(value,44),Scenario=U(value,48),Reserved=U(value,52)};
            var p=new Physics {Size=40,Version=ConfiguredAbiVersion,Flags=U(value,56),Reserved=U(value,60),
                GravityMetresPerSecondSquared=D(value,64),ShearViscosityPaSeconds=D(value,72),BulkViscosityPaSeconds=D(value,80)};
            var solverBytes=U(value,88);
            Require(U(value,92)==0 && cells>0 && cells<=ConfiguredMaximumCells && (ulong)g.Width*g.Height==cells && solverBytes>=32 &&
                (long)HeaderBytes+cells+solverBytes+DigestBytes==value.Length,"Configured domain image sizes or reserved fields differ.");
            Require(ValidGeometry(g),"Configured domain image has an unsupported geometry, scenario or opt-in.");
            Require(ValidPhysics(p),"Configured domain image has unsupported physics flags or values.");
            var bits=value.Slice(HeaderBytes,checked((int)cells)).ToArray();
            Require(bits.All(bit=>bit<=1) && bits.Any(bit=>bit==0),"Configured domain mask must be binary and retain a fluid cell.");
            var solver=value.Slice(HeaderBytes+(int)cells,checked((int)solverBytes)).ToArray();
            Require(solver.AsSpan(0,8).SequenceEqual(Encoding.ASCII.GetBytes("MACCLS01")),"Configured domain image does not enclose a MACCLS01 solver image.");
            return new NativeConfiguredDomainImage(g,p,bits,solver);
        }
    }
}
