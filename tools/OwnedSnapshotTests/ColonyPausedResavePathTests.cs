using System;
using System.Globalization;
using System.IO;
using MixedAtmospheres.Oni;

internal static class ColonyPausedResavePathTests
{
    internal static void Run(System.Action<string,System.Action> test, System.Action<bool> check)
    {
        void InDirectory(System.Action<string> action)
        {
            var directory=Path.Combine(Path.GetTempPath(),"ma-paused-resave-"+Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            try { action(directory); } finally { Directory.Delete(directory,true); }
        }
        test("paused resave gives cold and menu sessions distinct files and preserves prior evidence",()=>InDirectory(directory=> {
            var first=ColonyPausedResavePath.Destination(directory,1);
            File.WriteAllText(first,"verified first owner");
            var second=ColonyPausedResavePath.Destination(directory,2);
            check(first!=second);
            File.WriteAllText(second,"verified second owner");
            check(File.ReadAllText(first)=="verified first owner" && File.ReadAllText(second)=="verified second owner");
        }));
        test("paused resave refuses existing files and directories without overwriting them",()=>InDirectory(directory=> {
            var file=ColonyPausedResavePath.Destination(directory,7);File.WriteAllText(file,"preserve");
            bool refused=false;try { ColonyPausedResavePath.Destination(directory,7); } catch(InvalidOperationException) { refused=true; }
            check(refused && File.ReadAllText(file)=="preserve");
            var folder=ColonyPausedResavePath.Destination(directory,8);Directory.CreateDirectory(folder);
            refused=false;try { ColonyPausedResavePath.Destination(directory,8); } catch(InvalidOperationException) { refused=true; }
            check(refused && Directory.Exists(folder));
        }));
        test("paused resave requires a live session and uses invariant full-width session names",()=>InDirectory(directory=> {
            bool refused=false;try { ColonyPausedResavePath.Destination(directory,0); } catch(InvalidDataException) { refused=true; }
            check(refused);
            var original=CultureInfo.CurrentCulture;
            try {
                CultureInfo.CurrentCulture=CultureInfo.GetCultureInfo("ar-SA");
                check(Path.GetFileName(ColonyPausedResavePath.Destination(directory,ulong.MaxValue))=="owner-frame-zero-resave-18446744073709551615.sav");
            } finally { CultureInfo.CurrentCulture=original; }
        }));
    }
}
