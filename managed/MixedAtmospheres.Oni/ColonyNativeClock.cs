using System;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    internal static class ColonyNativeClock
    {
        private const string Library="mixed_atmospheres_oni";
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeState
        {
            internal uint Size,Version;
            internal int Width,Height,Cells;
            internal uint Random;
            internal int Iterate,Displacement;
            internal ushort Tick,Reserved;
            internal static NativeState Empty=>new NativeState{Size=36,Version=1};
            internal ColonyNativeClockState Image()=>new ColonyNativeClockState(Width,Height,Cells,Random,Iterate,Displacement,Tick);
        }
        [DllImport(Library,EntryPoint="ma_oni_runtime_clock_version",CallingConvention=CallingConvention.Cdecl)]private static extern uint Version();
        [DllImport(Library,EntryPoint="ma_oni_runtime_clock_start",CallingConvention=CallingConvention.Cdecl)]private static extern int Start();
        [DllImport(Library,EntryPoint="ma_oni_runtime_clock_copy",CallingConvention=CallingConvention.Cdecl)]private static extern int Copy(ref NativeState output);
        [DllImport(Library,EntryPoint="ma_oni_runtime_clock_restore",CallingConvention=CallingConvention.Cdecl)]private static extern int Restore(ref NativeState input);
        [DllImport(Library,EntryPoint="ma_oni_runtime_clock_pending",CallingConvention=CallingConvention.Cdecl)]private static extern int Pending(out uint pending);
        private static ColonyNativeClockState? pending;
        private static ColonyNativeClockState? captured,restored;
        internal static bool Enabled {get;private set;}
        internal static int Restores {get;private set;}
        internal static int Captures {get;private set;}
        private static object? Facts(ColonyNativeClockState? state)=>state==null?null:new{state.Width,state.Height,state.Cells,state.Random,state.Iterate,state.Displacement,state.Tick};
        internal static object Report()=>new{Enabled,Pending=pending!=null,Captures,Restores,Captured=Facts(captured),Restored=Facts(restored)};
        internal static void Reset(){Enabled=false;pending=captured=restored=null;Captures=Restores=0;}
        internal static void StartRequestedControl()
        {if(Environment.GetEnvironmentVariable("MA_COLONY_NATIVE_CLOCK_CONTROL")=="1")StartControl();}
        private static void StartControl()
        {
            if(Enabled)return;
            if(!ColonyPipeRuntime.NativeScheduleControl||Marshal.SizeOf<NativeState>()!=36||Version()!=1)
                throw new NotSupportedException("Native loop continuation needs its matching backend and native-schedule control.");
            OniNativeColony.Check(Start());Enabled=true;
        }
        internal static void Load(byte[]? image){pending=image==null?null:ColonyNativeClockState.Decode(image);}
        private static ColonyNativeClockState Read()
        {var state=NativeState.Empty;OniNativeColony.Check(Copy(ref state));if(state.Reserved!=0)throw new InvalidOperationException("Native loop state has reserved bits.");return state.Image();}
        internal static byte[]? Capture()
        {
            if(pending!=null)throw new InvalidOperationException("Native loop continuation has not restored before saving.");
            if(!Enabled)return null;captured=Read();++Captures;return captured.Encode();
        }
        internal static void BeforePositiveTime()
        {
            var image=pending;if(image==null)return;
            StartControl();
            // First obtain a current native world after all prefab messages
            // and pipe/culture bindings settled. This also activates capture.
            ColonySaveBarrier.SettleNativeStartupMessages();
            var state=new NativeState{Size=36,Version=1,Width=image.Width,Height=image.Height,Cells=image.Cells,Random=image.Random,
                Iterate=image.Iterate,Displacement=image.Displacement,Tick=image.Tick};
            OniNativeColony.Check(Restore(ref state));
            ColonySaveBarrier.SettleNativeStartupMessages();
            OniNativeColony.Check(Pending(out var waiting));
            if(waiting!=0||!Read().Same(image))throw new InvalidOperationException("Native loop continuation did not complete at its zero-time barrier.");
            restored=image;pending=null;++Restores;
        }
    }
}
