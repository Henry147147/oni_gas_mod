using MixedAtmospheres.Oni;
using R=MixedAtmospheres.Oni.OniNativeColony.Receipt;
internal static class Program
{
 static int checks;
 static void Check(bool ok,string reason){++checks;if(!ok)throw new Exception("Check "+checks+": "+reason);}
 static R[] Records(int n,ulong first=1,ulong frame=18)=>Enumerable.Range(0,n).Select(i=>new R{Sequence=first+(ulong)i,CompletedFrame=frame,Kind=OniNativeColony.Byproduct,Callback=812+i,NativeIndex=(uint)(i%79),PublicCell=(uint)(100+i),MassKg=BitConverter.Int32BitsToSingle(0x3c800000+i),TemperatureK=BitConverter.Int32BitsToSingle(0x43960000+i),Disease=(uint)(i%3),DiseaseCount=i*7}).ToArray();
 static byte[] Wire(IEnumerable<R> values){using var stream=new MemoryStream();using var w=new BinaryWriter(stream);foreach(var r in values){w.Write(r.Sequence);w.Write(r.CompletedFrame);w.Write(r.Callback);w.Write(r.Kind);w.Write(r.NativeIndex);w.Write(r.PublicCell);w.Write(r.MassKg);w.Write(r.TemperatureK);w.Write(r.Disease);w.Write(r.DiseaseCount);}return stream.ToArray();}
 static void Prime(int count=20)
 {
  TestRuntime.Reset();ColonySaveBarrier.StepSeconds=.2f;OniNativeColony.Current.CompletedFrames=17;ColonyOwner.CommitTestPublication(17,1);
  OniNativeColony.Current.CompletedFrames=18;OniNativeColony.Pending.AddRange(Records(count));UnityEngine.Time.frameCount=2;ColonyOwner.CommitTestPublication(18,2);
  Check(ColonyOwner.TestState.Armed&&ColonyOwner.TestState.ObservedThrough==(ulong)Math.Min(count,4096),"publication did not fence observed prefix");
 }
 static void Pump(int frame){UnityEngine.Time.frameCount=frame;ColonyOwner.PumpDeferredByproducts();}
 static void NoAdvance(int steps=0,int zero=0){Check(Game.StepCalls==steps&&Game.PositiveCalls==0&&Game.ZeroCalls==zero&&OniNativeColony.Current.CompletedFrames==18,"pump advanced or posted a native frame");Check(ColonySaveBarrier.CanRunLifecycle,"pump leaked critical step");}
 static void StateControls()
 {
  var state=new ColonyReceiptPumpState();var bootstrap=state.Suspend();state.Published(1,17,100,1);Check(state.Suspended,"first publication cleared an explicit settlement scope");bootstrap.Dispose();Check(!state.Armed,"load frame armed pump");state.Published(1,17,120,2);Check(!state.Armed,"same-frame flush armed pump");state.Published(1,18,150,3);Check(state.Armed,"positive publication did not arm");
  Check(!state.TryPump(3,out _),"publication frame allowed pump");Check(state.TryPump(4,out var lease),"idle frame refused lease");Check(!state.TryEnter(out _),"normal reentry acquired shared gate");lease.Dispose();Check(!state.TryPump(4,out _),"duplicate idle-frame budget");
  var outer=state.Suspend();var child=state.Suspend();outer.Dispose();Check(state.Suspended&&!state.TryPump(5,out _),"suspension out-of-order cleanup");child.Dispose();Check(!state.Suspended,"nested suspension leaked");
  Check(state.TryEnter(out var old),"old lease");var stale=state.Suspend();state.Reset();Check(state.TryEnter(out var fresh),"fresh lease");old.Dispose();stale.Dispose();Check(state.Current(fresh)&&!state.Suspended,"stale disposal corrupted new session");fresh.Dispose();
  state.Published(2,3,40,6);Check(!state.Armed&&state.Session==2&&state.ObservedThrough==40,"new session inherited old publication");
  for(var i=0;i<100;i++){Check(state.TryEnter(out var token),"gate warmup");token.Dispose();}
#if !GAME_MONO
  var before=GC.GetAllocatedBytesForCurrentThread();for(var i=0;i<100000;i++){if(!state.TryEnter(out var token))throw new Exception();token.Dispose();}
  Check(GC.GetAllocatedBytesForCurrentThread()==before,"delivery gate allocated");
#endif
 }
 static void PrefixControls()
 {
  Prime();var expected=Records(20);var before=Wire(OniNativeColony.Pending);Pump(2);Check(Wire(OniNativeColony.Pending).SequenceEqual(before),"same Unity frame consumed receipts");
  Pump(3);Check(OniNativeColony.Acks.Last()==6&&OniNativeColony.Pending.Count==14,"idle2ms prefix differs");var first=Wire(OniNativeColony.Pending);Pump(3);Check(Wire(OniNativeColony.Pending).SequenceEqual(first),"second LateUpdate doubled budget");
  for(var frame=4;OniNativeColony.Pending.Count>0;++frame)Pump(frame);
  Check(TestRuntime.Dispatches.SequenceEqual(expected.Select(r=>r.Sequence))&&Wire(TestRuntime.Spawned).SequenceEqual(Wire(expected)),"pump duplicate/lost/reordered dispatch or payload bits");NoAdvance();Check(ColonyOwner.TestState.Delivered==17,"idle delivered count includes normal publication");
  Prime();OniNativeColony.Pending.AddRange(Records(1,21,18));for(var frame=3;frame<15;++frame)Pump(frame);
  Check(OniNativeColony.Pending.Count==1&&OniNativeColony.Pending[0].Sequence==21,"new same-frame enqueue crossed observed-sequence fence");NoAdvance();
  Prime();OniNativeColony.Pending[0]=Records(1,4,19)[0];var all=Wire(OniNativeColony.Pending);Pump(3);Check(Wire(OniNativeColony.Pending).SequenceEqual(all),"future-frame prefix consumed");NoAdvance();
  foreach(var kind in new[]{OniNativeColony.Extract,OniNativeColony.Consumer,OniNativeColony.Pipe,7u,42u}){
   Prime();var head=OniNativeColony.Pending[0];head.Kind=kind;OniNativeColony.Pending[0]=head;all=Wire(OniNativeColony.Pending);var acks=OniNativeColony.Acks.Count;Pump(3);Check(Wire(OniNativeColony.Pending).SequenceEqual(all)&&OniNativeColony.Acks.Count==acks,"nonbyproduct prefix consumed");NoAdvance();
  }
  foreach(var outcome in new[]{1,2,3}){
   Prime();TestRuntime.Outcomes[5]=outcome;Pump(3);Check(OniNativeColony.Acks.Last()==4&&OniNativeColony.Pending[0].Sequence==5&&TestRuntime.Outcomes[5]==outcome,"job/precise/moving tail was consumed");NoAdvance();
  }
  Prime();var invalid=OniNativeColony.Pending[0];invalid.Callback=0;OniNativeColony.Pending[0]=invalid;all=Wire(OniNativeColony.Pending);Pump(3);Check(Wire(OniNativeColony.Pending).SequenceEqual(all),"unhandled element receipt was acknowledged");
  Prime(5000);all=Wire(OniNativeColony.Pending);Pump(3);Check(OniNativeColony.Pending.Count==904&&Wire(OniNativeColony.Pending).SequenceEqual(all),"buffer-capacity tail was treated as observed");NoAdvance();
 }
 static void GuardControls()
 {
  var guards=new Action[]{()=>ColonyOwner.Active=false,()=>ColonyLoadRefusal.Refused=true,()=>Game.Started=false,()=>SpeedControlScreen.Instance.IsPaused=true,()=>UnityEngine.Time.timeScale=0,()=>ColonyOwnerPausedResave.Holding=true,()=>ColonyContinuationProbe.Holding=true,()=>ColonyCallbackRequests.Pending=true,()=>ColonySavedParticles.Pending=true,()=>ColonyPipeSave.Pending=true,()=>ColonyPipeRuntime.NativeBindingsPending=true,()=>ColonyPipeCultureState.NeedsReadyBarrier=true,()=>ColonyClusterRetirement.Pending=true};
  foreach(var guard in guards){Prime();guard();var before=Wire(OniNativeColony.Pending);var reads=TestRuntime.RawReads;Pump(3);Check(Wire(OniNativeColony.Pending).SequenceEqual(before)&&TestRuntime.RawReads==reads,"load/pause/session/lifecycle guard failed");NoAdvance();}
  Prime();ColonyContinuationProbe.Holding=true;var captured=Wire(OniNativeColony.Pending);var captureAcks=OniNativeColony.Acks.ToArray();Pump(3);Pump(4);Check(Wire(OniNativeColony.Pending).SequenceEqual(captured)&&OniNativeColony.Acks.SequenceEqual(captureAcks),"continuation checkpoint boundary consumed pending receipts");ColonyContinuationProbe.Holding=false;Pump(5);Check(OniNativeColony.Acks.Last()==6&&OniNativeColony.Pending[0].Sequence==7,"pump did not resume after continuation capture");NoAdvance();
  Prime();using(var suspend=ColonyOwner.SuspendReceiptPump()){var before=Wire(OniNativeColony.Pending);Pump(3);Check(Wire(OniNativeColony.Pending).SequenceEqual(before),"settlement suspension failed");}Pump(4);Check(OniNativeColony.Acks.Last()==6,"settlement suspension leaked");
  Prime();ColonySaveBarrier.EnterStep();try{Pump(3);Check(OniNativeColony.Acks.Last()==3,"unfinished step allowed pump");}finally{ColonySaveBarrier.ExitStep();}NoAdvance();
  TestRuntime.Reset();ColonySaveBarrier.StepSeconds=.2f;ColonyOwner.CommitTestPublication(17,1);OniNativeColony.Current.CompletedFrames=18;OniNativeColony.Pending.AddRange(Records(20));TestRuntime.PublicationFails=true;ColonyOwner.CommitTestPublication(18,2);Pump(3);Check(OniNativeColony.Pending.Count==17&&!ColonyOwner.TestState.Armed,"failed publication advanced receipt fence");
 }
 static void ReentryControls()
 {
  Prime();TestRuntime.DispatchAction=()=>{TestRuntime.DispatchAction=null;ColonyOwner.PumpDeferredByproducts();_ = ColonyOwner.DeliverReceipts(new OwnedGameDataSnapshot());};var copies=TestRuntime.Copies;Pump(3);Check(OniNativeColony.Acks.Last()==6&&TestRuntime.Copies==copies+1,"reentrant pump/normal delivery copied or duplicated shared prefix");NoAdvance();
  Prime();UnityEngine.Time.frameCount=3;ColonySaveBarrier.StepSeconds=.2f;var normalCopies=TestRuntime.Copies;TestRuntime.DispatchAction=()=>{TestRuntime.DispatchAction=null;ColonyOwner.PumpDeferredByproducts();};_ = ColonyOwner.DeliverReceipts(new OwnedGameDataSnapshot());Check(OniNativeColony.Acks.Last()==6&&TestRuntime.Copies==normalCopies+1,"normal delivery allowed reentrant pump");NoAdvance();
  Prime();TestRuntime.DispatchAction=()=>{TestRuntime.DispatchAction=null;Game.Instance.StepTheSim(0);};copies=TestRuntime.Copies;Pump(3);Check(OniNativeColony.Acks.Last()==4&&TestRuntime.Copies==copies+1,"reentrant managed publication did not stop idle transaction");NoAdvance(1);
  Prime();TestRuntime.DispatchAction=()=>{TestRuntime.DispatchAction=null;Check(!ColonySaveBarrier.BeforeFileSave("save",false,true),"save inside Spawn wasn't deferred");};Pump(3);Check(Game.DelayedSaves==1&&OniNativeColony.Acks.Last()==6,"deferred save broke receipt prefix");NoAdvance();Check(ColonySaveBarrier.BeforeFileSave("save",false,true)&&OniNativeColony.Pending.Count==0,"deferred save failed to drain full ordinary barrier");Check(Game.PositiveCalls==0&&ColonySaveBarrier.ForCapture()==18,"save advanced clock");ColonySaveBarrier.SaveFinished();
  Prime();TestRuntime.DispatchAction=()=>{TestRuntime.DispatchAction=null;OniNativeColony.Pending.AddRange(Records(1,21));};Pump(3);Check(OniNativeColony.Pending.Last().Sequence==21&&OniNativeColony.Acks.Last()==6,"native append disturbed ordered prefix");
  Prime();var replacement=Records(4,1,1);TestRuntime.DispatchAction=()=>{TestRuntime.DispatchAction=null;ColonyOwner.ResetReceiptPump();OniNativeColony.Current.Session=2;OniNativeColony.Pending.Clear();OniNativeColony.Pending.AddRange(replacement);};var acks=OniNativeColony.Acks.Count;Pump(3);Check(OniNativeColony.Acks.Count==acks&&Wire(OniNativeColony.Pending).SequenceEqual(Wire(replacement))&&!ColonyFault.Faulted,"reset after Spawn acknowledged/faulted a new session");Check(ColonySaveBarrier.CanRunLifecycle,"reset leaked critical scope");Check(ColonyFrameLog.Ticks>=1000&&ColonyOwner.TestState.TotalTicks==0,"reset dropped global pump timing or mutated new-session timing");
 }
 static void FaultControls()
 {
  foreach(var phase in new[]{"status","copy","outcome","dispatch","ack"}){
   Prime();var before=Wire(OniNativeColony.Pending);var acks=OniNativeColony.Acks.Count;TestRuntime.FailAt=phase;Pump(3);Check(ColonyFault.Faulted&&OniNativeColony.Acks.Count==acks&&Wire(OniNativeColony.Pending).SequenceEqual(before),"fault before Ack changed custody: "+phase);var sent=TestRuntime.Dispatches.Count;Pump(4);Check(TestRuntime.Dispatches.Count==sent,"faulted pump retried Spawn: "+phase);NoAdvance();
  }
  Prime();var pending=Wire(OniNativeColony.Pending);var acknowledged=OniNativeColony.Acks.Count;TestRuntime.BeforeRawRead=()=>{if(TestRuntime.RawReads==2)OniNativeColony.Current.State=2;};Pump(3);Check(OniNativeColony.Acks.Count==acknowledged&&Wire(OniNativeColony.Pending).SequenceEqual(pending),"native fault before Ack changed pending prefix");NoAdvance();
 }
 static void EmptyPumpAllocation()
 {
  Prime(5);Pump(3);Check(OniNativeColony.Pending.Count==0,"empty pump fixture");
  for(var i=0;i<100;++i)Pump(1000+i);
#if !GAME_MONO
  var before=GC.GetAllocatedBytesForCurrentThread();for(var i=0;i<10000;++i)Pump(2000+i);var bytes=GC.GetAllocatedBytesForCurrentThread()-before;
  Check(bytes==0,"warmed empty-pump infrastructure allocated "+bytes);
#endif
  NoAdvance();
 }
 static void PacketControls()
 {
  for(var trial=0;trial<50;++trial){var random=new Random(147147+trial);var count=random.Next(5,150);Prime(count);var expected=Records(count);for(var frame=3;OniNativeColony.Pending.Count>0;++frame){Check(frame<300,"no pump progress");Pump(frame);}Check(TestRuntime.Dispatches.SequenceEqual(expected.Select(r=>r.Sequence))&&Wire(TestRuntime.Spawned).SequenceEqual(Wire(expected)),"randomized prefix order/payload bytes");Check(OniNativeColony.Acks.Last()==(ulong)count,"randomized Ack tail");NoAdvance();}
 }
 static int Main(string[] args){try{
  Check((ColonyOwner.ReceiptPumpReport()!=null)==(Environment.GetEnvironmentVariable("MA_COLONY_BYPRODUCT_PUMP_LOG")=="1"),"pump diagnostics were not opt-in");
  if(args.SequenceEqual(new[]{"--disabled"})||Environment.GetEnvironmentVariable("MA_TEST_RECEIPT_PUMP_DISABLED")=="1")
  {
   Check(Environment.GetEnvironmentVariable("MA_COLONY_BYPRODUCT_PUMP")=="0","disabled control needs explicit opt-out");Prime();var before=Wire(OniNativeColony.Pending);Pump(3);Check(Wire(OniNativeColony.Pending).SequenceEqual(before)&&ColonyOwner.TestState.Delivered==0,"opt-out delivered idle receipts");
   ColonySaveBarrier.StepSeconds=0;ColonyOwner.CommitTestPublication(18,4);Check(OniNativeColony.Pending.Count==0,"opt-out changed original zero-time full publication");NoAdvance();Console.WriteLine("PASS receipt pump opt-out: "+checks+" assertions; original publication drains, idle pump does not.");return 0;
  }
  if(args.Length!=0)throw new ArgumentException("Unknown receipt pump control argument");
  StateControls();PrefixControls();GuardControls();ReentryControls();FaultControls();EmptyPumpAllocation();PacketControls();
  Console.WriteLine("PASS idle receipt pump: "+checks+" assertions; actual delivery, pump and save-barrier bodies with deterministic clock/fake endpoints.");
#if GAME_MONO
  Console.WriteLine("Game Mono allocation counter unsupported; allocation assertions omitted, functional controls retained.");
#endif
  return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
