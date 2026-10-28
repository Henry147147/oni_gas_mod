using System.Text;
using MixedAtmospheres.Oni;

internal static class ActivityCheckpointChecks
{
    internal static int Run()
    {
        var checks=0;
        foreach(var pendingMode in new[]{false,true})
        foreach(var lifecycle in new[]{false,true})
        foreach(var wrapped in new[]{false,true})
        {
            var image=Image(pendingMode,lifecycle,wrapped,out var countAt,out var flagAt,out var transferMassAt);
            var before=(byte[])image.Clone();
            Check(ColonyCheckpointMetadata.CompletedFrames(image)==77);
            Check(ColonyCheckpointMetadata.OwnerVersion(image)==14);
            Check(ColonyCheckpointMetadata.CallbackRequestCount(image)==(wrapped?1u:0u));
            Check(ColonyCheckpointMetadata.Profile(image).Equals(new ColonyPhysicalProfile(14,28,1,400,800,8)));
            ColonyCheckpointMetadata.RefuseExperimentalActivity(image);
            Check(ColonyCheckpointMetadata.PhysicalImage(image).Slice(0,8).SequenceEqual(Encoding.ASCII.GetBytes("MACCLS01")));
            Check(image.SequenceEqual(before));
            if(!pendingMode&&!lifecycle&&!wrapped)
            {
                ColonyActivityResaveProof.ValidateImmutableOwner(image,before);++checks;
                var transfer=(byte[])image.Clone();BitConverter.GetBytes(.125).CopyTo(transfer,transferMassAt);
                var actor=(byte[])image.Clone();BitConverter.GetBytes(2u).CopyTo(actor,countAt+4+20);
                var later=(byte[])image.Clone();BitConverter.GetBytes((double).1f).CopyTo(later,countAt+4+84+8);
                foreach(var changed in new[]{transfer,actor,later})
                {
                    Check(ColonyCheckpointMetadata.CompletedFrames(changed)==77);
                    Check(ColonyCheckpointMetadata.Profile(changed).Equals(ColonyCheckpointMetadata.Profile(image)));
                    Check(ColonyCheckpointMetadata.PhysicalImage(changed).SequenceEqual(ColonyCheckpointMetadata.PhysicalImage(image)));
                    var refused=false;
                    try{ColonyActivityResaveProof.ValidateImmutableOwner(image,changed);}catch(InvalidOperationException){refused=true;}
                    Check(refused);
                }
                var emptyRefused=false;
                try{ColonyActivityResaveProof.ValidateImmutableOwner(Array.Empty<byte>(),Array.Empty<byte>());}catch(InvalidOperationException){emptyRefused=true;}
                Check(emptyRefused);
            }
            var tooMany=(byte[])image.Clone();BitConverter.GetBytes(1025u).CopyTo(tooMany,countAt);Refuse(()=>ColonyCheckpointMetadata.Profile(tooMany));
            var badFlag=(byte[])image.Clone();badFlag[flagAt]=2;Refuse(()=>ColonyCheckpointMetadata.Profile(badFlag));
            Refuse(()=>ColonyCheckpointMetadata.Profile(image.AsSpan(0,image.Length-1)));
        }
        return checks;
        void Check(bool yes) { ++checks;if(!yes)throw new Exception("Activity checkpoint metadata check failed."); }
        void Refuse(Action action) { try{action();}catch(InvalidDataException){++checks;return;}throw new Exception("Expected activity framing refusal."); }
    }
    internal static int ReadNativeImages(string directory)
    {
        var checks=0;
        foreach(var name in new[]{"native-wrapper14.owner","native-wrapper14-pending.owner","native-wrapper13-base14.owner"})
        {
            var image=File.ReadAllBytes(Path.Combine(directory,name));var original=(byte[])image.Clone();
            var profile=ColonyCheckpointMetadata.Profile(image);
            if(profile.OwnerVersion!=14 || profile.PhysicalVersion!=28 || profile.PressureIterationCap<400 || profile.HeatIterationCap<800)
                throw new Exception("Native activity checkpoint metadata differs: " + name);
            ++checks;
            ColonyCheckpointMetadata.RefuseExperimentalActivity(image);++checks;
            _=ColonyCheckpointMetadata.CompletedFrames(image);++checks;
            if((ColonyCheckpointMetadata.CallbackRequestCount(image)>0)!=(name=="native-wrapper13-base14.owner") || !image.SequenceEqual(original))
                throw new Exception("Native callback framing or source bytes differ: " + name);
            ++checks;
        }
        return checks;
    }
    private static byte[] Image(bool mode,bool lifecycle,bool wrapped,out int countAt,out int flagAt,out int transferMassAt)
    {
        using var stream=new MemoryStream();using var w=new BinaryWriter(stream);
        w.Write(Encoding.ASCII.GetBytes("MACOLN01"));w.Write(wrapped?13u:14u);if(wrapped){w.Write(14u);w.Write(1u);}
        w.Write(new byte[56]);w.Write(2u);w.Write(new byte[2*168]);
        w.Write(9u);w.Write(new byte[9]);w.Write(9u);w.Write(new byte[9]);w.Write(0u);
        w.Write(new byte[(6*2+8)*16]);w.Write(new byte[16]);w.Write(77UL);w.Write(new byte[6*8+7*8]);
        w.Write(1u);transferMassAt=(int)stream.Position+24;w.Write(new byte[48]);
        for(var k=1;k<4;++k)w.Write(0u);
        w.Write(1UL);w.Write(0u); // pipe-job next token, jobs
        w.Write(1u);w.Write(77UL);w.Write(0u);w.Write(0u);w.Write(0u);w.Write(0.0); // biology
        w.Write(12u);w.Write(Encoding.ASCII.GetBytes("MAPREC01"));w.Write(0u);
        w.Write(40UL);w.Write(42UL);flagAt=(int)stream.Position;w.Write((byte)1);w.Write(mode);if(mode)w.Write(false);
        w.Write(9u);for(var k=0;k<9;++k)w.Write(k*.01);
        countAt=(int)stream.Position;w.Write(2u);Packet(41,.2f);Packet(42,.2f);
        w.Write(lifecycle);if(lifecycle)Packet(0,0);
        w.Write(2u);w.Write(1u);w.Write(7u);
        using var physical=new MemoryStream();using(var p=new BinaryWriter(physical,Encoding.UTF8,true))
        {
            p.Write(Encoding.ASCII.GetBytes("MACCLS01"));p.Write(28u);p.Write(3u);p.Write(new byte[3]);
            p.Write(new byte[8+8+4+4+8+8+8+8+1+8+8+4+8]);p.Write(8.0);p.Write(new byte[1+8+8]);
            p.Write(1u);p.Write(400u);p.Write(800u);p.Write(new byte[40]);
        }
        w.Write((ulong)physical.Length);w.Write(physical.ToArray());w.Write(new byte[64]);return stream.ToArray();
        void Packet(ulong sequence,double seconds)
        {
            w.Write(sequence);w.Write(seconds);w.Write(3u);w.Write(1u);w.Write(4u);w.Write(7u);
            w.Write(2u);w.Write(2u);w.Write(8u);for(var k=0;k<5;++k)w.Write((ulong)(sequence==0?0:k+1));
        }
    }
}
