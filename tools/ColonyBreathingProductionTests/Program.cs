using System.Text.Json;
using A = MixedAtmospheres.Oni.ColonyBreathingCo2Accounting;

static class Program
{
    static int assertions,cases;
    static A.Source Key(int id=1,long life=101)=>new(id,life);
    static A.Sample S(int id,double held,long life=101)=>new(Key(id,life),held);
    static A.Window W(double held=0)=>new(1,new[]{S(1,held)});
    static void Check(bool value,string why){++assertions;if(!value)throw new Exception(why);}
    static void Throws<T>(Action action) where T:Exception
    { bool caught=false;try{action();}catch(T){caught=true;}Check(caught,"Expected "+typeof(T).Name); }
    static void Case(string name,Action action){action();++cases;Console.WriteLine("PASS "+name);}
    static void Invalid(A.Result result,string why)=>Check(!result.Valid&&!result.EverySourcePositive&&result.Error.Length>0,why);
    static void Controls()
    {
        Case("zero and positive held production",()=>{
            var zero=W().Complete(1,new[]{S(1,0)});Check(zero.Valid&&!zero.EverySourcePositive&&zero.Rows[0].ProducedKg==0,"Zero production passed");
            var positive=W().Complete(1,new[]{S(1,.125)});Check(positive.Valid&&positive.EverySourcePositive&&positive.Rows[0].ProducedKg==.125,"Held production lost");
            var negative=W(.25).Complete(1,new[]{S(1,.125)});Check(negative.Valid&&!negative.EverySourcePositive&&negative.Rows[0].ProducedKg==-.125,"Negative production was clamped");
        });
        Case("held reset plus actual puff, without counting old held CO2",()=>{
            var window=W(.25);Check(window.OriginalPuff(1,1,Key(),true,true,.25),"Scoped puff rejected");
            var result=window.Complete(1,new[]{S(1,.125)});Check(result.EverySourcePositive&&result.Rows[0].ProducedKg==.125,"Reset lost production");
            var oldOnly=W(.25);oldOnly.OriginalPuff(1,1,Key(),true,true,.25);
            var none=oldOnly.Complete(1,new[]{S(1,0)});Check(none.Valid&&!none.EverySourcePositive&&none.Rows[0].ProducedKg==0,"Pre-window CO2 credited as production");
        });
        Case("every original source, rather than positive aggregate",()=>{
            var window=new A.Window(1,new[]{S(1,0),S(2,0,202)});
            var result=window.Complete(1,new[]{S(1,.25),S(2,0,202)});
            Check(result.Valid&&!result.EverySourcePositive&&result.Rows.Sum(r=>r.ProducedKg)>.0,"One idle source hidden by another");
        });
        Case("source identity, duplicate and lifecycle guards",()=>{
            Throws<ArgumentException>(()=>new A.Window(1,new[]{S(1,0),S(1,0)}));
            Throws<ArgumentException>(()=>new A.Window(1,new[]{S(1,0),S(1,0,999)}));
            Throws<ArgumentException>(()=>new A.Window(1,new[]{S(-1,0)}));
            Throws<ArgumentException>(()=>new A.Window(1,new[]{S(1,0,0)}));
            Throws<ArgumentException>(()=>new A.Window(0,new[]{S(1,0)}));
            Throws<ArgumentException>(()=>new A.Window(1,Array.Empty<A.Sample>()));
            Invalid(W().Complete(1,new[]{S(1,.1,999)}),"Replaced component passed");
            Invalid(W().Complete(1,new[]{S(2,.1)}),"Wrong source passed");
            Invalid(W().Complete(1,Array.Empty<A.Sample>()),"Missing source passed");
            Invalid(W().Complete(1,new[]{S(1,.1),S(1,.1)}),"Duplicate final source passed");
            Invalid(W().Complete(1,new[]{S(1,.1),S(2,.1,202)}),"Changed cohort passed");
            Invalid(W().Complete(2,new[]{S(1,.1)}),"Changed session passed");
        });
        Case("only complete exact original attribution is credited",()=>{
            foreach(var kind in Enumerable.Range(0,5))
            {
                var window=W();
                var accepted=window.OriginalPuff(1,kind==4?2UL:1UL,kind==0?null:kind==1?Key(2):Key(),kind!=2,kind!=3,.125);
                Check(!accepted,"Bad attribution accepted");Invalid(window.Complete(1,new[]{S(1,.125)}),"Held gain hid bad observation");
            }
            var replaced=W();Check(!replaced.OriginalPuff(1,1,Key(1,999),true,true,.125),"Wrong puff lifecycle accepted");
            Invalid(replaced.Complete(1,new[]{S(1,.125)}),"Wrong puff lifecycle passed");
        });
        Case("unique events and closed windows",()=>{
            var duplicate=W();Check(duplicate.OriginalPuff(1,1,Key(),true,true,.125),"First event rejected");
            Check(!duplicate.OriginalPuff(1,1,Key(),true,true,.125),"Duplicate event credited");Invalid(duplicate.Complete(1,new[]{S(1,0)}),"Duplicate passed");
            var missing=W();Check(!missing.OriginalPuff(0,1,Key(),true,true,.1),"Missing event ID credited");Invalid(missing.Complete(1,new[]{S(1,.1)}),"Missing event ID passed");
            var window=W();var result=window.Complete(1,new[]{S(1,.125)});
            Throws<InvalidOperationException>(()=>window.OriginalPuff(1,1,Key(),true,true,.5));
            Throws<InvalidOperationException>(()=>window.Complete(1,new[]{S(1,.5)}));
            Check(result.Rows[0].OriginalPuffKg==0&&result.Rows[0].ProducedKg==.125,"Late event changed sealed result");
            var across=new A.Window(1,new[]{S(1,0),S(2,0,202)});across.OriginalPuff(1,1,Key(),true,true,.125);
            Check(!across.OriginalPuff(1,1,Key(2,202),true,true,.125),"One event credited to two sources");
        });
        Case("invalid values and reporting overflow fail closed",()=>{
            foreach(var value in new[]{-.1,double.NaN,double.PositiveInfinity,double.NegativeInfinity})
            {
                Throws<ArgumentException>(()=>new A.Window(1,new[]{S(1,value)}));
                Invalid(W().Complete(1,new[]{S(1,value)}),"Invalid final held CO2 passed");
                var puff=W();Check(!puff.OriginalPuff(1,1,Key(),true,true,value),"Invalid puff accepted");Invalid(puff.Complete(1,new[]{S(1,.1)}),"Invalid puff hidden");
                Throws<ArgumentException>(()=>A.Produced(0,.1,value));
            }
            var zero=W();Check(!zero.OriginalPuff(1,1,Key(),true,true,0),"Zero original puff accepted");
            Throws<OverflowException>(()=>A.Produced(0,double.MaxValue,double.MaxValue));
            var overflow=W();overflow.OriginalPuff(1,1,Key(),true,true,double.MaxValue);
            Check(!overflow.OriginalPuff(2,1,Key(),true,true,double.MaxValue),"Puff sum overflow accepted");
            Invalid(overflow.Complete(1,new[]{S(1,0)}),"Puff sum overflow passed");
            var finalOverflow=W();finalOverflow.OriginalPuff(1,1,Key(),true,true,double.MaxValue);
            Invalid(finalOverflow.Complete(1,new[]{S(1,double.MaxValue)}),"Production overflow passed");
        });
        Case("missing reset attribution cannot pass from another source",()=>{
            var window=new A.Window(1,new[]{S(1,.25),S(2,0,202)});
            var result=window.Complete(1,new[]{S(1,0),S(2,1,202)});
            Check(result.Valid&&!result.EverySourcePositive&&result.Rows[0].ProducedKg==-.25,"Missing puff was forgiven");
            var invalid=W();invalid.Invalidate("source destroyed");Invalid(invalid.Complete(1,new[]{S(1,.125)}),"Lifecycle invalidation was ignored");
        });
    }
    static void Evidence(string sourcePath,string finalPath,string puffPath,string output)
    {
        using var source=JsonDocument.Parse(File.ReadAllText(sourcePath));using var final=JsonDocument.Parse(File.ReadAllText(finalPath));
        var s=source.RootElement;var f=final.RootElement;
        Check(s.GetProperty("Session").GetUInt64()==f.GetProperty("Session").GetUInt64(),"Evidence session changed");
        var first=s.GetProperty("NativeFrame").GetUInt64();var last=f.GetProperty("NativeFrame").GetUInt64();
        var starts=s.GetProperty("Breathers").EnumerateArray().ToDictionary(x=>x.GetProperty("Id").GetInt32());
        var ends=f.GetProperty("Breathers").EnumerateArray().ToDictionary(x=>x.GetProperty("Id").GetInt32());
        Check(starts.Count==3&&starts.Keys.Order().SequenceEqual(ends.Keys.Order()),"V4 source cohort differs");
        var events=File.ReadLines(puffPath).Where(x=>!string.IsNullOrWhiteSpace(x)).Select(x=>JsonDocument.Parse(x)).ToArray();
        try
        {
            var inWindow=events.Select(x=>x.RootElement).Where(x=>x.GetProperty("NativeFrame").GetUInt64()>first&&x.GetProperty("NativeFrame").GetUInt64()<=last).ToArray();
            Check(inWindow.Length==0,"V4 no-puff window evidence changed");
            var rows=new List<object>();var positive=true;
            foreach(var pair in starts.OrderBy(x=>x.Key))
            {
                var before=pair.Value;var after=ends[pair.Key];
                // The actual fields are binary32; promote those exact values,
                // rather than interpreting their JSON decimals as stored doubles.
                var start=before.GetProperty("accumulatedCO2").GetSingle();var end=after.GetProperty("accumulatedCO2").GetSingle();
                Check(before.GetProperty("Emits").GetBoolean()&&after.GetProperty("Emits").GetBoolean(),"Provider emission evidence changed");
                Check(before.GetProperty("Spawned").GetDouble()==0&&after.GetProperty("Spawned").GetDouble()==0,"V4 puff totals changed");
                var produced=A.Produced((double)start,(double)end,0);positive &= produced>0;
                rows.Add(new{Id=pair.Key,InitialHeldKg=(double)start,FinalHeldKg=(double)end,InitialHeldBits=BitConverter.SingleToInt32Bits(start).ToString("x8"),FinalHeldBits=BitConverter.SingleToInt32Bits(end).ToString("x8"),OriginalPuffKg=0,ProducedKg=produced,Positive=produced>0,MinimumPuffKg=(double)after.GetProperty("minCO2ToEmit").GetSingle()});
            }
            Check(positive,"Observed V4 per-source mass criterion is not positive");
            var emitted=events.Select(x=>x.RootElement).Where(x=>x.GetProperty("OriginalBreather").GetBoolean()).ToArray();
            var earliest=emitted.Min(x=>x.GetProperty("NativeFrame").GetUInt64());Check(earliest==51&&earliest>last,"First V4 original puff evidence changed");
            double Co2(JsonElement e)=>e.GetProperty("Views").EnumerateArray().Single(x=>x.GetProperty("Element").GetString()=="CarbonDioxide").GetProperty("OwnedViewKg").GetDouble();
            var report=new{Kind="PRIVATE proposed CO2-production formula evaluation, not an existing-gate pass",SourceNativeFrame=first,FinalNativeFrame=last,SourceViewFrame=s.GetProperty("ViewFrame").GetUInt64(),FinalViewFrame=f.GetProperty("ViewFrame").GetUInt64(),OriginalScopedPuffsInsideWindow=inWindow.Length,FirstLaterOriginalPuffNativeFrame=earliest,OriginalGlobalNetCo2Kg=Co2(f)-Co2(s),EveryObservedSourceFormulaPositive=positive,Sources=rows,LifecycleReferenceGuardRetrospectivelyProved=false,Limit="V4 sidecars contain prefab IDs but no component lifetime token/reference. This evaluates the exact helper formula on observed rows; live source-reference guards are tested separately. No gate is reclassified."};
            File.WriteAllText(output,JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true})+"\n");
            Console.WriteLine("PASS actual V4 formula: three positive source rows, no scoped puffs before41; later51+ events excluded; original gate remains failed");
        }
        finally{foreach(var item in events)item.Dispose();}
    }
    static int Main(string[] args)
    {
        try
        {
            Controls();
            if(args.Length==4)Evidence(args[0],args[1],args[2],args[3]);else Check(args.Length==0,"Use source.json final.json puffs.jsonl output.json");
            Console.WriteLine($"PASS {cases} compiled control groups; {assertions} assertions");return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
}
