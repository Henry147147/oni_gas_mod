using System;
using System.Diagnostics;
using System.IO;
using MixedAtmospheres.Oni;

internal static class DevelopmentTelemetryTests
{
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        var tick=Stopwatch.Frequency/1000; // One millisecond of the same monotonic clock the game hooks use.
        bool Near(double? value,double expected)=>value.HasValue && Math.Abs(value.Value-expected)<=1e-9*Math.Max(1,Math.Abs(expected));
        const int Prepare=DevelopmentTelemetrySampler.PrepareGameDataMessage, Submit=DevelopmentTelemetrySampler.NewGameFrameMessage;
        test("telemetry latency windows report nearest-rank percentiles, exact maxima and reset per interval",()=> {
            var window=new LatencyWindow(128);
            for(var value=100;value>=1;--value) window.Add(value);
            var summary=window.TakeSummary();
            check(summary.Samples==100 && summary.Retained==100 && summary.P50==50 && summary.P95==95 && summary.Max==100 && summary.Mean==50.5);
            var empty=window.TakeSummary();
            check(empty.Samples==0 && empty.Retained==0 && empty.P50==null && empty.P95==null && empty.Max==null && empty.Mean==null);
            window.Add(7);var single=window.TakeSummary();check(single.P50==7 && single.P95==7 && single.Max==7 && single.Mean==7);
            window.Add(0);check(window.TakeSummary().Max==0);
            reject(()=>window.Add(double.NaN));reject(()=>window.Add(-1e-12));reject(()=>window.Add(double.PositiveInfinity));
            check(window.Count==0);reject(()=>new LatencyWindow(0));reject(()=>new LatencyWindow((1<<20)+1));
        });
        test("telemetry latency windows keep fixed memory while count, mean and maximum cover every sample",()=> {
            var window=new LatencyWindow(4);
            foreach(var value in new[]{1000.0,1,2,3,4,5}) window.Add(value);
            var summary=window.TakeSummary();
            check(summary.Samples==6 && summary.Retained==4 && summary.Max==1000 && Near(summary.Mean,1015.0/6) && summary.P50==3 && summary.P95==5);
            var large=new LatencyWindow();
            for(var index=0;index<100000;++index) large.Add(index%1000);
            var bounded=large.TakeSummary();
            check(large.Capacity==LatencyWindow.DefaultCapacity && bounded.Samples==100000 && bounded.Retained==LatencyWindow.DefaultCapacity && bounded.Max==999);
        });
        test("telemetry sampler times only native frame submit and wait boundaries and counts processed frames while active",()=> {
            var sampler=new DevelopmentTelemetrySampler(16);
            sampler.RecordNativeMessage(Prepare,0,tick,2*tick,1);sampler.RecordUnityFrame(16);
            check(!sampler.Active && sampler.NativeFramesProcessed==0);
            sampler.Start();check(sampler.Active);
            sampler.RecordNativeMessage(Submit,0,3*tick,9*tick,9);
            sampler.RecordNativeMessage(Prepare,10*tick,15*tick,17*tick,1);
            sampler.RecordNativeMessage(Prepare,20*tick,21*tick,25*tick,2);
            sampler.RecordNativeMessage(818320644,0,50*tick,60*tick,7);
            sampler.RecordNativeMessage(Prepare,30*tick,29*tick,31*tick,0);
            var interval=sampler.TakeInterval();
            check(interval.NativeFramesProcessed==3 && interval.IntervalNativeFrames==3 && interval.InvalidSamples==1);
            check(interval.NativeFrameSubmitMs.Samples==1 && Near(interval.NativeFrameSubmitMs.Max,3));
            check(interval.NativeFrameWaitMs.Samples==2 && Near(interval.NativeFrameWaitMs.P50,1) && Near(interval.NativeFrameWaitMs.Max,5));
            check(interval.ManagedPublicationMs.Samples==3 && Near(interval.ManagedPublicationMs.P50,2) && Near(interval.ManagedPublicationMs.Max,4));
            check(interval.UnityFrameMs.Samples==0 && interval.TextureBatchMs.Samples==0 && interval.PresentationLagMs.Samples==0);
            var next=sampler.TakeInterval();
            check(next.NativeFramesProcessed==3 && next.IntervalNativeFrames==0 && next.NativeFrameWaitMs.Samples==0 && next.InvalidSamples==0);
            sampler.Stop();sampler.RecordNativeMessage(Prepare,0,tick,tick,4);check(!sampler.Active && sampler.NativeFramesProcessed==3);
            sampler.Start();check(sampler.NativeFramesProcessed==0 && sampler.TakeInterval().NativeFrameWaitMs.Samples==0);
        });
        test("telemetry presentation lag runs from the oldest unrendered native publication to its first texture batch",()=> {
            var sampler=new DevelopmentTelemetrySampler(16);sampler.Start();
            sampler.ObservePublication(5,10*tick);sampler.ObservePublication(6,12*tick);
            sampler.ObserveTextureBatch(4,13*tick,14*tick);
            sampler.ObserveTextureBatch(6,20*tick,23*tick);
            sampler.ObserveTextureBatch(6,30*tick,31*tick);
            var interval=sampler.TakeInterval();
            check(interval.TextureBatchMs.Samples==3 && Near(interval.TextureBatchMs.Max,3) && Near(interval.TextureBatchMs.P50,1));
            check(interval.PresentationLagMs.Samples==1 && Near(interval.PresentationLagMs.Max,10));
            sampler.RecordUnityFrame(16.5);sampler.RecordUnityFrame(double.NaN);sampler.RecordUnityFrame(-1);
            var frames=sampler.TakeInterval();check(frames.UnityFrameMs.Samples==1 && frames.UnityFrameMs.Max==16.5 && frames.InvalidSamples==2);
            sampler.Stop();sampler.ObservePublication(9,40*tick);sampler.Start();sampler.ObserveTextureBatch(9,41*tick,42*tick);
            check(sampler.TakeInterval().PresentationLagMs.Samples==0);
        });
        test("telemetry lines follow native frame intervals with a wall-clock heartbeat",()=> {
            check(DevelopmentTelemetrySampler.Due(299,0,300,59.9,0,60)==null);
            check(DevelopmentTelemetrySampler.Due(300,0,300,1,0,60)=="interval");
            check(DevelopmentTelemetrySampler.Due(10,0,300,60,0,60)=="heartbeat");
            check(DevelopmentTelemetrySampler.Due(900,600,300,500,0,60)=="interval");
            check(DevelopmentTelemetrySampler.Due(899,600,300,119,60,60)==null);
            // A maximal long run stays well inside the journal's line limit.
            check(ColonyValidationLimits.MaximumLongRunFrames/ColonyValidationLimits.MinimumTelemetryIntervalFrames+
                ColonyValidationLimits.MaximumLongRunTimeoutSeconds/DevelopmentTelemetry.HeartbeatSeconds+3<DevelopmentTelemetryJournal.DefaultMaximumLines);
        });
        test("telemetry reads Linux resident memory fields in bytes and rejects malformed units",()=> {
            DevelopmentTelemetry.ParseProcessStatus(new[]{"Name:\tOxygenNotIncluded","VmHWM:\t  2048 kB","VmRSS:\t    1024 kB","Threads:\t40"},out var resident,out var peak);
            check(resident==1024L*1024 && peak==2048L*1024);
            DevelopmentTelemetry.ParseProcessStatus(new[]{"Name:\tother"},out resident,out peak);check(resident==null && peak==null);
            reject(()=>DevelopmentTelemetry.ParseProcessStatus(new[]{"VmRSS:\t12 MB"},out _,out _));
            reject(()=>DevelopmentTelemetry.ParseProcessStatus(new[]{"VmRSS:\t-12 kB"},out _,out _));
        });
        test("telemetry journal appends flushed single-line JSON objects, keeps earlier sessions and bounds its lines",()=> {
            var directory=Path.Combine(Path.GetTempPath(),"ma-development-telemetry-"+Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            try
            {
                var path=Path.Combine(directory,DevelopmentTelemetry.FileName);
                File.WriteAllText(path,"{\"Earlier\":true}\n");
                using(var journal=new DevelopmentTelemetryJournal(path,3))
                {
                    check(journal.Append("{\"Kind\":\""+DevelopmentTelemetry.Kind+"\",\"Text\":\"two\\nlines\",\"Value\":1.5}"));
                    // Flushed: an independent reader sees the complete line while the journal stays open.
                    using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))
                    using(var reader=new StreamReader(stream))
                    {
                        var lines=reader.ReadToEnd().Split('\n');
                        check(lines.Length==3 && lines[2]=="" && lines[1].StartsWith("{\"Kind\":\"mixed-atmospheres-development-telemetry\"",StringComparison.Ordinal));
                    }
                    foreach(var invalid in new[]{"","{\"A\":1}\n","{\"A\":\r1}","[1]","{\"A\":1} ","\"text\""}) reject(()=>journal.Append(invalid));
                    reject(()=>journal.Append(null!));
                    reject(()=>journal.Append("{\"Large\":\""+new string('x',DevelopmentTelemetryJournal.MaximumLineBytes)+"\"}"));
                    reject(()=>journal.Append("{\"Unpaired\":\"\ud800\"}"));
                    check(journal.Lines==1 && journal.Append("{\"Sequence\":2,\"Text\":\"\u00b0C\"}") && journal.Append("{\"Sequence\":3}"));
                    check(!journal.Append("{\"Sequence\":4}") && journal.Truncated && journal.Lines==3);
                }
                var all=File.ReadAllLines(path);
                check(all.Length==4 && all[0]=="{\"Earlier\":true}" && all[2]=="{\"Sequence\":2,\"Text\":\"\u00b0C\"}" && all[3]=="{\"Sequence\":3}");
                var closed=new DevelopmentTelemetryJournal(path);closed.Dispose();reject(()=>closed.Append("{\"Sequence\":5}"));
                check(File.ReadAllLines(path).Length==4);
                reject(()=>new DevelopmentTelemetryJournal(path,0));
            }
            finally {Directory.Delete(directory,true);}
        });
    }
}
