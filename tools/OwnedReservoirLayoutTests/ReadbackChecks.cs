using System.Text;
using MixedAtmospheres.Oni;

internal static partial class Program
{
    private static ColonySaveReadback Readback(LayoutFixture f,byte[]? file=null,int? bodyBytes=null,string? bodyHash=null,byte[]? sim=null,Guid? colony=null)
        =>ColonySaveReadback.Verify(file??f.File,bodyBytes??f.Body.Length,bodyHash??Sha(f.Body),sim??f.Options.Sim,colony??Guid.Parse(LayoutFixture.NativeD),f.Header.AsSpan(16));
    private static void RegisterReadbackChecks()
    {
        // What the checks before a load read (ColonyOwnerPersistence.IsMixtureSave, LoadRefusal). Game builds
        // 763796 and 764025 write object-save 7.39; until 1.13.8 the reader returned nothing for their saves.
        Add("the sim section is read from object-save 7.38 and later minor versions only",()=>
        {
            foreach(var minor in new[]{38,39,40})
            {
                var f=new LayoutFixture(o=>o.ObjectMinor=minor);var sim=ColonySaveReadback.SimSection(f.File);
                Check(sim!=null && sim.AsSpan().SequenceEqual(f.Options.Sim));
                Check(OwnedReservoirLoadHeaderV1.InspectLayoutHeader(f.Header).SaveMinorVersion==minor);
                var plain=new LayoutFixture(o=>{o.ObjectMinor=minor;o.Compression=0;});
                Check(ColonySaveReadback.SimSection(plain.Header.Concat(plain.Directory).Concat(plain.Body).ToArray())!.AsSpan().SequenceEqual(plain.Options.Sim));
            }
            Check(ColonySaveReadback.SimSection(new LayoutFixture(o=>o.ObjectMinor=37).File)==null);
            Check(ColonySaveReadback.SimSection(new LayoutFixture(o=>o.Json=LayoutFixture.Json(false,false,39).Replace(":7,",":8,",StringComparison.Ordinal)).File)==null);
            var later=new LayoutFixture(o=>o.ObjectMinor=39);int at=later.BodyRanges["ksav"].Offset;
            Check(ColonySaveReadback.SimSection(later.Rebody(Put32(later.Body,at+8,37)))==null);
            Check(ColonySaveReadback.SimSection(later.Rebody(Put32(later.Body,at+4,8)))==null);
            Check(ColonySaveReadback.SimSection(later.Rebody(PutByte(later.Body,at,0)))==null);
            // The complete layout inspection still walks the objects of 7.38 only.
            Reject(later.File,SavedInspectionFailureKind.Unsupported,path:"KSAV");
        });
        Add("a Mixed Atmospheres envelope is returned with or without ONI's length prefix",()=>
        {
            var envelope=Encoding.ASCII.GetBytes("MACOLONY").Concat(new byte[]{1,2,3,4,5}).ToArray();
            foreach(var minor in new[]{38,39})
            {
                Check(ColonySaveReadback.SimSection(new LayoutFixture(o=>{o.ObjectMinor=minor;o.Sim=envelope;}).File)!.AsSpan().SequenceEqual(envelope));
                var prefixed=BitConverter.GetBytes(envelope.Length).Concat(envelope).ToArray();
                Check(ColonySaveReadback.SimSection(new LayoutFixture(o=>{o.ObjectMinor=minor;o.Sim=prefixed;}).File)!.AsSpan().SequenceEqual(envelope));
            }
            Check(ColonySaveReadback.SimSection(new byte[]{1,2,3})==null && ColonySaveReadback.SimSection(Array.Empty<byte>())==null);
        });
        Add("saved write receipt associates exact complete body and sim with one colony",()=>
        {
            var f=new LayoutFixture();var receipt=Readback(f);
            Check(receipt.FileBytes==f.File.Length && receipt.BodyBytes==f.Body.Length && receipt.FileSha256==Sha(f.File) && receipt.BodySha256==Sha(f.Body));
        });
        Add("uncompressed ordinary saves retain the same exact body check",()=>
        {
            var f=new LayoutFixture(o=>o.Compression=0);var file=f.Header.Concat(f.Directory).Concat(f.Body).ToArray();
            var receipt=Readback(f,file);Check(receipt.FileSha256==Sha(file) && receipt.BodySha256==Sha(f.Body));
            // Existing compressed-layout admission remains strict.
            Reject(file,SavedInspectionFailureKind.Unsupported);
            Throws<System.IO.InvalidDataException>(()=>Readback(f,file.Concat(new byte[]{0}).ToArray()));
            Throws<System.IO.InvalidDataException>(()=>Readback(f,file[..^1]));
        });
        Add("stale file with valid framing cannot satisfy the current serialization receipt",()=>
        {
            var previous=new LayoutFixture();var current=new LayoutFixture(o=>o.Tail=Encoding.ASCII.GetBytes("new managed state"));
            Throws<System.IO.InvalidDataException>(()=>Readback(current,previous.File));
            Check(previous.File.SequenceEqual(new LayoutFixture().File));
        });
        Add("same length changed managed bytes reject independently of exact sim bytes",()=>
        {
            var f=new LayoutFixture();var changed=(byte[])f.Body.Clone();changed[^1]^=1;
            Throws<System.IO.InvalidDataException>(()=>Readback(f,f.Rebody(changed)));
        });
        Add("an identical body cannot substitute an older different save header",()=>
        {
            var f=new LayoutFixture();var changed=new LayoutFixture(o=>o.Json=LayoutFixture.Json(false).Replace("synthetic","alternate"));
            Check(f.Body.SequenceEqual(changed.Body) && f.Header.Length==changed.Header.Length);
            Throws<System.IO.InvalidDataException>(()=>Readback(f,changed.File));
        });
        Add("body digest alone cannot substitute a different native physical pair",()=>
        {
            var f=new LayoutFixture();var different=(byte[])f.Options.Sim.Clone();different[0]^=1;
            Throws<System.IO.InvalidDataException>(()=>Readback(f,sim:different));
            Throws<System.IO.InvalidDataException>(()=>Readback(f,colony:Guid.Parse("22222222-2222-3333-4444-555555555555")));
        });
        Add("incomplete write and trailing compressed bytes cannot produce receipts",()=>
        {
            var f=new LayoutFixture();
            foreach(var file in new[]{f.File[..^1],f.File.Concat(new byte[]{0}).ToArray()})
                Throws<SavedInspectionException>(()=>Readback(f,file));
            Throws<System.IO.InvalidDataException>(()=>Readback(f,bodyBytes:f.Body.Length+1));
            Throws<System.IO.InvalidDataException>(()=>Readback(f,bodyHash:new string('0',64)));
        });
        Add("write receipt validates identity and source bounds before body decoding",()=>
        {
            var f=new LayoutFixture();
            Throws<ArgumentException>(()=>Readback(f,colony:Guid.Empty));
            Throws<ArgumentException>(()=>Readback(f,bodyBytes:0));
            Throws<ArgumentException>(()=>Readback(f,sim:Array.Empty<byte>()));
            Throws<System.IO.InvalidDataException>(()=>Readback(f,Put32(f.File,4,int.MaxValue)));
        });
    }
}
