using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using MixedAtmospheres.Oni;
var managed=Path.Combine(args[0],"OxygenNotIncluded_Data/Managed");
AssemblyLoadContext.Default.Resolving+=(_,n)=>{var p=Path.Combine(managed,n.Name+".dll");return File.Exists(p)?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;};
return Proof.Run();
static class Proof
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    internal static int Run()
    {
        var checks=0;void Check(bool ok,string why){++checks;if(!ok)throw new Exception(why);}
        var directory=Path.Combine(AppContext.BaseDirectory,"ma-report-json-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(directory);
        try
        {
            var path=Path.Combine(directory,"report.json");
            var values=Enumerable.Range(0,8).Select(i=>"{\"writer\":"+i+",\"payload\":\""+new string((char)('a'+i),1024*1024)+"\"}").ToArray();
            ReportJson.WriteAtomicText(path,values[0]);
            using var start=new Barrier(values.Length);
            var tasks=values.Select(value=>Task.Run(()=>{start.SignalAndWait();for(var k=0;k<8;++k)ReportJson.WriteAtomicText(path,value);})).ToArray();
            Task.WaitAll(tasks);Check(values.Contains(File.ReadAllText(path)),"concurrent writers published a mixed or truncated document");
            Check(!File.Exists(path+".tmp"),"concurrent temporary file leaked");
            var reads=0;var invalid=0;using var stop=new CancellationTokenSource();using var ready=new ManualResetEventSlim();
            var reader=Task.Run(()=>{while(!stop.IsCancellationRequested){using var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);using var input=new StreamReader(stream);var value=input.ReadToEnd();if(!values.Contains(value))Interlocked.Increment(ref invalid);Interlocked.Increment(ref reads);ready.Set();}});
            try{Check(ready.Wait(TimeSpan.FromSeconds(10)),"reader did not start");for(var k=0;k<32;++k)ReportJson.WriteAtomicText(path,values[k%values.Length]);}
            finally{stop.Cancel();reader.GetAwaiter().GetResult();}
            Check(reads>0&&invalid==0,"external-style readers observed an incomplete replacement");
            Check(!File.Exists(path+".tmp"),"reader phase temporary file leaked");
            var blocked=Path.Combine(directory,"directory-as-target");Directory.CreateDirectory(blocked);
            var failed=false;try{ReportJson.WriteAtomicText(blocked,values[0]);}catch(IOException){failed=true;}catch(UnauthorizedAccessException){failed=true;}
            Check(failed&&!File.Exists(blocked+".tmp")&&Directory.Exists(blocked),"failed replacement did not retain target and clean temporary file");
            ReportJson.WriteAtomicText(path,values[1]);Check(File.ReadAllText(path)==values[1],"writer lock did not recover after exception");
            var other=Path.Combine(directory,"other.json");
            Parallel.Invoke(()=>ReportJson.WriteAtomicText(path,values[2]),()=>ReportJson.WriteAtomicText(other,values[3]));
            Check(File.ReadAllText(path)==values[2]&&File.ReadAllText(other)==values[3],"independent report paths interfered");
            Console.WriteLine($"Atomic report writes: {checks} controls PASS; 64 simultaneous-start writes, 32 replacements with concurrent readers and failure recovery.");return 0;
        }
        finally{Directory.Delete(directory,true);}
    }
}
