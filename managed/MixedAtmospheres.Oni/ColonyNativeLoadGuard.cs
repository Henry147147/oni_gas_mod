using System;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    internal sealed class ColonyNativeLoadGuard : IDisposable
    {
        private const string Library="mixed_atmospheres_oni";
        [StructLayout(LayoutKind.Sequential)]private struct Config
        {internal uint Size,Version,GridWidth,GridHeight,OffsetX,OffsetY,Width,Height;}
        [DllImport(Library,EntryPoint="ma_oni_load_guard_version",CallingConvention=CallingConvention.Cdecl)]private static extern uint Version();
        [DllImport(Library,EntryPoint="ma_oni_load_guard_begin",CallingConvention=CallingConvention.Cdecl)]private static extern int Begin(ref Config config,out ulong token);
        [DllImport(Library,EntryPoint="ma_oni_load_guard_end",CallingConvention=CallingConvention.Cdecl)]private static extern int End(ulong token,out ulong suppressed);
        private ulong token;
        internal static ulong Suppressed {get;private set;}
        internal static void Reset(){Suppressed=0;}
        internal ColonyNativeLoadGuard(OniNativeColony.Config owner)
        {
            const string mismatch="Mixture saves need the matching native load guard; reinstall the complete mod package.";
            uint version;
            try{version=Version();}
            catch(Exception error)when(error is DllNotFoundException||error is EntryPointNotFoundException){throw new NotSupportedException(mismatch,error);}
            if(Marshal.SizeOf<Config>()!=32||version!=1)throw new NotSupportedException(mismatch);
            var config=new Config{Size=32,Version=1,GridWidth=owner.GridWidth,GridHeight=owner.GridHeight,OffsetX=owner.OffsetX,OffsetY=owner.OffsetY,Width=owner.Width,Height=owner.Height};
            OniNativeColony.Check(Begin(ref config,out token));
        }
        public void Dispose()
        {
            if(token==0)return;var value=token;token=0;
            OniNativeColony.Check(End(value,out var suppressed));Suppressed=suppressed;
        }
    }
}
