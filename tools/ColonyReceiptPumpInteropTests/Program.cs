using System.Runtime.InteropServices;
using MixedAtmospheres.Oni;
internal static class Program {
 static int Main(string[] args){try{
  if(args.Length!=1)throw new ArgumentException("native backend path required");
#if !GAME_MONO
  var path=Path.GetFullPath(args[0]);NativeLibrary.SetDllImportResolver(typeof(Program).Assembly,(name,assembly,search)=>name=="mixed_atmospheres_oni"?NativeLibrary.Load(path):IntPtr.Zero);
#endif
  var legacy=OniNativeColony.Status.Empty;OniNativeColony.Check(OniNativeColony.CopyStatus(ref legacy));var raw=ColonyReceiptPumpNative.Read();
  if(Marshal.SizeOf<OniNativeColony.Status>()!=600||Marshal.SizeOf<ColonyReceiptPumpNative.Reading>()!=600||Marshal.SizeOf<OniNativeColony.Receipt>()!=48||Marshal.OffsetOf<OniNativeColony.Status>("ErrorBytes").ToInt32()!=344)throw new Exception("ABI size/offset mismatch");
  if(raw.State!=legacy.State||raw.Session!=legacy.Session||raw.Frame!=legacy.CompletedFrames||raw.Pending!=legacy.ReceiptsPending||raw.Seconds!=legacy.TimeSeconds)throw new Exception("raw/legacy native status differs");
  if(raw.State!=0)throw new Exception("interop control requires a fresh inactive process");
  var receipts=new OniNativeColony.Receipt[4096];for(var i=0;i<200;++i){_ = ColonyReceiptPumpNative.Read();if(!ColonyReceiptPumpNative.PlainByproduct(1))throw new Exception();OniNativeColony.Check(OniNativeColony.CopyReceipts(receipts,out _));OniNativeColony.Check(OniNativeColony.AckReceipts(0));}
#if !GAME_MONO
  var before=GC.GetAllocatedBytesForCurrentThread();
#endif
  for(var i=0;i<10000;++i){_ = ColonyReceiptPumpNative.Read();if(!ColonyReceiptPumpNative.PlainByproduct(1))throw new Exception();OniNativeColony.Check(OniNativeColony.CopyReceipts(receipts,out _));OniNativeColony.Check(OniNativeColony.AckReceipts(0));}
#if !GAME_MONO
  var bytes=GC.GetAllocatedBytesForCurrentThread()-before;if(bytes!=0)throw new Exception("warmed native interop allocated "+bytes+" bytes");
  Console.WriteLine("PASS warmed native interop allocation0bytes/10000iterations.");
#else
  Console.WriteLine("Game Mono allocation counter unsupported; no Mono allocation claim.");
#endif
  var after=ColonyReceiptPumpNative.Read();if(after.State!=0||after.Session!=raw.Session||after.Frame!=raw.Frame||after.Seconds!=raw.Seconds)throw new Exception("status/sidecar/ack control advanced owner state");
  Console.WriteLine("PASS native pump interop: legacy/raw600byte offsets+48byte receipt ABI;10000 warmed status/job/precise/moving/copy/ack iterations; inactive owner clocks unchanged.");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
