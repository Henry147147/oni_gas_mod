using System;
using System.IO;
using System.Linq;
using System.Reflection;
using MixedAtmospheres.Oni;

// The game's Newtonsoft build needs Unity's CAS facades at runtime, so these
// engine-free checks construct requests directly. The runner and visual-demo
// keys are compared with these declared fields by the Python suite.
internal static class ColonyValidationLimitsTests
{
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        string Message(System.Action action)
        {
            try {action();} catch(InvalidDataException error) {return error.Message;}
            throw new InvalidOperationException("Expected a configuration rejection");
        }
        ColonyValidationConfiguration Request(string mode,int frames,int timeout,bool longRun=false,int interval=ColonyValidationLimits.DefaultTelemetryIntervalFrames)=>
            new ColonyValidationConfiguration{Mode=mode,CompletedFrames=frames,TimeoutSeconds=timeout,LongRun=longRun,TelemetryIntervalFrames=interval,
                Floor=mode=="colony-vanilla"?"f1":""};
        ColonyValidationConfiguration Valid(ColonyValidationConfiguration value) {ColonyValidationLimits.Validate(value);return value;}
        string Rejected(ColonyValidationConfiguration value)=>Message(()=>ColonyValidationLimits.Validate(value));
        const string Unsupported="Unsupported colony validation mode or limits.";
        test("colony validation requests keep their declared fields and short defaults",()=> {
            var fields=typeof(ColonyValidationConfiguration).GetFields(BindingFlags.Public|BindingFlags.Instance).Select(field=>field.Name).ToArray();
            check(fields.SequenceEqual(new[]{"Mode","LoadSavePath","AutoStartNewColony","ReuseGeneratedWorld","IdleReservoirLeaseOnly",
                "PrivateFileProtocolAssemblySha256","PrivateOwnedLoadRejection","QuitWhenComplete","QuitFromMainMenu","SaveOnComplete","OverwriteSave","VanillaLoad","SaveBeforeResume",
                "VerifySaveIoRecovery","VerifyNaturalAutosave","VerifyPersistentActors","VerifyLiveBreathing","VerifyTransientParticles",
                "SubnormalCarbonParticle","ReloadInProcess","CompletedFrames","TimeoutSeconds","Seed","Cluster","DlcMask","ColonyName","Floor","Speed","Scenario","LongRun","TelemetryIntervalFrames"}));
            var defaults=Valid(new ColonyValidationConfiguration());
            check(defaults.Mode=="snapshot-copy" && defaults.CompletedFrames==30 && defaults.TimeoutSeconds==600 && defaults.LoadSavePath=="" &&
                !defaults.LongRun && defaults.TelemetryIntervalFrames==ColonyValidationLimits.DefaultTelemetryIntervalFrames &&
                defaults.Floor=="" && defaults.Scenario=="" && defaults.Speed==1 && !defaults.OverwriteSave && !defaults.VanillaLoad && !defaults.QuitFromMainMenu);
            check(ColonyValidationLimits.Modes.Length==41 && ColonyValidationLimits.Modes.Distinct().Count()==41 &&
                ColonyValidationLimits.Modes.Contains("colony-owner-inactive") &&
                ColonyValidationLimits.Modes.Contains("native-configured-domain") && ColonyValidationLimits.Modes.Contains("native-configured-water-domain") && ColonyValidationLimits.Modes.Contains("native-configured-vacuum-domain"));
            foreach(var mode in ColonyValidationLimits.Modes)
            {
                check(Valid(Request(mode,1,30)).Mode==mode);
                var widest=Valid(Request(mode,10000,7200));check(widest.CompletedFrames==10000 && widest.TimeoutSeconds==7200);
            }
            check(Valid(new ColonyValidationConfiguration{Mode="native-domain",CompletedFrames=120,TimeoutSeconds=1800,AutoStartNewColony=true,QuitWhenComplete=true,SaveOnComplete=true}).SaveOnComplete);
        });
        test("DLC selection covers all 32 combinations and refuses unknown identities",()=> {
            check(new ColonyValidationConfiguration().DlcMask==-1);
            for(var mask=0;mask<32;++mask)
            {
                var ids=ColonyDlcSelection.Ids.Where((id,bit)=>(mask&(1<<bit))!=0).ToArray();
                check(ColonyDlcSelection.Mask(ids)==mask);
                check(ColonyDlcSelection.Mask(new[]{""}.Concat(ids).ToArray())==mask);
                check(Valid(new ColonyValidationConfiguration{DlcMask=mask}).DlcMask==mask);
            }
            foreach(var mask in new[]{-2,32,int.MaxValue})reject(()=>Valid(new ColonyValidationConfiguration{DlcMask=mask}));
            foreach(var ids in new[]{new[]{"DLC6_ID"},new[]{"VANILLA_ID"},new[]{"DLC2_ID","DLC2_ID"},new[]{"",""},new string[]{null!}})
                reject(()=>ColonyDlcSelection.Mask(ids));
            reject(()=>ColonyDlcSelection.Mask(null!));
            check(Valid(new ColonyValidationConfiguration{Mode="colony-owner",DlcMask=1,Scenario="spaced-out-worlds"}).Scenario=="spaced-out-worlds");
        });
        test("floor and colony controls require their declared modes and keep fixed identities",()=> {
            foreach(var mode in new[]{"colony-owner","colony-vanilla"})
                foreach(var arena in new[]{"f1","f2","f3","f4","f5"})
                    check(Valid(new ColonyValidationConfiguration{Mode=mode,Floor=arena}).Floor==arena);
            reject(()=>Valid(new ColonyValidationConfiguration{Mode="colony-vanilla"}));
            reject(()=>Valid(new ColonyValidationConfiguration{Mode="colony-owner",Floor="f6"}));
            reject(()=>Valid(new ColonyValidationConfiguration{Mode="colony-owner",Floor="f1",Scenario="door"}));
            reject(()=>Valid(new ColonyValidationConfiguration{Mode="native-domain",Floor="f1"}));
            foreach(var speed in new[]{1,2,3})check(Valid(new ColonyValidationConfiguration{Speed=speed}).Speed==speed);
            foreach(var speed in new[]{0,4})reject(()=>Valid(new ColonyValidationConfiguration{Speed=speed}));
            reject(()=>Valid(new ColonyValidationConfiguration{Mode="colony-owner",OverwriteSave=true}));
            check(Valid(new ColonyValidationConfiguration{Mode="colony-owner",OverwriteSave=true,SaveOnComplete=true}).OverwriteSave);
            reject(()=>Valid(new ColonyValidationConfiguration{Mode="colony-owner-load",OverwriteSave=true}));
            check(Valid(new ColonyValidationConfiguration{Mode="colony-owner-load",OverwriteSave=true,SaveOnComplete=true}).OverwriteSave);
            reject(()=>Valid(new ColonyValidationConfiguration{Mode="native-load-domain",OverwriteSave=true,SaveOnComplete=true}));
            reject(()=>Valid(new ColonyValidationConfiguration{Mode="colony-owner",VanillaLoad=true}));
            check(Valid(new ColonyValidationConfiguration{Mode="colony-owner-load",VanillaLoad=true}).VanillaLoad);
        });
        test("short requests reject their old out-of-range values and name the explicit long-run opt-in",()=> {
            foreach(var value in new[]{Request("native-domain",0,600),Request("native-domain",-1,600),Request("native-domain",120,29),
                Request("native-unknown",120,600),Request("Native-Domain",120,600),Request("native-domain",100001,600),
                Request("native-domain",120,172801),Request(null!,120,600)})
                check(Rejected(value)==Unsupported);
            check(Rejected(Request("native-domain",10001,600)).Contains("explicit LongRun opt-in"));
            check(Rejected(Request("native-domain",30000,172800)).Contains("explicit LongRun opt-in"));
            check(Rejected(Request("native-domain",120,7201)).Contains("explicit LongRun opt-in"));
            check(Rejected(Request("native-domain",120,600,false,60)).Contains("interval requires the explicit LongRun"));
            reject(()=>ColonyValidationLimits.Validate(null!));
        });
        test("explicit long runs admit ten ONI cycles with margin and a 48-hour limit in audited scenes",()=> {
            // Ten 600-second cycles at the native 0.2-second timestep.
            check(ColonyValidationLimits.MaximumLongRunFrames>=10*600*5 && ColonyValidationLimits.MaximumLongRunTimeoutSeconds==48*3600);
            check(ColonyValidationLimits.LongRunModes.All(mode=>ColonyValidationLimits.Modes.Contains(mode)));
            foreach(var mode in ColonyValidationLimits.LongRunModes)
            {
                var tenCycles=Valid(Request(mode,30000,172800,true));
                check(tenCycles.LongRun && tenCycles.CompletedFrames==30000 && tenCycles.TimeoutSeconds==172800 && tenCycles.TelemetryIntervalFrames==300);
                check(Valid(Request(mode,100000,172800,true,10)).TelemetryIntervalFrames==10);
                check(Valid(Request(mode,1,30,true,10000)).CompletedFrames==1);
                var started=Request(mode,600,600,true);started.AutoStartNewColony=started.QuitWhenComplete=started.ReuseGeneratedWorld=true;
                check(Valid(started).LongRun);
            }
        });
        test("long runs keep frame, timeout and telemetry ceilings and reject unaudited scenes or fixture controls",()=> {
            check(Rejected(Request("native-domain",100001,600,true))==Unsupported);
            check(Rejected(Request("native-domain",30000,172801,true))==Unsupported);
            check(Rejected(Request("native-domain",30000,29,true))==Unsupported);
            foreach(var interval in new[]{9,0,-300,10001})
                check(Rejected(Request("native-domain",30000,172800,true,interval)).Contains("10..10000"));
            check(Rejected(Request("native-domain",120,600,false,9)).Contains("10..10000"));
            foreach(var mode in ColonyValidationLimits.Modes.Except(ColonyValidationLimits.LongRunModes))
            {
                check(Rejected(Request(mode,120,600,true)).StartsWith("Long runs admit only",StringComparison.Ordinal));
                check(Rejected(Request(mode,30000,172800,true)).StartsWith("Long runs admit only",StringComparison.Ordinal));
            }
            foreach(var field in new[]{"SaveOnComplete","SaveBeforeResume","VerifySaveIoRecovery","VerifyNaturalAutosave","VerifyPersistentActors",
                "VerifyLiveBreathing","VerifyTransientParticles","SubnormalCarbonParticle","ReloadInProcess","IdleReservoirLeaseOnly","PrivateOwnedLoadRejection"})
            {
                var value=Request("native-domain",30000,172800,true);
                typeof(ColonyValidationConfiguration).GetField(field)!.SetValue(value,true);
                check(Rejected(value).StartsWith("Long runs exclude",StringComparison.Ordinal));
            }
            var load=Request("native-domain",30000,172800,true);load.LoadSavePath="/tmp/colony.sav";
            check(Rejected(load).StartsWith("Long runs exclude",StringComparison.Ordinal));
            var pinned=Request("native-domain",30000,172800,true);pinned.PrivateFileProtocolAssemblySha256=new string('a',64);
            check(Rejected(pinned).StartsWith("Long runs exclude",StringComparison.Ordinal));
        });
    }
}
