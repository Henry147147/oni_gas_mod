using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using HarmonyLib;
using MixedAtmospheres.Core.Interop;
using UnityEngine;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    // Explicit bounded development save/load. Ordinary scalar saves remain
    // guarded; only one paused, completed pair can enter one Sim.Save call.
    internal static unsafe partial class NativeSealedDomainValidation
    {
        internal static bool ColonySaveMode=>ColonyRuntimeValidation.Settings?.Mode=="native-save-domain" || ColonyRuntimeValidation.Settings?.SaveOnComplete==true;
        internal static bool ColonyRestoreMode=>ColonyRuntimeValidation.Settings?.Mode=="native-load-domain";
        private static ColonySaveEnvelope? stagedColonySave;
        private static NativeDomainCheckpointPayload? pendingColonyRestore;
        private static byte[]? restoredExpected;
        private static ulong replayFrame, savedSourceFrame;
        private static ulong restoredSubmittedFrames;
        private static double savedSourceTime, replayTime;
        private static ulong loadedSourceFrame;
        private static double loadedSourceTime;
        private static string loadedSourceHash="";
        private static bool frameZeroResaveVerified;
        private static bool frameZeroResaveScheduled;
        private static bool initialPauseChallengeVerified;
        private static int initialPauseTicksHeld,initialPositiveTicksHeld;
        private static int resumePreparationFrame=-1;
        internal static bool HoldInitialRestorePause=>ColonyRestoreMode && colonyLoaded &&
            ColonyRuntimeValidation.Settings?.SaveBeforeResume==true && !frameZeroResaveVerified;
        internal static bool HoldActorRestorePause=>ColonyRestoreMode && colonyLoaded && restoredActorImage!=null &&
            (!restoredActorBindingsVerified || PendingActorRestores()!=0);
        internal static bool HoldValidationSimulationTick=>HoldInitialRestorePause || HoldActorRestorePause || HoldOrbitalReferenceRestorePause || HoldIoControlPause || HoldLiveBreathingRestorePause || HoldTransientRestorePause;
        internal static void PublishPausedValidationTick(Game game,float elapsed)
        {
            if(SpeedControlScreen.Instance!=null && !SpeedControlScreen.Instance.IsPaused) SpeedControlScreen.Instance.Pause(false);
            // Time.deltaTime was latched before a pause set this frame's time
            // scale to zero. Do not spend that positive interval or any saved
            // managed subtick accumulator while the initial save is pending.
            if(HoldInitialRestorePause) {++initialPauseTicksHeld;if(elapsed>0) ++initialPositiveTicksHeld;}
            AccessTools.Method(typeof(Game),"UnsafeSim200ms").Invoke(game,new object[]{0f});
        }
        private static string savedPhysicalHash="", restoredInitialHash="", savedFileHash="", savedFilePath="";
        private static bool colonyFileSaved, colonyLoaded, zeroRestoreVerified, restoredSceneStarted, restoredRendererVerified;
        private static int stagedSaveWrites, exactReplayComparisons;
        private static bool restoredScenarioSelected;
        private static uint restoredScenario;
        private static int domainManagedThread;
        private static readonly List<OwnedGameDataSnapshot> restoreNativeOutbox=new List<OwnedGameDataSnapshot>();
        private static int restoredNativeBatches;
        private static byte[] CatalogHash()=>Enumerable.Range(0,32).Select(i=>Convert.ToByte(catalog!.Fingerprint.Substring(2*i,2),16)).ToArray();
        private static void AdmitNativeInventory(NativeDomainCheckpointPayload payload,ReadOnlySpan<byte> native)
        {
            var phases=ElementLoader.elements.ToDictionary(e=>(int)e.id,e=>e.IsGas ? NativeMatterPhase.Gas : e.IsLiquid ? NativeMatterPhase.Liquid : e.IsSolid ? NativeMatterPhase.Solid : NativeMatterPhase.Vacuum);
            try {payload.ValidateNativeInventory(native,phases,(int)SimHashes.Vacuum,(int)SimHashes.Oxygen,(int)SimHashes.Water,(int)SimHashes.Unobtanium);}
            catch(InvalidDataException)
            {
                if(IsMode)
                {
                    var path=Path.Combine(ColonyRuntimeValidation.OutputDirectory,"rejected-native-inventory-"+Guid.NewGuid().ToString("N")+".bin");
                    try {using var diagnostic=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None);var bytes=native.ToArray();diagnostic.Write(bytes,0,bytes.Length);}
                    catch(IOException) { }
                }
                throw;
            }
        }
        private static int automaticSaveAdmissions;
        private static bool savedAsAutoSave;
        private static int serializedBodyCaptures,serializedBodyBytes;
        private static int serializedHeaderCaptures;
        private static byte[] serializedHeaderJson=Array.Empty<byte>();
        private static string serializedBodyHash="";
        private static bool savedFileReadbackVerified;
        private static bool savePauseStateRestored;
        private static int savePauseDepthBefore,savePauseDepthAfter;
        private static int PauseDepth()=>(int)AccessTools.Field(typeof(SpeedControlScreen),"pauseCount").GetValue(SpeedControlScreen.Instance);
        private static bool rejectedSaveVerified;
        private static bool SaveRejectionRequired=>ColonyRuntimeValidation.Settings?.Mode=="native-save-domain";
        private static void VerifyPendingSaveRejection()
        {
            if(rejectedSaveVerified || !SaveRejectionRequired) return;
            SpeedControlScreen.Instance.Pause(false);
            AccessTools.Method(typeof(Game),"UnsafeSim200ms").Invoke(Game.Instance,new object[]{0f});Capture();
            var frame=latest.CompletedFrames;var time=latest.TimeSeconds;
            SimMessages.ModifyEnergy(target,.001f,1000,SimMessages.EnergySourceID.DebugHeat);
            var heat=HeatStatus.Empty;Check(CopyHeat(ref heat));Require(heat.PendingBatches==1,"Pending save rejection has no reserved command.");
            var rejectedPath=Path.Combine(ColonyRuntimeValidation.OutputDirectory,"rejected-pending-colony.sav");
            Require(!File.Exists(rejectedPath),"Pending save rejection destination already exists.");
            SpeedControlScreen.Instance.Unpause(false);var declinedBefore=declinedColonySaves;
            SaveLoader.Instance.Save(rejectedPath,false,false);
            AcknowledgePrivateSaveProblem();var declined=declinedColonySaves==declinedBefore+1;
            Capture();Check(CopyHeat(ref heat));
            rejectedSaveVerified=declined && !File.Exists(rejectedPath) && !SpeedControlScreen.Instance.IsPaused && stagedColonySave==null &&
                automaticSaveAdmissions==0 && latest.State==1 && latest.CompletedFrames==frame && latest.TimeSeconds==time && heat.PendingBatches==1;
            Require(rejectedSaveVerified,"Declined pending save wrote a file, changed time, lost its command or left the game paused.");
        }
        internal sealed class ColonySaveLease
        {
            internal readonly ulong Token;
            internal readonly byte[] Physical;
            internal readonly bool WasPaused;
            internal readonly int InitialPauseDepth;
            internal readonly string PreviousSavePath;
            internal readonly HashSet<int> PreviousDialogs;
            internal bool FileAccepted {get;private set;}
            private bool finished;
            internal ColonySaveLease(ulong token,byte[] physical,bool paused,int depth)
            {Token=token;Physical=physical;WasPaused=paused;InitialPauseDepth=depth;PreviousSavePath=SaveLoader.GetActiveSaveFilePath();PreviousDialogs=ActiveSaveDialogs();}
            internal void CompleteFile(string requested,string actual)
            {
                if(Path.GetFullPath(requested)!=Path.GetFullPath(actual) || !File.Exists(actual) || new FileInfo(actual).Length<=0)
                    throw new IOException("Normal colony save did not finish its requested file.");
                Require(serializedBodyCaptures==1 && stagedColonySave!=null,"Normal colony save has no complete serialized body.");
                if(serializedHeaderCaptures!=1) throw new IOException("Normal colony save did not finish writing its current header.");
                using var file=new FileStream(actual,FileMode.Open,FileAccess.Read,FileShare.Read);
                if(file.Length>1<<30) throw new IOException("Saved colony exceeds the supported readback limit.");
                var bytes=new byte[checked((int)file.Length)];var count=0;
                while(count<bytes.Length) {var got=file.Read(bytes,count,bytes.Length-count);if(got<=0) throw new IOException("Saved colony was truncated during readback.");count+=got;}
                if(file.ReadByte()!=-1) throw new IOException("Saved colony grew during readback.");
                var receipt=ColonySaveReadback.Verify(bytes,serializedBodyBytes,serializedBodyHash,
                    stagedColonySave!.CopyEncodedData(),SaveLoader.Instance.GameInfo.colonyGuid,serializedHeaderJson);
                savedFilePath=actual;savedFileHash=receipt.FileSha256;savedFileReadbackVerified=true;FileAccepted=true;
            }
            internal void RecordWriteProblem(Exception problem)=>RecordUnsuccessfulColonyWrite(this,problem);
            internal void Finish(bool success)
            {
                if(finished) return;finished=true;
                try
                {
                    if(success)
                    {
                        Require(stagedColonySave!=null && SavedTransientParticlesUnchanged(NativeDomainCheckpointPayload.Parse(stagedColonySave.CustomData).Geometry),
                            "Normal colony serialization changed an in-flight native CO2 parcel.");
                        Require(stagedSaveWrites==1 && savedFileReadbackVerified && TryCapturePhysicalCheckpoint(out var after) && after.SequenceEqual(Physical),
                            "Normal colony serialization reused its sim section or changed physical state.");
                        colonyFileSaved=true;
                        ObserveNaturalAutosaveCompleted(Physical);
                    }
                }
                finally
                {
                    stagedColonySave=null;
                    try {Check(ReleasePair(Token));}
                    finally
                    {
                        // Pause is reference-counted. Release exactly the
                        // request this lease added, including an already
                        // paused caller; preserve a latched domain fault.
                        if(WasPaused || (active && latest.State==1 && !failed)) SpeedControlScreen.Instance.Unpause(false);
                        savePauseDepthBefore=InitialPauseDepth;savePauseDepthAfter=PauseDepth();
                        savePauseStateRestored=savePauseDepthAfter==savePauseDepthBefore;
                    }
                }
            }
        }
        internal static ColonySaveLease BeginAutomaticColonySave(bool isAutoSave)
        {
            Require(Thread.CurrentThread.ManagedThreadId==domainManagedThread && active && !arming && !failed && Game.Instance!=null && Game.Instance.GameStarted() && stagedColonySave==null &&
                restoreNativeOutbox.Count==0,"Colony save requires one live, fully published domain and no nested save.");
            var wasPaused=SpeedControlScreen.Instance.IsPaused;var pauseDepth=PauseDepth();SpeedControlScreen.Instance.Pause(false);
            ulong token=0;
            try
            {
                var synchronize=AccessTools.Method(typeof(Game),"UnsafeSim200ms");
                var publish=AccessTools.Method(typeof(Game),"StepTheSim");
                synchronize.Invoke(Game.Instance,new object[]{0f});
                Capture();VerifyNativeOrbitalReferences("before-paired-save");var source=latest;var requested=PairStatus.Empty;
                var requestCode=RequestSnapshotPair(source.Session,source.CompletedFrames,0,0,ref requested);
                if(requestCode==3)
                {
                    var detail=Marshal.PtrToStringAnsi(LastError())??"A complete checkpoint is not available yet.";
                    var actors=ActorStatus.Empty;Check(CopyActorStatus(ref actors));
                    throw new ColonySaveUnavailableException(detail,actors.LiveActors!=0);
                }
                Check(requestCode);token=requested.Token;
                synchronize.Invoke(Game.Instance,new object[]{0f});var timer=Stopwatch.StartNew();var ready=PairStatus.Empty;
                do
                {
                    Check(PollPair(token,ref ready));
                    if(ready.State==1) {publish.Invoke(Game.Instance,new object[]{0f});Thread.Sleep(1);}
                }
                while(ready.State==1 && timer.ElapsedMilliseconds<5000);
                Require(ready.State==2 && ready.Session==source.Session && ready.CompletedFrame==source.CompletedFrames && ready.PhysicalTime==source.TimeSeconds,
                    "Paused checkpoint source differs: state="+ready.State+", captured="+ready.CompletedFrame+"/"+ready.PhysicalTime+", requested="+source.CompletedFrames+"/"+source.TimeSeconds);
                var native=new byte[checked((int)ready.NativeBytes)];var physical=new byte[checked((int)ready.PhysicalBytes)];
                Check(CopyPair(token,native,ready.NativeBytes,physical,ready.PhysicalBytes));
                RecordSavedPersistentActors(physical);
                var particles=CaptureTransientParticles(ready.Configuration);
                var payload=NativeDomainCheckpointPayload.Create(ready,physical,particles);AdmitNativeInventory(payload,native);
                lastSavedTransientParticles=particles;
                var config=payload.Geometry;Check(ValidateCheckpoint(ref config,physical,(uint)physical.Length,payload.PhysicalTime));
                stagedColonySave=ColonySaveEnvelope.Create(744825,2,ready.CompletedFrame,CatalogHash(),native,payload.CopyEncodedData());stagedSaveWrites=0;
                savedSourceFrame=ready.CompletedFrame;savedSourceTime=ready.PhysicalTime;savedPhysicalHash=CheckpointHash(physical);
                savedNativeCycle=GameClock.Instance.GetCycle();savedNativeGameSeconds=GameClock.Instance.GetTime();
                ++automaticSaveAdmissions;savePauseStateRestored=false;savedAsAutoSave=isAutoSave;
                colonyFileSaved=false;
                serializedBodyCaptures=serializedBodyBytes=0;serializedBodyHash="";savedFileReadbackVerified=false;
                serializedHeaderCaptures=0;serializedHeaderJson=Array.Empty<byte>();
                return new ColonySaveLease(token,physical,wasPaused,pauseDepth);
            }
            catch
            {
                stagedColonySave=null;
                try {if(token!=0) Check(ReleasePair(token));}
                finally {if(wasPaused || (active && latest.State==1 && !failed)) SpeedControlScreen.Instance.Unpause(false);}
                throw;
            }
        }
        private static void SavePausedColony()
        {
            Require(SpeedControlScreen.Instance.IsPaused && !colonyFileSaved,"Colony checkpoint fixture requires a fresh source.");
            SeedPausedTransientParticle();
            var temporary=Path.Combine(ColonyRuntimeValidation.OutputDirectory,"native-colony.pending.sav");
            var final=Path.Combine(ColonyRuntimeValidation.OutputDirectory,"native-colony.sav");
            Require(!File.Exists(temporary) && !File.Exists(final),"Checkpoint destination already exists.");
            // Exercise the same unprepared public entry used by menu/autosave,
            // including pausing and restoring a previously running game.
            SpeedControlScreen.Instance.Unpause(false);
            var written=SaveLoader.Instance.Save(temporary,false,false);
            Require(written==temporary && colonyFileSaved && automaticSaveAdmissions==(IoSaveControlRequired ? 2 : 1) && stagedSaveWrites==1 &&
                savePauseStateRestored && !SpeedControlScreen.Instance.IsPaused && stagedColonySave==null,
                "The ordinary colony save entry did not reserve, write and retire one complete pair.");
            SpeedControlScreen.Instance.Pause(false);
            using(var file=new FileStream(temporary,FileMode.Open,FileAccess.ReadWrite,FileShare.None)) file.Flush(true);
            File.Move(temporary,final);savedFilePath=final;savedFileHash=CheckpointHash(File.ReadAllBytes(final));
            Require(TryCapturePhysicalCheckpoint(out var physical),"Saved physical owner is unavailable after ordinary save.");
            File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory,"saved-colony-physical.bin"),physical);
        }
        internal static bool TryWriteStagedColonySave(BinaryWriter writer,int offsetX,int offsetY)
        {
            if(stagedColonySave==null) return false;
            Require(active && SpeedControlScreen.Instance.IsPaused && offsetX==0 && offsetY==0 && stagedSaveWrites==0,
                "Staged colony save was reused or changed placement.");
            stagedColonySave.WriteLengthPrefixed(writer);++stagedSaveWrites;return true;
        }
        internal static void CaptureSerializedColonyBody(BinaryWriter writer)
        {
            if(stagedColonySave==null) return;
            Require(serializedBodyCaptures==0 && stagedSaveWrites==1 && writer.BaseStream is MemoryStream,
                "Ordinary save did not produce one complete in-memory colony body.");
            writer.Flush();var stream=(MemoryStream)writer.BaseStream;
            Require(stream.Position==stream.Length && stream.Length>0 && stream.Length<=256<<20,
                "Ordinary save body is incomplete or exceeds its supported size.");
            var body=stream.ToArray();serializedBodyBytes=body.Length;serializedBodyHash=CheckpointHash(body);++serializedBodyCaptures;
        }
        internal static void CaptureSerializedColonyHeader(byte[] bytes)
        {
            if(stagedColonySave==null || serializedBodyCaptures!=1) return;
            serializedHeaderJson=(byte[])bytes.Clone();++serializedHeaderCaptures;
        }
        internal static bool TryLoadPairedColony(IReader reader,ref int result)
        {
            var data=reader.RawBytes();var position=reader.Position;
            if(position<0 || position>data.Length-4) return false;
            var length=BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(position));
            if(length<8 || length>data.Length-position-4 || !ColonySaveEnvelope.HasMagic(data.AsSpan(position+4,length))) return false;
            Require(ColonyRestoreMode && !active && pendingColonyRestore==null,"This paired colony requires the explicit native load development mode.");
            Require(ActorCheckpointVersion()==1 && Marshal.SizeOf<PersistentActorCommand>()==96,"Unsupported durable native actor ABI.");
            Require(RadialConsumptionVersion()==1,"Unsupported native radius-consumption capability.");
            Require(FrameInputsVersion()==1,"Unsupported native accepted frame input journal ABI.");
            RequireChunkHeatCapability();
            ConfigureDomainPresentation();
            var envelope=ColonySaveEnvelope.Parse(data.AsSpan(position+4,length),744825,2,CatalogHash());
            var payload=NativeDomainCheckpointPayload.Parse(envelope.CustomData);AdmitNativeInventory(payload,envelope.NativeData);
            Require(payload.CompletedFrame==envelope.CompletedFrame,"Saved colony physical source differs.");
            var physical=payload.PhysicalImage.ToArray();var config=payload.Geometry;
            Check(ValidateCheckpoint(ref config,physical,(uint)physical.Length,payload.PhysicalTime));
            // Consume original framing only after full admission. The normal
            // SaveLoader continues with the same reader and managed objects.
            Require(reader.ReadInt32()==length && reader.ReadBytes(length).AsSpan().SequenceEqual(envelope.CopyEncodedData()),"Selected paired section changed during loading.");
            var native=envelope.NativeData.ToArray();IntPtr loaded;
            fixed(byte* pointer=native) loaded=Sim.SIM_HandleMessage(-672538170,native.Length,pointer);
            Require(loaded!=IntPtr.Zero,"Original native loader rejected its admitted paired world.");
            pendingColonyRestore=payload;result=0;return true;
        }
        internal static void RestoreAfterNativeStart()
        {
            var payload=pendingColonyRestore;if(payload==null) return;
            Require(ColonyRestoreMode && !active,"Native start attempted to replace a live physical owner.");
            restoredScenario=payload.Geometry.Scenario;restoredScenarioSelected=true;domainManagedThread=Thread.CurrentThread.ManagedThreadId;
            x=checked((int)payload.Geometry.X);y=checked((int)payload.Geometry.Y);target=Grid.XYToCell(x,y);
            hotSolid=Grid.XYToCell(x+Width+2,y);coldSolid=hotSolid+1;
            var config=payload.Geometry;var physical=payload.PhysicalImage.ToArray();latest=Status.Empty;
            restoredActorImage=NativeActorCheckpointImage.HasMagic(physical) ? NativeActorCheckpointImage.Parse(physical) : null;
            arming=true;
            Check(RestoreCheckpoint(ref config,physical,(uint)physical.Length,payload.PhysicalTime,ref latest));
            prepared=true;started=Time.realtimeSinceStartup;restoredExpected=physical;replayTime=payload.PhysicalTime;replayGeometry=payload.Geometry;
            pendingTransientParticles=payload.TransientParticles;
            savedSourceFrame=loadedSourceFrame=payload.CompletedFrame;savedSourceTime=loadedSourceTime=payload.PhysicalTime;
            savedPhysicalHash=loadedSourceHash=CheckpointHash(physical);
            // No Unity simulation objects have been deserialized yet. Restore
            // omitted native flags through lifecycle-only worker passes.
            for(var index=0;index<cells.Length;++index)
            {
                var global=Grid.XYToCell(x+index%Width,y+index/Width);
                var packet=Packet(writer=>{writer.Write(global);writer.Write(-1);writer.Write(NativeFlowMask);writer.Write((byte)1);writer.Write((ushort)0);});
                SendRaw(-469311643,packet);
            }
            SendZeroTimeFrame();var first=WaitZeroTimeFrame();
            Require(NoMaterialEvents(first),"Zero-time flow flag restoration emitted native material.");
            using(var baseline=OwnedGameDataSnapshot.Capture(first,Grid.WidthInCells,Grid.HeightInCells))
            {
                for(var index=0;index<cells.Length;++index)
                {
                    var column=index%Width;var global=Grid.XYToCell(x+column,y+index/Width);
                    var packet=Packet(writer=>
                    {
                        writer.Write(global);writer.Write(-1);writer.Write(ProjectionTemperature(column));writer.Write(ProjectionMass(column));writer.Write(0);
                        writer.Write((ushort)ElementLoader.FindElementByHash(Liquid ? SimHashes.Water : SimHashes.Oxygen).idx);writer.Write((byte)1);writer.Write((byte)255);writer.Write((byte)1);writer.Write(new byte[3]);
                    });SendRaw(-1252920804,packet);
                }
                SendZeroTimeFrame();var restored=WaitZeroTimeFrame();
                Require(NoMaterialEvents(restored),"Zero-time compatibility restoration emitted native material.");
                var source=baseline.Pointer;
                for(var global=0;global<Grid.CellCount;++global)
                {
                    var xx=global%Grid.WidthInCells;var yy=global/Grid.WidthInCells;
                    if(xx>=x && xx<x+Width && yy>=y && yy<y+Height)
                        Require((restored->properties[global]&NativeFlowMask)==NativeFlowMask && restored->mass[global]==ProjectionMass(xx-x) &&
                            restored->temperature[global]==ProjectionTemperature(xx-x),"Restored compatibility cells/flags differ.");
                    else Require(source->elementIdx[global]==restored->elementIdx[global] && source->mass[global]==restored->mass[global] &&
                        source->temperature[global]==restored->temperature[global] && source->diseaseIdx[global]==restored->diseaseIdx[global] &&
                        source->diseaseCount[global]==restored->diseaseCount[global] && source->properties[global]==restored->properties[global],
                        "Zero-time compatibility restoration changed an outside cell.");
                }
            }
            var initial=new byte[physical.Length];Check(CopyDomainSnapshot(latest.Session,0,initial,(uint)initial.Length,out var copied));
            Require(copied==physical.Length && initial.SequenceEqual(physical),"Native lifecycle restoration changed saved physical state or time.");
            restoredInitialHash=CheckpointHash(initial);zeroRestoreVerified=true;colonyLoaded=true;pendingColonyRestore=null;
            textureWriteBaseline=MixtureWorldRendering.TextureWrites;active=true;arming=false;
            ColonyRuntimeValidation.SetStage("restored-native-colony-awaiting-game-start");WriteReport();
        }
        private static bool NoMaterialEvents(Sim.GameDataUpdate* value)=>value->numSpawnFallingLiquidInfo==0 && value->numSpawnOreInfo==0 && value->numDigInfo==0;
        private static byte[] Packet(Action<BinaryWriter> write)
        {using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true)) write(writer);return stream.ToArray();}
        private static IntPtr SendRaw(int message,byte[] data)
        {fixed(byte* pointer=data) return Sim.SIM_HandleMessage(message,data.Length,pointer);}
        private static void SendZeroTimeFrame()=>SendRaw(-775326397,Packet(writer=>
        {writer.Write(0f);writer.Write(0);writer.Write(0);writer.Write(Grid.WidthInCells);writer.Write(Grid.HeightInCells);writer.Write(0f);writer.Write(0f);}));
        private static Sim.GameDataUpdate* WaitZeroTimeFrame()
        {
            var visibility=Enumerable.Repeat((byte)255,Grid.CellCount).ToArray();var timer=Stopwatch.StartNew();
            do
            {
                var pointer=(Sim.GameDataUpdate*)SendRaw(1078620451,visibility);
                if(pointer!=null && (pointer->numFramesProcessed>0 || OwnedGameDataSnapshot.HasTransientEvents(pointer)))
                {
                    Require(restoreNativeOutbox.Count<64,"Native restore outbox exceeded its bounded startup batches.");
                    restoreNativeOutbox.Add(OwnedGameDataSnapshot.Capture(pointer,Grid.WidthInCells,Grid.HeightInCells));
                }
                if(pointer!=null && pointer->numFramesProcessed>0) return pointer;
                Thread.Sleep(1);
            }
            while(timer.ElapsedMilliseconds<5000);
            throw new TimeoutException("Native zero-time restore frame did not complete.");
        }
        private static void PrependRestoredNativeEvents(OwnedGameDataSnapshot candidate)
        {for(var index=restoreNativeOutbox.Count-1;index>=0;--index) candidate.PrependNativeEventsFrom(restoreNativeOutbox[index]);}
        private static void ReleaseRestoredNativeEvents(bool delivered)
        {
            if(delivered) restoredNativeBatches+=restoreNativeOutbox.Count;
            foreach(var snapshot in restoreNativeOutbox) snapshot.Dispose();restoreNativeOutbox.Clear();
        }
        private static void VerifyRestoredPhysicalFrame()
        {
            if(!colonyLoaded || restoredExpected==null || latest.CompletedFrames==replayFrame) return;
            Require(latest.CompletedFrames==replayFrame+1 && latest.LastTimestepSeconds==(double).2f,"Restored worker observation skipped a physical frame or changed timestep.");
            var expected=ReplayCompletedFrameInputs(restoredExpected);
            // A live duplicant may already have reserved its next request.
            // Observe the immutable completed image; save admission continues
            // to require the separate drained checkpoint API.
            var actual=ReadCompletedPhysicalFrame();
            if(!actual.SequenceEqual(expected)) {
                File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory,"failed-continuation-expected.bin"),expected);
                File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory,"failed-continuation-actual.bin"),actual);
                throw new InvalidOperationException("Restored colony worker differs from exact saved continuation: expected "+CheckpointHash(expected)+", actual "+CheckpointHash(actual));
            }
            replayTime+=latest.LastTimestepSeconds;Require(latest.TimeSeconds==replayTime,"Restored colony reset or changed its physical clock.");
            ObserveReloadPhysicalFrame(actual);
            restoredExpected=expected;replayFrame=latest.CompletedFrames;++exactReplayComparisons;
        }
        internal static void BeforeRestoredNativeFrame(int message,int length,byte* payload)
        {
            if(message!=-775326397 || !ColonyRestoreMode || !colonyLoaded || completed || failed) return;
            Require(length>0 && length%sizeof(Sim.NewGameFrame)==0 && payload!=null,"Restored frame publication has an invalid payload.");
            var frames=(Sim.NewGameFrame*)payload;var seconds=frames[0].elapsedSeconds;
            for(var index=1;index<length/sizeof(Sim.NewGameFrame);++index)
                Require(frames[index].elapsedSeconds==seconds,"Restored active regions disagree on their physical interval.");
            var timer=Stopwatch.StartNew();
            while(replayFrame<restoredSubmittedFrames && timer.ElapsedMilliseconds<5000)
            {
                // Read through ONI's ordinary publication/callback path. The
                // preceding frame has already been submitted; queueing another
                // could retire its complete state before the game observes it.
                AccessTools.Method(typeof(Game),"StepTheSim").Invoke(Game.Instance,new object[]{0f});
                Capture();if(replayFrame<restoredSubmittedFrames) Thread.Sleep(1);
            }
            Require(replayFrame==restoredSubmittedFrames,"The preceding restored worker frame was not published before the next submission.");
            if(seconds>0)
            {
                Require(seconds==.2f,"Restored game changed its declared timestep.");
                restoredSubmittedFrames=checked(restoredSubmittedFrames+1);
            }
        }
        private static void TickRestoredColony()
        {
            Require(colonyLoaded && active && restoredExpected!=null,"Selected game did not restore a complete paired physical owner.");
            Capture();
            if(!RestoreTransientParticles())
            {Require(Time.realtimeSinceStartup-started<90,"Saved CO2 particles did not find their normal game pool.");WriteReport();return;}
            if(LiveBreathingRequired && !BindLiveBreather(true))
            {Require(Time.realtimeSinceStartup-started<90,"Saved live breather did not complete its normal spawn lifecycle.");WriteReport();return;}
            Require(latest.State==1 && BoundaryReady(),"Restored native colony boundary or owner failed.");
            FlushRestoredEmitterConfigurations();
            if(PendingActorRestores()!=0)
            {
                Require(Time.realtimeSinceStartup-started<90,"Saved point actors did not bind to their restored native components.");
                ColonyRuntimeValidation.SetStage("waiting-for-persistent-actor-bindings");WriteReport();return;
            }
            VerifyRestoredPersistentActors();
            if(!restoredOrbitalReferencesVerified)
            {
                Require(latest.CompletedFrames==0,"Native orbital references were not admitted before restored physical advancement.");
                if(!VerifyNativeOrbitalReferences("restored-before-positive-time",true))
                {
                    if(orbitalRestoreWaitStarted<0) orbitalRestoreWaitStarted=Time.realtimeSinceStartup;
                    Require(Time.realtimeSinceStartup-orbitalRestoreWaitStarted<90,"Restored native save roots did not finish their normal Unity spawn lifecycle.");
                    ++orbitalRestoreWaitInspections;orbitalMaximumPendingSaveRoots=Math.Max(orbitalMaximumPendingSaveRoots,orbitalPendingSaveRoots);
                    VerifyOrbitalRestoreHold();
                    ColonyRuntimeValidation.SetStage("waiting-for-native-world-save-roots");WriteReport();return;
                }
                if(orbitalRestoreWaitInspections>0) VerifyOrbitalRestoreHold();
                restoredOrbitalReferenceFrame=latest.CompletedFrames;restoredOrbitalReferenceTime=latest.TimeSeconds;
                restoredOrbitalReferencesVerified=true;
            }
            ObservePersistentEmitterStatus();
            if(!restoredSceneStarted)
            {
                Require(SaveLoader.Instance.loadedFromSave,"Restored validation was created as a new colony.");
                if(ColonyRuntimeValidation.Settings!.SaveBeforeResume && !frameZeroResaveVerified)
                {
                    if(!SpeedControlScreen.Instance.IsPaused) SpeedControlScreen.Instance.Pause(false);
                    if(resumePreparationFrame<0)
                    {
                        var accumulator=AccessTools.Field(typeof(Game),"simDt");var subtick=AccessTools.Field(typeof(Game),"simSubTick");
                        var beforeAccumulator=(float)accumulator.GetValue(Game.Instance);var beforeSubtick=(int)subtick.GetValue(Game.Instance);
                        AccessTools.Method(typeof(Game),"SimEveryTick").Invoke(Game.Instance,new object[]{.2f});Capture();
                        initialPauseChallengeVerified=latest.CompletedFrames==0 && latest.TimeSeconds==loadedSourceTime &&
                            (float)accumulator.GetValue(Game.Instance)==beforeAccumulator && (int)subtick.GetValue(Game.Instance)==beforeSubtick &&
                            TryCapturePhysicalCheckpoint(out var held) && CheckpointHash(held)==loadedSourceHash && initialPositiveTicksHeld>0;
                        Require(initialPauseChallengeVerified,"A positive latched timestep escaped the initial paused simulation gate.");
                        resumePreparationFrame=Time.frameCount;return;
                    }
                    if(Time.frameCount-resumePreparationFrame<3) return;
                    Require(latest.CompletedFrames==0 && latest.TimeSeconds==loadedSourceTime,"Initial resave advanced before its paused admission.");
                    var path=Path.Combine(ColonyRuntimeValidation.OutputDirectory,ReloadSaveName);
                    if(!frameZeroResaveScheduled)
                    {
                        Require(!File.Exists(path),"Initial resave destination already exists.");
                        frameZeroResaveScheduled=true;
                        Game.Instance.StartDelayedSave(path,true,false);
                        ColonyRuntimeValidation.SetStage("waiting-for-initial-deferred-autosave");return;
                    }
                    if(!colonyFileSaved) {WriteReport();return;}
                    frameZeroResaveVerified=colonyFileSaved && savedSourceFrame==0 && savedSourceTime==loadedSourceTime &&
                        savedPhysicalHash==loadedSourceHash && stagedSaveWrites==1 && automaticSaveAdmissions==1 &&
                        savedAsAutoSave && savedFilePath==path && savePauseStateRestored && SpeedControlScreen.Instance.IsPaused && stagedColonySave==null;
                    Require(frameZeroResaveVerified,"Ordinary save before resume changed the saved image/clock, reused a pair or unpaused the game.");
                }
                restoredSceneStarted=true;initialHot=Grid.Temperature[hotSolid];initialCold=Grid.Temperature[coldSolid];
                var center=Grid.CellToPosCCC(target,Grid.SceneLayer.Background);CameraController.Instance.SnapTo(center);
                CameraController.Instance.SetTargetPos(center,20,false);CameraController.Instance.OrthographicSize=7;
                UnityEngine.Object.FindFirstObjectByType<ColonyValidationDriver>().gameObject.AddComponent<NativeSealedDomainLabel>();
                SpeedControlScreen.Instance.Unpause(false);ColonyRuntimeValidation.SetStage("running-restored-native-colony");
            }
            nativeSolidsAdvanced=Grid.Temperature[hotSolid]<initialHot && Grid.Temperature[coldSolid]>initialCold;
            if(latest.CompletedFrames<(ulong)ColonyRuntimeValidation.Settings!.CompletedFrames) {WriteReport();return;}
            if(LiveBreathingRequired && !LiveBreathingDrained) {WriteReport();return;}
            if(rendererWaitFrame<0) {rendererWaitFrame=Time.frameCount;SpeedControlScreen.Instance.Pause(false);return;}
            if(Time.frameCount-rendererWaitFrame<3) return;
            var frame=MixturePresentationRuntime.AcquireFrame();
            restoredRendererVerified=frame!=null && !frame.IsFixture && frame.TryGetCell(target,out var reading) &&
                reading!.Revision==(long)latest.CompletedFrames && MixtureWorldRendering.TextureWrites>textureWriteBaseline &&
                MixtureWorldRendering.LastTexturePublicationGeneration==frame.PublicationGeneration && string.IsNullOrEmpty(MixtureWorldRendering.LastError);
            gameGridProjectionVerified=Enumerable.Range(0,cells.Length).All(index=>
            {
                var global=Grid.XYToCell(x+index%Width,y+index/Width);return Grid.Mass[global]==(float)cells[index].TotalMass &&
                    Grid.Temperature[global]==(float)cells[index].Temperature && Grid.Element[global].id==(Liquid ? SimHashes.Water : SimHashes.Oxygen);
            });
            nativeRendererAdvanced=restoredRendererVerified;
            if(LiveBreathingRequired) VerifyLiveBreathing();
            VerifySavedParticleDeposit();
            Require(exactReplayComparisons>=(int)ColonyRuntimeValidation.Settings.CompletedFrames && zeroRestoreVerified && restoredNativeBatches>=2 &&
                restoreNativeOutbox.Count==0 && nativeSnapshotVerified &&
                nativeCellsUnchanged && projectionPrimed && nativeFlowFenceVerified && retainedFrameVerified && nativeSolidsAdvanced &&
                restoredRendererVerified && gameGridProjectionVerified && latest.MaximumRelativeMassError<=1e-8 && latest.MaximumRelativeEnergyError<=1e-6,
                "Restored actual colony failed its continuation, publication, rendering or physical accounts.");
            VerifySaveGuard();VerifyRestoredReaderBindings();VerifyNativeGpuGasTextures();VerifyRestoredPersistentActorCompletion();
            if(ViscousMode) Require(ViscosityVerified,"Restored native worker did not continue physical viscosity and represented heating.");
            completed=true;NativeBackendObservation.Flush();WriteReport();
            ColonyRuntimeValidation.CaptureScreenshot("native-domain.png");
            ColonyRuntimeValidation.CompletePresentation("actual-game-restored-native-domain-validation");
        }
        private static object DeferredSaveDiagnostics()
        {
            var game=Game.Instance;
            var pre=game==null ? null : AccessTools.Field(typeof(Game),"activatePreCB").GetValue(game) as Delegate;
            var indicator=pre?.Target as SaveActive;
            var animation=indicator==null ? null : AccessTools.Field(typeof(SaveActive),"controller").GetValue(indicator) as KBatchedAnimController;
            object? Field(string name)=>animation==null ? null : AccessTools.Field(typeof(KAnimControllerBase),name)?.GetValue(animation);
            return new {Frame=Time.frameCount,ScaledDelta=Time.deltaTime,UnscaledDelta=Time.unscaledDeltaTime,TimeScale=Time.timeScale,
                UnscaledTime=Time.unscaledTimeAsDouble,Realtime=Time.realtimeSinceStartupAsDouble,
                CaptureDelta=Time.captureDeltaTime,MaximumDelta=Time.maximumDeltaTime,
                MonotonicTicks=Stopwatch.GetTimestamp(),MonotonicFrequency=Stopwatch.Frequency,
                UtcTicks=System.DateTime.UtcNow.Ticks,
                GameActive=game!=null && game.gameObject.activeInHierarchy,GamePaused=game==null ? null : AccessTools.Field(typeof(Game),"IsPaused").GetValue(game),
                HasFirstSimTick=game==null ? null : AccessTools.Field(typeof(Game),"hasFirstSimTickRun").GetValue(game),
                IsDragging=PlayerController.Instance!=null && PlayerController.Instance.IsDragging(),
                PreCallback=pre?.Method.DeclaringType?.FullName+"."+pre?.Method.Name,
                IndicatorPresent=indicator!=null,IndicatorActive=indicator!=null && indicator.gameObject.activeInHierarchy,
                IndicatorSpawned=indicator!=null && indicator.isSpawned,
                AnimationCallbackPending=indicator!=null && AccessTools.Field(typeof(SaveActive),"readyForSaveCallback").GetValue(indicator)!=null,
                AnimationPresent=animation!=null,AnimationEnabled=animation!=null && animation.enabled,
                AnimationActive=animation!=null && animation.gameObject.activeInHierarchy,
                AnimationAwake=Field("hasAwakeRun"),AnimationHasEnabled=Field("hasEnableRun"),
                AnimationName=animation==null ? "" : animation.currentAnim.ToString(),
                AnimationElapsed=Field("elapsedTime"),AnimationStopped=Field("stopped"),AnimationVisible=Field("isVisible"),
                AnimationUsesGameTime=Field("forceUseGameTime"),AnimationMode=Field("mode")?.ToString()};
        }
        private static object ColonyCheckpointReport()=>new {Saved=colonyFileSaved,Loaded=colonyLoaded,ZeroTimeRestoreVerified=zeroRestoreVerified,
            ManagedSceneRestored=restoredSceneStarted,RendererVerified=restoredRendererVerified,SimSaveWrites=stagedSaveWrites,
            AutomaticSaveAdmissions=automaticSaveAdmissions,SavePauseStateRestored=savePauseStateRestored,
            SavedAsAutoSave=savedAsAutoSave,
            SavedFileReadbackVerified=savedFileReadbackVerified,SerializedBodyCaptures=serializedBodyCaptures,
            SerializedBodyBytes=serializedBodyBytes,SerializedBodySha256=serializedBodyHash,
            SerializedHeaderCaptures=serializedHeaderCaptures,SerializedHeaderSha256=CheckpointHash(serializedHeaderJson),
            SavePauseDepthBefore=savePauseDepthBefore,SavePauseDepthAfter=savePauseDepthAfter,
            PendingSaveRejectionRequired=SaveRejectionRequired,PendingSaveRejectionVerified=rejectedSaveVerified,
            SaveProblems=SaveProblemReport(),
            SourceFrame=colonyLoaded ? loadedSourceFrame : savedSourceFrame,SourceTime=colonyLoaded ? loadedSourceTime : savedSourceTime,
            SourcePhysicalSha256=colonyLoaded ? loadedSourceHash : savedPhysicalHash,RestoredInitialSha256=restoredInitialHash,
            SavedFrame=savedSourceFrame,SavedTime=savedSourceTime,SavedPhysicalSha256=savedPhysicalHash,
            FrameZeroResaveRequired=ColonyRuntimeValidation.Settings?.SaveBeforeResume==true,FrameZeroResaveVerified=frameZeroResaveVerified,
            FrameZeroResaveUsedGameDelayedSave=frameZeroResaveScheduled,
            InitialPauseChallengeVerified=initialPauseChallengeVerified,InitialPauseTicksHeld=initialPauseTicksHeld,
            InitialPositiveTicksHeld=initialPositiveTicksHeld,
            DeferredSaveStartup=frameZeroResaveScheduled && !frameZeroResaveVerified ? DeferredSaveDiagnostics() : null,
            ExactContinuationFrames=exactReplayComparisons,ReplayedFrame=replayFrame,ReplayedTime=replayTime,NativeStartupBatchesDelivered=restoredNativeBatches,
            FrameInputReplay=FrameInputReplayReport(),
            CurrentPhysicalSha256=CheckpointHash(restoredExpected),SavedFile=savedFilePath,SavedFileSha256=savedFileHash,
            NativeColonyId=SaveLoader.Instance?.GameInfo.colonyGuid.ToString("N"),WholeColonyFluidCheckpoint=false};
    }
    [HarmonyPatch]
    internal static class NativeDomainColonyLoadPatch
    {
        private static IEnumerable<MethodBase> TargetMethods()=>new[]{AccessTools.Method(typeof(Sim),nameof(Sim.LoadWorld)),AccessTools.Method(typeof(Sim),nameof(Sim.Load))};
        private static bool Prefix(IReader __0,ref int __result)=>!NativeSealedDomainValidation.TryLoadPairedColony(__0,ref __result);
    }
    [HarmonyPatch(typeof(Sim),nameof(Sim.Start))]
    internal static class NativeDomainColonyStartPatch
    {private static void Postfix()=>NativeSealedDomainValidation.RestoreAfterNativeStart();}
    [HarmonyPatch(typeof(SpeedControlScreen),nameof(SpeedControlScreen.Unpause))]
    internal static class NativeDomainInitialRestorePausePatch
    {
        private static void Postfix()
        {
            if(NativeSealedDomainValidation.HoldValidationSimulationTick && !SpeedControlScreen.Instance.IsPaused)
                SpeedControlScreen.Instance.Pause(false);
        }
    }
    [HarmonyPatch(typeof(Game),"SimEveryTick")]
    internal static class NativeDomainInitialRestoredTickPatch
    {
        private static bool Prefix(Game __instance,float __0)
        {
            if(!NativeSealedDomainValidation.HoldValidationSimulationTick) return true;
            NativeSealedDomainValidation.PublishPausedValidationTick(__instance,__0);return false;
        }
    }
    [HarmonyPatch(typeof(SaveLoader),nameof(SaveLoader.Save),new[]{typeof(string),typeof(bool),typeof(bool)})]
    internal static class NativeDomainAutomaticSavePatch
    {
        private static bool Prefix(bool __1,ref string __result,out NativeSealedDomainValidation.ColonySaveLease? __state)
        {
            __state=null;if(!NativeSealedDomainValidation.Active) return true;
            try {__state=NativeSealedDomainValidation.BeginAutomaticColonySave(__1);return true;}
            catch(NativeSealedDomainValidation.ColonySaveUnavailableException problem)
            {NativeSealedDomainValidation.DeclineColonySave(problem);__result=SaveLoader.GetActiveSaveFilePath();return false;}
            catch(Exception problem) when(problem is InvalidDataException || problem is NotSupportedException)
            {NativeSealedDomainValidation.DeclineColonySave(new NativeSealedDomainValidation.ColonySaveUnavailableException(problem.Message,false));__result=SaveLoader.GetActiveSaveFilePath();return false;}
        }
        private static void Postfix(string __0,ref string __result,NativeSealedDomainValidation.ColonySaveLease? __state)
        {
            if(__state==null) return;
            try {__state.CompleteFile(__0,__result);}
            catch(Exception problem) when(problem is IOException || problem is UnauthorizedAccessException || problem is InvalidDataException ||
                problem is NotSupportedException || problem is SavedInspectionException)
            {__state.RecordWriteProblem(problem);__result=__state.PreviousSavePath;}
        }
        private static Exception? Finalizer(Exception? __exception,NativeSealedDomainValidation.ColonySaveLease? __state)
        {
            try {__state?.Finish(__exception==null && __state.FileAccepted);}
            catch(Exception failure) {return __exception ?? failure;}
            return __exception;
        }
    }
    [HarmonyPatch]
    internal static class NativeDomainSerializedBodyPatch
    {
        private static MethodBase TargetMethod()=>AccessTools.Method(typeof(SaveLoader),"Save",new[]{typeof(BinaryWriter)});
        private static void Postfix(BinaryWriter __0)=>NativeSealedDomainValidation.CaptureSerializedColonyBody(__0);
    }
    [HarmonyPatch(typeof(SaveGame),nameof(SaveGame.GetSaveHeader))]
    internal static class NativeDomainSerializedHeaderPatch
    {
        private static void Postfix(byte[] __result)=>NativeSealedDomainValidation.CaptureSerializedColonyHeader(__result);
    }
}
