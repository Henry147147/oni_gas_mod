using System.Text;
using MixedAtmospheres.Oni;

internal static partial class Program
{
    private static ColonySaveReadback Readback(LayoutFixture f,byte[]? file=null,int? bodyBytes=null,string? bodyHash=null,byte[]? sim=null,Guid? colony=null)
        =>ColonySaveReadback.Verify(file??f.File,bodyBytes??f.Body.Length,bodyHash??Sha(f.Body),sim??f.Options.Sim,colony??Guid.Parse(LayoutFixture.NativeD));
    private static void RegisterReadbackChecks()
    {
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
