using System.Reflection;
using System.Runtime.InteropServices;
using MixedAtmospheres.Oni;

namespace HarmonyLib {
 [AttributeUsage(AttributeTargets.Class,AllowMultiple=true)]internal sealed class HarmonyPatch:Attribute{public HarmonyPatch(Type t,string name){}public HarmonyPatch(Type t,string name,Type[] args){}}
 internal static class AccessTools { internal static MethodInfo? Method(Type t,string name)=>t.GetMethod(name,BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static); }
}
namespace UnityEngine {internal static class Time {internal static int frameCount;internal static float timeScale=3;} internal static class Debug {internal static readonly List<string> Lines=new();internal static void Log(object line)=>Lines.Add(line.ToString()!);} }
internal enum SimHashes {}
internal static class Grid {internal static bool IsValidCell(int cell)=>cell>=0&&cell<1000000;}
internal static class ElementLoader {internal static object? FindElementByHash(SimHashes hash)=>(int)hash==0?null:TestRuntime.Element;}
internal sealed class SpeedControlScreen {internal static readonly SpeedControlScreen Instance=new();internal bool IsPaused;}
internal static class PauseScreen {public static void TriggerQuitGame(){}}
internal sealed class SaveLoader {public static void LoadScene(){}public string Save(string name,bool autosave,bool pointer)=>name;}
internal sealed class Game {
 internal static readonly Game Instance=new();internal static int PositiveCalls,ZeroCalls,DelayedSaves,StepCalls;internal static Action? NativeMessages;
 internal void StartDelayedSave(string name,bool autosave,bool pointer)=>++DelayedSaves;
 public void UnsafeSim200ms(float dt){if(dt>0)++PositiveCalls;else ++ZeroCalls;StepTheSim(dt);++OniNativeObserver.Hooks[8].Entries;NativeMessages?.Invoke();++OniNativeObserver.Hooks[19].Exits;}
 internal static bool Started=true;public bool GameStarted()=>Started;
 public void StepTheSim(float dt){++StepCalls;
  const BindingFlags flags=BindingFlags.Static|BindingFlags.NonPublic;
  var prefix=typeof(ColonySaveStepPatch).GetMethod("Prefix",flags)!;var finalizer=typeof(ColonySaveStepPatch).GetMethod("Finalizer",flags)!;
  Exception? error=null;
  try {prefix.Invoke(null,new object[]{dt});ColonyOwner.CommitTestPublication(OniNativeColony.Current.CompletedFrames,UnityEngine.Time.frameCount);}
  catch(TargetInvocationException e){error=e.InnerException;}
  catch(Exception e){error=e;}
  var result=(Exception?)finalizer.Invoke(null,new object?[]{error});if(result!=null)throw result;
 }
}
internal static class Sim {
 [StructLayout(LayoutKind.Sequential)]internal struct MassConsumedCallback {public int callbackIdx;public ushort elemIdx;public float mass,temperature;public byte diseaseIdx;public int diseaseCount;}
 [StructLayout(LayoutKind.Sequential)]internal struct MassEmittedCallback {public int callbackIdx;public ushort elemIdx;public byte suceeded;public float mass,temperature;public byte diseaseIdx;public int diseaseCount;}
 [StructLayout(LayoutKind.Sequential)]internal struct ConsumedMassInfo {public int Cell;public float Mass;}
}
namespace MixedAtmospheres.Oni {
 internal static class Clock {internal const long Frequency=1000000;internal static long Timestamp;}
 internal enum SnapshotEventChannel {MassConsumedCallbacks,MassEmittedCallbacks,RemovedMassEntries}
 internal sealed class OwnedGameDataSnapshot {
  internal readonly List<byte> Bytes=new();
  internal void AppendEvents<T>(SnapshotEventChannel channel,T[] values)where T:struct {
   TestRuntime.Fail("append");Bytes.Add((byte)channel);Bytes.AddRange(BitConverter.GetBytes(values.Length));
   foreach(var v in values){using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true))foreach(var field in typeof(T).GetFields().OrderBy(f=>f.MetadataToken)){var item=field.GetValue(v);if(item is int i)writer.Write(i);else if(item is ushort u)writer.Write(u);else if(item is float f)writer.Write(f);else if(item is byte b)writer.Write(b);else throw new NotSupportedException(field.Name);}var bytes=stream.ToArray();Bytes.AddRange(bytes);if(!TestRuntime.Callbacks.TryGetValue(channel,out var all))TestRuntime.Callbacks.Add(channel,all=new());all.AddRange(bytes);}
  }
 }
 internal static class OniNativeColony {
  internal const uint Byproduct=5,Extract=1,Consumer=9,Pipe=10;
  [StructLayout(LayoutKind.Sequential)]internal struct Receipt {public ulong Sequence,CompletedFrame;public int Callback;public uint Kind,NativeIndex,PublicCell;public float MassKg,TemperatureK;public uint Disease;public int DiseaseCount;}
  internal struct Status {public uint State;public ulong Session,CompletedFrames;public ulong ReceiptsPending;public string Error;internal static Status Empty=>new(){Error=""};}
  internal static Status Current=new(){State=1,Session=1,CompletedFrames=17,Error=""};
  internal static readonly List<Receipt> Pending=new();internal static readonly List<ulong> Acks=new();internal static uint Submitted,Deferred;internal static Action? FlushWork;
  internal static void Check(int code){if(code!=0)throw new InvalidOperationException("native failure "+code);}
  internal static int Wait()=>0;internal static int Busy()=>0;
  internal static int CopyStatus(ref Status s){s=Current;s.ReceiptsPending=(uint)Pending.Count;return 0;}
  internal static byte DiseaseIdx(uint value)=>(byte)value;
  internal static int CopyReceipts(Receipt[] output,uint capacity,out uint count){TestRuntime.Fail("copy");++TestRuntime.Copies;count=(uint)Pending.Count;Pending.CopyTo(0,output,0,Math.Min(Pending.Count,(int)capacity));Clock.Timestamp+=TestRuntime.CopyTicks;TestRuntime.AfterCopy?.Invoke();return 0;}
  internal static int AckReceipts(ulong through){TestRuntime.Fail("ack");if(through==0)throw new InvalidOperationException("zero ack");Acks.Add(through);Pending.RemoveAll(r=>r.Sequence<=through);foreach(var key in TestRuntime.Outcomes.Keys.Where(k=>k<=through).ToArray())TestRuntime.Outcomes.Remove(key);return 0;}
  internal static uint FlushVersion=1;internal static uint InputFlushVersion()=>FlushVersion;
  internal static int FlushInputs(out uint submitted,out uint deferred){FlushWork?.Invoke();FlushWork=null;submitted=Submitted;deferred=Deferred;Submitted=Deferred=0;return 0;}
 }
 internal sealed class Hook {internal ulong Entries,Exits;}
 internal sealed class HookStatus {internal Hook[] Hooks=OniNativeObserver.Hooks;}
 internal static class OniNativeObserver {internal static readonly Hook[] Hooks=Enumerable.Range(0,20).Select(_=>new Hook()).ToArray();internal static HookStatus CaptureStatus()=>new();}
 internal static class ColonyClusterRetirement {internal static bool Pending;}
 internal static class ColonyOwnedStorage {internal static bool RetirementsPending;internal static long NativeDataDeclarations;}
 internal static class ColonySavedParticles {internal static bool Pending;internal static void RestorePending(){TestRuntime.Fail("restore");}}
 internal static class ColonyPipeSave {internal static bool Pending;internal static Action? Restore;internal static void TryRestore(){var run=Restore;Restore=null;run?.Invoke();}}
 internal static class ColonyPipeRuntime {internal static bool Active,NativeBindingsPending;}
 internal static class ColonyPipeCultureState {internal static bool NeedsReadyBarrier;}
 internal static class ColonyOwnerPausedResave {internal static bool Holding;}
 internal static class ColonyLoadRefusal {internal static bool Refused;}
 internal static class ColonyConduitsSchedule {internal static void FlushTopology(){}}
 internal static class ColonyPipeContactHeat {internal static int Remaining;internal static string? BodyHeatMismatch()=>Remaining-->0?"held contact":null;}
 internal static class ColonyRuntimeValidation {internal static object? Settings;}
 internal static class ColonyActivity {internal static void Refresh(){}}
 internal static class ColonyActors {internal static bool IsConsumerReceipt(OniNativeColony.Receipt r)=>r.Kind==OniNativeColony.Consumer;internal static Sim.ConsumedMassInfo Event(OniNativeColony.Receipt r){TestRuntime.Events.Add(r.Sequence);return new(){Cell=(int)r.PublicCell,Mass=r.MassKg};}}
 internal static class ColonyPipePorts {internal static bool Receipt(in OniNativeColony.Receipt r){if(r.Kind!=OniNativeColony.Pipe && !TestRuntime.Outcomes.ContainsKey(r.Sequence))return false;TestRuntime.Dispatch(r);return true;}internal static void Acknowledged(ulong sequence){TestRuntime.Fail("settled");}}
 internal static class ColonyFault {
  internal static bool Faulted;internal static string Failure="";
  internal static void Inject(string where)=>TestRuntime.Fail(where);
  internal static void Raise(string where,Exception error){Faulted=true;Failure=where+":"+error.Message;OniNativeColony.Current.State=2;}
  internal static Exception? Contain(Exception? error,string where){if(error!=null)Raise(where,error);return null;}
 }
 internal static class TestRuntime {
  internal static bool PublicationFails;internal static readonly object Element=new();internal static int Copies,RawReads;internal static Action? AfterCopy,BeforeRawRead;internal static readonly Dictionary<ulong,int> Outcomes=new();
  internal static readonly Dictionary<SnapshotEventChannel,List<byte>> Callbacks=new();internal static readonly List<OniNativeColony.Receipt> Spawned=new();internal static readonly List<ulong> Dispatches=new(),Events=new();internal static readonly List<byte> Payloads=new();
  internal static OwnedGameDataSnapshot? LastSnapshot;internal static long WorkTicks=1000,CopyTicks;internal static string FailAt="";internal static Action? DispatchAction;
  internal static void Fail(string phase){if(FailAt==phase){FailAt="";throw new InvalidOperationException("injected "+phase);}}
  internal static void Dispatch(OniNativeColony.Receipt r){Fail("dispatch");Dispatches.Add(r.Sequence);Spawned.Add(r);Payloads.AddRange(BitConverter.GetBytes(r.Sequence));Payloads.AddRange(BitConverter.GetBytes(r.MassKg));Payloads.AddRange(BitConverter.GetBytes(r.TemperatureK));Clock.Timestamp+=WorkTicks;DispatchAction?.Invoke();}
  internal static void Reset(){ColonyFrameLog.Ticks=0;PublicationFails=false;Copies=RawReads=0;AfterCopy=BeforeRawRead=null;Outcomes.Clear();UnityEngine.Time.frameCount=1;UnityEngine.Time.timeScale=3;SpeedControlScreen.Instance.IsPaused=false;ColonySavedParticles.Pending=ColonyCallbackRequests.Pending=ColonyContinuationProbe.Holding=ColonyLoadRefusal.Refused=ColonyOwnerPausedResave.Holding=ColonyPipeSave.Pending=ColonyPipeRuntime.NativeBindingsPending=ColonyPipeCultureState.NeedsReadyBarrier=false;ColonyOwner.ResetReceiptPump();ColonySaveBarrier.Reset();ColonySaveBarrier.SaveFinished();ColonyOwner.DeferredReceiptPublications=0;ColonyOwner.ReceiptBudgetTicks=2000;ColonyOwner.publicationLog=false;ColonyOwner.Active=true;ColonyFault.Faulted=false;ColonyFault.Failure="";OniNativeColony.Pending.Clear();OniNativeColony.Acks.Clear();OniNativeColony.Current=new(){State=1,Session=1,CompletedFrames=17,Error=""};OniNativeColony.Submitted=OniNativeColony.Deferred=0;OniNativeColony.FlushVersion=1;OniNativeColony.FlushWork=null;Dispatches.Clear();Spawned.Clear();Events.Clear();Payloads.Clear();Callbacks.Clear();LastSnapshot=null;Clock.Timestamp=0;WorkTicks=1000;CopyTicks=0;FailAt="";DispatchAction=null;Game.PositiveCalls=Game.ZeroCalls=Game.DelayedSaves=Game.StepCalls=0;Game.Started=true;Game.NativeMessages=null;ColonyPipeSave.Restore=null;ColonyPipeRuntime.Active=false;ColonyPipeContactHeat.Remaining=0;ColonyRuntimeValidation.Settings=null;ColonyClusterRetirement.Pending=false;ColonyOwnedStorage.RetirementsPending=false;ColonyOwnedStorage.NativeDataDeclarations=0;UnityEngine.Debug.Lines.Clear();}
 }
}

namespace MixedAtmospheres.Oni {internal static class ColonyReceiptPumpNative {internal struct Reading {internal uint State;internal ulong Session,Frame,Pending;}
internal static Reading Read(){++TestRuntime.RawReads;TestRuntime.BeforeRawRead?.Invoke();TestRuntime.Fail("status");return new(){State=OniNativeColony.Current.State,Session=OniNativeColony.Current.Session,Frame=OniNativeColony.Current.CompletedFrames,Pending=(ulong)OniNativeColony.Pending.Count};}
internal static bool PlainByproduct(ulong sequence){TestRuntime.Fail("outcome");return !TestRuntime.Outcomes.ContainsKey(sequence);}
}}

namespace MixedAtmospheres.Oni {internal static class ColonyFrameLog {internal enum Part {Receipts,ByproductPump} internal static long Ticks;internal static void AddTicks(Part part,long ticks){Ticks+=ticks;} }}

namespace MixedAtmospheres.Oni {internal static class ColonyContinuationProbe {internal static bool Holding;}internal static class ColonyCallbackRequests {internal static bool Pending;}}
