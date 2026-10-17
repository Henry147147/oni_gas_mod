using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
using MixedAtmospheres.Oni;

internal static class TileHoverReceiptFileTests
{
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    private static extern SafeFileHandle CreateFileW(string name,uint access,uint sharing,IntPtr security,
        uint creation,uint flags,IntPtr template);
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check)
    {
        var directory=Path.Combine("local","hover-file-sharing-tests",Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var path=Path.GetFullPath(Path.Combine(directory,"receipt.json"));
        test("hover receipt reader bounds bytes and rejects malformed UTF8",()=> {
            File.WriteAllText(path,"{\"Case\":1}",new UTF8Encoding(false));
            check(TileHoverReceiptFile.Read(path)=="{\"Case\":1}");
            File.WriteAllBytes(path,new byte[4097]); var rejected=false;
            try { TileHoverReceiptFile.Read(path); } catch(InvalidDataException) { rejected=true; }
            check(rejected); File.WriteAllBytes(path,new byte[]{0xff}); rejected=false;
            try { TileHoverReceiptFile.Read(path); } catch(DecoderFallbackException) { rejected=true; }
            check(rejected);
        });
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) return;
        test("Windows hover receipt remains readable during a rename DELETE handle while old reader rejects",()=> {
            const string contents="{\"Case\":2,\"Revision\":17}"; File.WriteAllText(path,contents,new UTF8Encoding(false));
            using var rename=CreateFileW(path,0x10000,1|2|4,IntPtr.Zero,3,0,IntPtr.Zero);
            check(!rename.IsInvalid); var oldRejected=false;
            try { File.ReadAllText(path); } catch(IOException error) { oldRejected=(error.HResult&0xffff)==32; }
            check(oldRejected && TileHoverReceiptFile.Read(path)==contents);
        });
        test("Windows hover receipt reader still rejects an unfinished exclusive writer",()=> {
            using var writer=new FileStream(path,FileMode.Create,FileAccess.Write,FileShare.None);
            writer.WriteByte((byte)'{'); writer.Flush(true); var rejected=false;
            try { TileHoverReceiptFile.Read(path); } catch(IOException error) { rejected=(error.HResult&0xffff)==32; }
            check(rejected);
        });
    }
}
