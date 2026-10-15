using System.Buffers.Binary;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;
using HarmonyLib;
using MixedAtmospheres.Oni;
using UnityEngine.SceneManagement;

internal static class Program
{
    private static string? fixture;
    private static int Main(string[] args)
    {
        if (args.Length < 1 || args.Length > 2) throw new ArgumentException("Pass the licensed ONI directory and optional private malformed fixture.");
        var managed = Path.Combine(Path.GetFullPath(args[0]), "OxygenNotIncluded_Data", "Managed");
        AssemblyLoadContext.Default.Resolving += (_, name) =>
        { var path = Path.Combine(managed, name.Name + ".dll"); return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null; };
        fixture = args.Length == 2 ? args[1] : null;
        return Run();
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Run()
    {
        var tests = new (string, System.Action)[]
        {
            ("empty selection stays inactive; actual empty bytes are invalid", Empty),
            ("ordinary header preserves exact bytes and all metadata", Ordinary),
            ("ordinary native build metadata is not mistaken for ownership", OrdinaryBuild),
            ("fully valid owned declarations still require rejection", Owned),
            ("escaped reserved property cannot downgrade to unmarked", Escaped),
            ("case variants of reserved namespace reject explicitly", WrongCase),
            ("duplicate reserved and ordinary JSON keys reject", Duplicate),
            ("every truncated owned JSON prefix rejects", Truncated),
            ("invalid UTF8 and malformed Unicode escapes reject", Unicode),
            ("strict JSON rejects comments trailing commas unquoted names and nonfinite numbers", JsonSyntax),
            ("reserved name inside a value or nested object is not a top-level declaration", Namespace),
            ("binary header lengths versions compression and bounds reject", PrefixBounds),
            ("JSON depth and token limits stop oversized structure", Complexity),
            ("owned metadata versions identities and hashes validate strictly", OwnedMetadata),
            ("owned schema extension and missing fields reject", ClosedSchema),
            ("inspection copies both input and exported bytes", Immutable),
            ("actual copied reader header must exactly match the ordinary preflight", ActualHeader),
            ("header replacement shifted readers and truncation reject before native header parsing", ActualHeaderChanges),
            ("unmarked selected header cannot hide a MACOLONY native section", EnvelopeMagic),
            ("native section framing is bounded without interpreting ordinary payloads", NativeFraming),
            ("ordinary selected observations are immutable and one-use", SelectedContext),
            ("file inspection reads only the bounded header without modifying body", FileHeader),
            ("missing denied directory URI and device paths report read error", FileErrors),
            ("actual selected truncated file is invalid rather than no selection", TruncatedFile),
            ("valid ordinary inputs cannot acquire fatal attempts", NoFalseAttempt),
            ("attempt observations are immutable and fatal remains sticky", Sticky),
            ("frontend request is once-only and safe across callback reentry", FrontendReentry),
            ("native view retirement requires exact fatal owner thread and once-latched request", ViewRetirementDecision),
            ("stale successor and entering teardown cannot retire another native view", ViewRetirementLifetime),
            ("SetIsLoading reentry must revalidate identity owner and teardown before retirement", ViewRetirementReentry),
            ("pinned Game.SetIsLoading only assigns its exact managed field", ViewRetirementContract),
            ("view retirement precedes frontend and only invalidates CellCount after the decision", ViewRetirementWiring),
            ("native view retirement observations retain immutable exact references and dimensions", ViewRetirementObservation),
            ("completion kind is chosen once and cannot fall back after Game cleanup", CompletionKinds),
            ("pre-Game completion requires both witnessed bodies all holds and exact reference clearance", PreGameJoin),
            ("failed skipped and swallowed pre-Game bodies remain terminal", PreGameWitnessFailures),
            ("stale foreign and duplicate pre-Game tokens cannot release a successor", PreGameStaleTokens),
            ("exact inherited loader and scene hooks retain the OnSpawn and selected-call holds", PreGameHookJoin),
            ("skipped and swallowed Game teardown bodies cannot report completion", GameWitnessFailures),
            ("normal-path certificates reject skipped bodies and throws after pre-return markers", NormalPathCertificates),
            ("all three original bodies receive entry and normal-return witnesses with labels preserved", BodyWitnessPlacement),
            ("body witnesses reject unsupported targets duplicate callbacks and exception regions", BodyWitnessRejection),
            ("OnSpawn acquires its hold before reads and releases only its matching finalizer", OnSpawnHoldWiring),
            ("unexpected late Game initialization irreversibly invalidates pre-Game completion", LateGameInvalidation),
            ("empty animation-grid fence requires exact authority and preserves ordinary grid work", EmptyAnimationDecision),
            ("native getter reentry requires fresh owner capture and fresh animation authority", CaptureReentry),
            ("exact dead-loader CAS preserves null and foreign successor references", ExactLoaderReferenceCas),
            ("dead-loader clearance revalidates observed authority and current holds before CAS", PreGameFinalizationAuthority),
            ("pre-Game finalization authenticates lifecycle before CAS and never calls native reset", PreGameFinalizationWiring),
            ("reentrant selected loads cannot replace a fatal attempt", ReentrantSelection),
            ("selected inner and outer false results latch before fatal gating", SelectedFalseResults),
            ("early rejected selected calls stay held across synchronous teardown", EarlyRejectedInvocation),
            ("actual result hook uses the tested decision and requests frontend before forcing false", ResultHookWiring),
            ("fatal callbacks stop saved-root success and both Game.Load continuation boundaries", DeserializationContinuation),
            ("actual teardown is mandatory and failures retain fatal", Teardown),
            ("stale and foreign teardown handles cannot release a successor", Stale),
            ("nested actual teardown hooks give completion only to the outer invocation", NestedTeardownHooks),
            ("failed actual outer teardown cannot be released by nested or stale completion", FailedTeardownHooks),
            ("actual fatal result hook overrides an ignored inner result without callback replay", FatalResultHook),
            ("active-path clearing requires the exact still-held selected failure", SelectedPathClearWiring),
            ("OnSpawn continuation preserves ordinary success and stops false after completed teardown", OnSpawnContinuationDecision),
            ("OnSpawn adds one clean guard only after the selected result setter", OnSpawnContinuationPlacement),
            ("OnSpawn rejects ambiguous adjacency duplicate guards and exception regions", OnSpawnContinuationRejection),
            ("manual install and rollback both include the exact continuation transpiler", OnSpawnTranspilerContract),
            ("callback teardown cannot release fatal until original selected call unwinds", PendingSelected),
            ("entering teardown cannot request a recursive frontend transition", TeardownNoRecursiveFrontend),
            ("attempt and observation histories stay bounded", BoundedHistory),
            ("attempt mutation requires activation thread; immutable reads remain available", Threading),
            ("manual hooks match every exact licensed overload and patch signature", Contracts),
            ("licensed assemblies match the pinned installation contract hashes", PinnedHashes),
            ("dormant boundary has no automatic PatchAll activation or native shutdown call", Dormant),
            ("private malformed copied-save fixture remains byte-identical", PrivateFixture)
        };
        var failed = 0;
        foreach (var (name, test) in tests)
        { try { test(); Console.WriteLine("PASS " + name); } catch (Exception error) { ++failed; Console.WriteLine("FAIL " + name + "\n" + error); } }
        Console.WriteLine($"{tests.Length - failed} passed, {failed} failed"); return failed == 0 ? 0 : 1;
    }
    private static void Check(bool condition, string message = "Assertion failed") { if (!condition) throw new Exception(message); }
    private static void Throws<T>(System.Action action) where T : Exception
    { try { action(); } catch (T) { return; } throw new Exception("Expected " + typeof(T).Name); }
    private static string Hash(byte[] value) => Convert.ToHexStringLower(SHA256.HashData(value));
    private static byte[] Header(string json, uint build = 744825, uint version = 1, int compression = 1)
    {
        var data = Encoding.UTF8.GetBytes(json); var bytes = new byte[16 + data.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(bytes, build); BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(4), data.Length);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(8), version); BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(12), compression);
        data.CopyTo(bytes, 16); return bytes;
    }
    private static string OwnedJson()
    {
        var id = "11111111222233334444555555555555";
        return "{\"baseName\":\"task\",\"colonyGuid\":\"11111111-2222-3333-4444-555555555555\",\"saveMajorVersion\":7,\"saveMinorVersion\":38,\"" + OwnedReservoirLoadHeaderV1.ReservedPropertyName + "\":{" +
            "\"formatVersion\":1,\"scope\":\"two-gas-reservoirs-idle\",\"oniBuild\":744825,\"nativeColonyId\":\"" + id + "\",\"ownerCount\":2," +
            "\"checkpointFormatVersion\":1,\"checkpointSha256\":\"" + new string('a', 64) + "\",\"envelopeFormatVersion\":1,\"envelopeSha256\":\"" + new string('b', 64) + "\",\"coreFormatVersion\":3,\"materialModelVersion\":1," +
            "\"catalogVersion\":\"explicit-test-catalog\",\"catalogFingerprint\":\"" + new string('c', 64) + "\",\"assemblyCSharpSha256\":\"" + OwnedReservoirLoadHeaderV1.AssemblyCSharpSha256 + "\",\"assemblyCSharpFirstpassSha256\":\"" + OwnedReservoirLoadHeaderV1.FirstpassSha256 + "\"}}";
    }
    private static OwnedReservoirLoadHeaderInspection Invalid() => OwnedReservoirLoadHeaderV1.Inspect(Header("{\"mixedAtmospheresOwnedReservoirs\":{\"formatVersion\":99}}"));
    private static void IsInvalid(byte[] bytes) { var result = OwnedReservoirLoadHeaderV1.Inspect(bytes); Check(result.Kind == OwnedReservoirLoadInputKind.InvalidHeader && result.MustReject); }
    private static void Empty()
    { Check(OwnedReservoirLoadHeaderV1.InspectFile(null).Kind == OwnedReservoirLoadInputKind.NoSelection); Check(!OwnedReservoirLoadHeaderV1.InspectFile("").MustReject); IsInvalid(Array.Empty<byte>()); }
    private static void Ordinary()
    {
        var bytes = Header("{\"baseName\":\"ordinary\",\"dlcIds\":[\"EXPANSION1_ID\"],\"n\":-2.4e+3,\"b\":true,\"o\":null,\"emoji\":\"🚀\"}");
        var reading = OwnedReservoirLoadHeaderV1.Inspect(bytes);
        Check(reading.Kind == OwnedReservoirLoadInputKind.Unmarked && !reading.MustReject && reading.CopyHeaderBytes().SequenceEqual(bytes));
        Check(reading.HeaderSha256 == Hash(bytes) && reading.Declaration == null && !reading.ReservedNamespaceObserved);
    }
    private static void OrdinaryBuild() { Check(OwnedReservoirLoadHeaderV1.Inspect(Header("{}", 700000)).Kind == OwnedReservoirLoadInputKind.Unmarked); }
    private static void Owned()
    { var r = OwnedReservoirLoadHeaderV1.Inspect(Header(OwnedJson())); Check(r.Kind == OwnedReservoirLoadInputKind.OwnedHeader && r.MustReject && r.ReservedNamespaceObserved && r.Declaration!.CatalogVersion == "explicit-test-catalog"); }
    private static void Escaped()
    {
        var escaped = string.Concat(OwnedReservoirLoadHeaderV1.ReservedPropertyName.Select(ch => "\\u" + ((int)ch).ToString("x4")));
        Check(OwnedReservoirLoadHeaderV1.Inspect(Header(OwnedJson().Replace(OwnedReservoirLoadHeaderV1.ReservedPropertyName, escaped))).Kind == OwnedReservoirLoadInputKind.OwnedHeader);
    }
    private static void WrongCase() { var r = OwnedReservoirLoadHeaderV1.Inspect(Header(OwnedJson().Replace("mixedAtmospheresOwnedReservoirs", "MixedAtmospheresOwnedReservoirs"))); Check(r.Kind == OwnedReservoirLoadInputKind.InvalidHeader && r.ReservedNamespaceObserved); }
    private static void Duplicate()
    { IsInvalid(Header("{\"mixedAtmospheresOwnedReservoirs\":{},\"mixedAtmospheresOwnedReservoirs\":{}}")); IsInvalid(Header("{\"x\":1,\"x\":2}")); IsInvalid(Header("{\"x\":{\"z\":1,\"z\":2}}")); }
    private static void Truncated()
    { var json = OwnedJson(); for (var i = 0; i < json.Length; ++i) IsInvalid(Header(json.Substring(0, i))); foreach (var text in new[] { "{\"mixedAtmospheresOwnedRes", "{\"\\u006dixedAtmospheresOwnedReservoirs", "{\"mixedAtmospheresOwnedReservoirs\"" }) IsInvalid(Header(text)); }
    private static void Unicode()
    { var bad = Header("{\"x\":\"ab\"}"); bad[bad.Length - 4] = 0xff; IsInvalid(bad); foreach (var text in new[] { "{\"x\":\"\\uD800\"}", "{\"x\":\"\\uDC00\"}", "{\"x\":\"\\uGGGG\"}" }) IsInvalid(Header(text)); Check(!OwnedReservoirLoadHeaderV1.Inspect(Header("{\"x\":\"\\uD83D\\uDE80\"}")).MustReject); }
    private static void JsonSyntax()
    { foreach (var text in new[] { "{x:1}", "{'x':1}", "{\"x\":NaN}", "{\"x\":Infinity}", "{\"x\":01}", "{\"x\":1.}", "{\"x\":1e}", "{\"x\":+1}", "{\"x\":true,}", "{\"x\":[1,]}", "{}{}", "/*x*/{}", "[]", "null" }) IsInvalid(Header(text)); }
    private static void Namespace()
    { foreach (var text in new[] { "{\"text\":\"mixedAtmospheresOwnedReservoirs\"}", "{\"other\":{\"mixedAtmospheresOwnedReservoirs\":{}}}" }) Check(!OwnedReservoirLoadHeaderV1.Inspect(Header(text)).MustReject); }
    private static void PrefixBounds()
    {
        var good = Header("{}"); foreach (var length in new[] { -1, 0, int.MaxValue, OwnedReservoirLoadHeaderV1.MaximumJsonBytes + 1, 1, 3 }) { var b = (byte[])good.Clone(); BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(4), length); IsInvalid(b); }
        IsInvalid(Header("{}", version: 0)); IsInvalid(Header("{}", version: 2)); IsInvalid(Header("{}", compression: -1)); IsInvalid(Header("{}", compression: 2)); IsInvalid(Header("{}", build: 0)); IsInvalid(good.Concat(new byte[] { 0 }).ToArray());
    }
    private static void Complexity()
    { IsInvalid(Header("{\"x\":" + new string('[', 34) + "0" + new string(']', 34) + "}")); IsInvalid(Header("{\"x\":[" + string.Join(',', Enumerable.Repeat("0", OwnedReservoirLoadHeaderV1.MaximumJsonTokens)) + "]}")); }
    private static void OwnedMetadata()
    {
        var source = OwnedJson();
        foreach (var (from, to) in new[] { ("\"formatVersion\":1", "\"formatVersion\":99"), ("\"ownerCount\":2", "\"ownerCount\":1"), ("\"coreFormatVersion\":3", "\"coreFormatVersion\":2"), ("\"oniBuild\":744825", "\"oniBuild\":744824"), ("\"materialModelVersion\":1", "\"materialModelVersion\":1.0"), ("\"saveMinorVersion\":38", "\"saveMinorVersion\":37"), ("two-gas-reservoirs-idle", "world"), ("explicit-test-catalog", ""), (OwnedReservoirLoadHeaderV1.AssemblyCSharpSha256, new string('a', 64)), (new string('c',64),new string('C',64)), ("11111111222233334444555555555555", "22222222222233334444555555555555") }) IsInvalid(Header(source.Replace(from, to)));
        IsInvalid(Header(source, build: 744824));
    }
    private static void ClosedSchema()
    { IsInvalid(Header(OwnedJson().Replace("\"ownerCount\":2,", ""))); IsInvalid(Header(OwnedJson().Replace("\"ownerCount\":2,", "\"extra\":1,\"ownerCount\":2,"))); IsInvalid(Header("{\"mixedAtmospheresOwnedReservoirs\":null}")); }
    private static void Immutable()
    { var bytes = Header(OwnedJson()); var result = OwnedReservoirLoadHeaderV1.Inspect(bytes); var hash = result.HeaderSha256; Array.Fill(bytes,(byte)0); var copy = result.CopyHeaderBytes(); Array.Fill(copy,(byte)1); Check(Hash(result.CopyHeaderBytes()) == hash && result.Declaration!.CatalogVersion == "explicit-test-catalog"); }
    private static void ActualHeader()
    {var bytes=Header("{\"baseName\":\"ordinary\"}");var expected=OwnedReservoirLoadHeaderV1.Inspect(bytes);var raw=bytes.Concat(new byte[]{9,8,7,6}).ToArray();var before=Hash(raw);var result=OwnedReservoirLoadHeaderV1.VerifyActualHeader(expected,raw,0);Check(result.Kind==OwnedReservoirLoadInputKind.Unmarked && result.CopyHeaderBytes().SequenceEqual(bytes) && Hash(raw)==before);}
    private static void ActualHeaderChanges()
    {var expected=OwnedReservoirLoadHeaderV1.Inspect(Header("{}"));foreach(var raw in new[]{Header(OwnedJson()),Header("{\"changed\":true}"),new byte[3],Header("{}").Take(17).ToArray()})Check(OwnedReservoirLoadHeaderV1.VerifyActualHeader(expected,raw,0).MustReject);Check(OwnedReservoirLoadHeaderV1.VerifyActualHeader(expected,Header("{}"),1).MustReject);Throws<ArgumentException>(()=>OwnedReservoirLoadHeaderV1.VerifyActualHeader(Invalid(),Header("{}"),0));}
    private static byte[] Section(string payload){var data=Encoding.ASCII.GetBytes(payload);var bytes=new byte[data.Length+4];BinaryPrimitives.WriteInt32LittleEndian(bytes,data.Length);data.CopyTo(bytes,4);return bytes;}
    private static void EnvelopeMagic()
    {var bytes=Section("MACOLONYopaque-unverified-bytes");var r=OwnedReservoirLoadHeaderV1.InspectNativeSection(bytes,0,"task");Check(r!.Kind==OwnedReservoirLoadInputKind.UnsupportedEnvelope && r.MustReject && !r.ReservedNamespaceObserved && r.Declaration==null);var shifted=new byte[3].Concat(bytes).ToArray();Check(OwnedReservoirLoadHeaderV1.InspectNativeSection(shifted,3,"task")!.Kind==OwnedReservoirLoadInputKind.UnsupportedEnvelope);Check(OwnedReservoirLoadHeaderV1.InspectNativeSection(Section("MACOLONx"),0,"task")==null);}
    private static void NativeFraming()
    {foreach(var bytes in new[]{Array.Empty<byte>(),new byte[3],new byte[4],Section("MACOLONY").Take(10).ToArray()})Check(OwnedReservoirLoadHeaderV1.InspectNativeSection(bytes,0,"task")!.Kind==OwnedReservoirLoadInputKind.InvalidNativeSection);Check(OwnedReservoirLoadHeaderV1.InspectNativeSection(Section("SIMSAVE\0ordinary"),0,"task")==null);Check(OwnedReservoirLoadHeaderV1.InspectNativeSection(new byte[4],-1,"task")!.MustReject);}
    private static void SelectedContext()
    {var a=new OwnedReservoirSelectedLoad(OwnedReservoirLoadHeaderV1.Inspect(Header("{}")));var b=a.WithVerifiedHeader();Check(!a.ActualHeaderVerified && b.ActualHeaderVerified && ReferenceEquals(a.Identity,b.Identity));Throws<InvalidOperationException>(()=>b.WithVerifiedHeader());Throws<ArgumentException>(()=>new OwnedReservoirSelectedLoad(Invalid()));}
    private static void InTemp(System.Action<string> action)
    {
        var root = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var dir = Path.GetFullPath(Path.Combine(root, "ma-load-check-" + Guid.NewGuid().ToString("N")));
        Check(dir.StartsWith(root, StringComparison.Ordinal) && Path.GetFileName(dir).StartsWith("ma-load-check-", StringComparison.Ordinal));
        Directory.CreateDirectory(dir); try { action(dir); } finally { Directory.Delete(dir,true); }
    }
    private static void FileHeader()
    { InTemp(dir => { var path=Path.Combine(dir,"ordinary.sav"); var bytes=Header("{}").Concat(new byte[]{255,0,1,2,3}).ToArray(); File.WriteAllBytes(path,bytes); var result=OwnedReservoirLoadHeaderV1.InspectFile(path); Check(result.Kind==OwnedReservoirLoadInputKind.Unmarked && result.HeaderBytes==18 && result.FileBytes==bytes.Length && File.ReadAllBytes(path).SequenceEqual(bytes)); }); }
    private static void FileErrors()
    { InTemp(dir => { foreach(var path in new[]{Path.Combine(dir,"missing.sav"),dir,"https://example.invalid/owned.sav","\\\\.\\pipe\\owned","bad\0path"," "}) Check(OwnedReservoirLoadHeaderV1.InspectFile(path).Kind==OwnedReservoirLoadInputKind.ReadError); var denied=Path.Combine(dir,"locked.sav"); File.WriteAllBytes(denied,Header("{}")); using var locked=new FileStream(denied,FileMode.Open,FileAccess.ReadWrite,FileShare.None); Check(OwnedReservoirLoadHeaderV1.InspectFile(denied).Kind==OwnedReservoirLoadInputKind.ReadError); }); }
    private static void TruncatedFile()
    { InTemp(dir=>{var path=Path.Combine(dir,"short.sav");File.WriteAllBytes(path,Header(OwnedJson()).Take(20).ToArray());var r=OwnedReservoirLoadHeaderV1.InspectFile(path);Check(r.Kind==OwnedReservoirLoadInputKind.InvalidHeader && r.MustReject);}); }
    private static void NoFalseAttempt()
    {var tracker=new OwnedReservoirLoadAttemptTracker();Throws<ArgumentException>(()=>tracker.Reject("bad",OwnedReservoirLoadHeaderV1.InspectFile(null)));Throws<ArgumentException>(()=>tracker.Reject("bad",OwnedReservoirLoadHeaderV1.Inspect(Header("{}"))));Check(!tracker.IsFatal && tracker.Last==null);}
    private static void Sticky()
    {var t=new OwnedReservoirLoadAttemptTracker();var old=t.Reject("outer",Invalid());t.Record(old,"inner:false","caught");t.Record(old,"exception","swallowed");Check(t.IsFatal && old.Observations.Count==1 && t.Current!.Observations.Count==3 && t.Current.Id==old.Id && t.Current.Fatal);}
    private static void FrontendReentry()
    {var t=new OwnedReservoirLoadAttemptTracker();var a=t.Reject("outer",Invalid());Check(t.TryRequestFrontend(a));Check(!t.TryRequestFrontend(a));var token=t.BeginTeardown(a);Check(token!=null);Check(t.CompleteTeardown(token!,null));Check(!t.Record(a,"callback-return","must not resurrect"));Check(!t.IsFatal && t.Last!.TeardownCompleted && t.Last.FrontendRequested);}
    private static string ViewDecision(OwnedReservoirLoadAttempt expected, OwnedReservoirLoadAttempt? current,
        Game? expectedOwner, Game? observedOwner, bool installed = true, int expectedThread = 17, int observedThread = 17)
        => InvokeBoundary("DecideViewRetirement", installed, expectedThread, observedThread, expected, current, expectedOwner, observedOwner)!.ToString()!;
    private static void ViewRetirementDecision()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();var attempt=tracker.Reject("selected",Invalid());
        var owner=ManagedShell<Game>();var foreign=ManagedShell<Game>();
        Check(ViewDecision(attempt,tracker.Current,owner,owner)=="NotCurrentAttempt","No retirement before once-only frontend latch");
        Check(tracker.TryRequestFrontend(attempt));var current=tracker.Current!;
        Check(ViewDecision(attempt,current,owner,owner)=="Retire");
        Check(ViewDecision(attempt,current,owner,owner,installed:false)=="NotCurrentAttempt");
        Check(ViewDecision(attempt,current,owner,owner,observedThread:18)=="NotCurrentAttempt");
        Check(ViewDecision(attempt,null,owner,owner)=="NotCurrentAttempt");
        Check(ViewDecision(attempt,current,owner,foreign)=="ForeignOwner");
        Check(ViewDecision(attempt,current,null,null)=="NoCurrentOwner");
        Check(ViewDecision(attempt,current,null,owner)=="ForeignOwner");
        Check(ViewDecision(attempt,current,owner,null)=="NoCurrentOwner");
        var code=PatchProcessor.GetOriginalInstructions(BoundaryMethod("DecideViewRetirement")).ToArray();
        Check(!code.Any(i=>i.operand is FieldInfo f && f.DeclaringType==typeof(Grid)),"Pure decision must not initialize Grid");
        Check(!code.Any(i=>i.operand is MethodBase m && m.DeclaringType==typeof(UnityEngine.Object)),"Unity overloaded equality is unsafe on retired references");
    }
    private static void ViewRetirementLifetime()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();var owner=ManagedShell<Game>();
        var old=tracker.Reject("old",Invalid());Check(tracker.TryRequestFrontend(old));
        var begun=tracker.BeginTeardown(old)!;
        Check(ViewDecision(old,tracker.Current,owner,owner)=="NotCurrentAttempt");
        Check(tracker.CompleteTeardown(begun,null));
        Check(ViewDecision(old,tracker.Current,owner,owner)=="NotCurrentAttempt");
        var successor=tracker.Reject("successor",Invalid());Check(tracker.TryRequestFrontend(successor));
        Check(ViewDecision(old,tracker.Current,owner,owner)=="NotCurrentAttempt");
        Check(ViewDecision(successor,tracker.Current,owner,owner)=="Retire");
        var otherTracker=new OwnedReservoirLoadAttemptTracker();var foreign=otherTracker.Reject("foreign",Invalid());
        Check(otherTracker.TryRequestFrontend(foreign));
        Check(ViewDecision(successor,otherTracker.Current,owner,owner)=="NotCurrentAttempt");
        Check(tracker.Current!.Id==successor.Id && tracker.Current.FrontendRequested && !tracker.Current.TeardownStarted);
    }
    private static void ViewRetirementReentry()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();var owner=ManagedShell<Game>();var replacement=ManagedShell<Game>();
        var before=tracker.Reject("selected",Invalid(),selectedCallPending:true);Check(tracker.TryRequestFrontend(before));
        var beforeCallback=tracker.Current!;Check(ViewDecision(before,beforeCallback,owner,owner)=="Retire");
        tracker.Record(before,"foreign patch callback","same identity may create a new immutable observation");
        Check(!ReferenceEquals(beforeCallback,tracker.Current) && ViewDecision(before,tracker.Current,owner,owner)=="Retire");
        Check(ViewDecision(before,tracker.Current,owner,replacement)=="ForeignOwner");
        Check(ViewDecision(before,tracker.Current,owner,null)=="NoCurrentOwner");
        Check(ViewDecision(before,tracker.Current,owner,owner,observedThread:18)=="NotCurrentAttempt");
        var teardown=tracker.BeginTeardown(before)!;Check(ViewDecision(before,tracker.Current,owner,owner)=="NotCurrentAttempt");
        Check(!tracker.CompleteTeardown(teardown,null) && ViewDecision(before,tracker.Current,owner,owner)=="NotCurrentAttempt");
        Check(tracker.ReleaseSelectedCall(before) && ViewDecision(before,tracker.Current,owner,owner)=="NotCurrentAttempt");
        var successor=tracker.Reject("reentrant successor",Invalid());Check(tracker.TryRequestFrontend(successor));
        Check(ViewDecision(before,tracker.Current,owner,owner)=="NotCurrentAttempt");
        Check(ViewDecision(successor,tracker.Current,replacement,replacement)=="Retire");
        // These are pure before/after-call decisions, not execution of Unity or
        // the Grid-mutating preparation method on the managed host.
    }
    private static void ViewRetirementContract()
    {
        InvokeBoundary("RequireViewRetirementContract");
        var method=typeof(Game).GetMethod("SetIsLoading",BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance,Type.EmptyTypes)!;
        var field=typeof(Game).GetField("isLoading",BindingFlags.NonPublic|BindingFlags.Instance)!;
        var body=method.GetMethodBody()!;var il=body.GetILAsByteArray()!;
        Check(!method.IsStatic && method.ReturnType==typeof(void) && field.FieldType==typeof(bool) && !field.IsStatic);
        Check(body.ExceptionHandlingClauses.Count==0 && il.Length==8);
        Check(il[0]==0x02 && il[1]==0x17 && il[2]==0x7d && BitConverter.ToInt32(il,3)==field.MetadataToken && il[7]==0x2a);
        var instructions=PatchProcessor.GetOriginalInstructions(method).ToArray();
        Check(instructions.Length==4 && instructions[2].opcode==System.Reflection.Emit.OpCodes.Stfld && Equals(instructions[2].operand,field));
        var contracts=PatchProcessor.GetOriginalInstructions(typeof(OwnedReservoirLoadBoundary).GetMethod("GuardContracts",BindingFlags.NonPublic|BindingFlags.Static)!).ToArray();
        Check(Calls(contracts.First(i=>i.opcode!=System.Reflection.Emit.OpCodes.Nop),typeof(OwnedReservoirLoadBoundary),"RequireViewRetirementContract"),"Validate the field-only contract before any hook installation");
        // No invocation of the game method, Grid, or their type initializers.
    }
    private static void ViewRetirementWiring()
    {
        var request=PatchProcessor.GetOriginalInstructions(BoundaryMethod("RequestFrontend")).ToArray();
        var latch=Array.FindIndex(request,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"TryRequestFrontend"));
        var retire=Array.FindIndex(request,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"RetireNativeViewBeforeFrontend"));
        var clear=Array.FindIndex(request,i=>Calls(i,typeof(SaveLoader),"SetActiveSaveFilePath"));
        var scene=Array.FindIndex(request,i=>Calls(i,typeof(App),"LoadScene"));
        Check(latch>=0 && latch<retire && retire<clear && clear<scene);
        Check(request.Skip(retire+1).Take(clear-retire-1).Any(i=>i.opcode.FlowControl==System.Reflection.Emit.FlowControl.Cond_Branch),"A stale attempt must stop the frontend request");
        var prep=PatchProcessor.GetOriginalInstructions(BoundaryMethod("RetireNativeViewBeforeFrontend")).ToArray();
        var thread=Array.FindIndex(prep,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"RequireActivationThread"));
        var owner=Array.FindIndex(prep,i=>Calls(i,typeof(Game),"get_Instance"));
        var current=Array.FindIndex(prep,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"get_Current"));
        var decision=Array.FindIndex(prep,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"DecideViewRetirement"));
        var grid=Array.FindIndex(prep,i=>i.operand is FieldInfo f && f.DeclaringType==typeof(Grid));
        var loading=Array.FindIndex(prep,i=>Calls(i,typeof(Game),"SetIsLoading"));
        var writes=prep.Select((instruction,index)=>(instruction,index)).Where(p=>p.instruction.opcode==System.Reflection.Emit.OpCodes.Stsfld && p.instruction.operand is FieldInfo f && f.DeclaringType==typeof(Grid)).ToArray();
        Check(thread>=0 && thread<owner && owner<current && current<decision && decision<grid && grid<loading);
        Check(writes.Length==1 && ((FieldInfo)writes[0].instruction.operand).Name=="CellCount" && loading<writes[0].index);
        Check(prep[writes[0].index-1].opcode==System.Reflection.Emit.OpCodes.Ldc_I4_0);
        var postThread=Array.FindIndex(prep,loading+1,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"RequireActivationThread"));
        var postAttempt=Array.FindIndex(prep,loading+1,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"get_Current"));
        var postOwner=Array.FindIndex(prep,loading+1,i=>Calls(i,typeof(Game),"get_Instance"));
        var postDecision=Array.FindIndex(prep,loading+1,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"DecideViewRetirement"));
        Check(loading<postThread && postThread<postOwner && postOwner<postAttempt && postAttempt<postDecision && postDecision<writes[0].index,
            "Foreign Harmony callbacks require current attempt revalidation after SetIsLoading and its potentially patched owner getter");
        Check(!prep.Skip(loading+1).Take(postDecision-loading-1).Any(i=>i.operand is FieldInfo f && f.DeclaringType==typeof(Grid)),
            "No global Grid read or write before post-call validation");
        var lastGrid=Array.FindLastIndex(prep,i=>i.operand is FieldInfo f && f.DeclaringType==typeof(Grid));
        var skipExits=prep.Skip(decision+1).Take(grid-decision-1).Count(i=>
            i.opcode==System.Reflection.Emit.OpCodes.Ret ||
            (i.opcode.FlowControl==System.Reflection.Emit.FlowControl.Branch &&
                prep.Skip(lastGrid+1).Any(target=>InstructionLabels(target).Contains(i.operand))));
        Check(skipExits>=2,"Stale and no-exact-owner paths must exit without reaching any Grid access (including Debug shared returns)");
        Check(prep.Skip(postDecision+1).Take(writes[0].index-postDecision-1).Any(i=>
            i.opcode==System.Reflection.Emit.OpCodes.Ret ||
            (i.opcode.FlowControl==System.Reflection.Emit.FlowControl.Branch &&
                prep.Skip(lastGrid+1).Any(target=>InstructionLabels(target).Contains(i.operand)))),
            "Changed post-call decision exits past all Grid accesses and prevents the frontend request");
        Check(prep.Count(i=>i.opcode==System.Reflection.Emit.OpCodes.Ldstr && Equals(i.operand,"native-view-retired"))==1
            && prep.Any(i=>i.opcode==System.Reflection.Emit.OpCodes.Ldstr && Equals(i.operand,"native-view-retirement:skipped")));
        foreach(var method in typeof(OwnedReservoirLoadBoundary).GetMethods(BindingFlags.NonPublic|BindingFlags.Static))
            foreach(var instruction in PatchProcessor.GetOriginalInstructions(method))
            {
                if(instruction.opcode==System.Reflection.Emit.OpCodes.Stsfld && instruction.operand is FieldInfo f && f.DeclaringType==typeof(Grid))
                    Check(method==BoundaryMethod("RetireNativeViewBeforeFrontend") && f.Name=="CellCount","No dimensions/arrays or other Grid stores");
                if(instruction.operand is MethodBase called)Check(!(called.DeclaringType==typeof(Sim)&&called.Name=="Shutdown"));
            }
    }
    private static void ViewRetirementObservation()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();var attempt=tracker.Reject("selected",Invalid());
        var game=ManagedShell<Game>();var observation=new OwnedReservoirNativeViewRetirement(attempt,17,17,true,game,game,true,"retired",1024,0,32,32,32,32);
        Check(ReferenceEquals(observation.AttemptIdentity,attempt.Identity) && observation.AttemptId==attempt.Id);
        Check(ReferenceEquals(observation.ExpectedOwner,game) && ReferenceEquals(observation.ObservedOwner,game) && observation.OwnerMatches && observation.AttemptMatches);
        Check(observation.ActivationThreadId==17 && observation.ObservedThreadId==17 && observation.Retired);
        Check(observation.PriorCellCount==1024 && observation.CellCount==0 && observation.PriorWidth==observation.WidthInCells && observation.PriorHeight==observation.HeightInCells);
        Check(typeof(OwnedReservoirNativeViewRetirement).GetProperties(BindingFlags.NonPublic|BindingFlags.Instance).All(p=>p.SetMethod==null));
        tracker.Record(attempt,"after","unchanged observation");Check(observation.Reason=="retired" && observation.PriorCellCount==1024);
        var skipped=new OwnedReservoirNativeViewRetirement(attempt,17,17,true,null,game,false,"no exact owner");
        Check(!skipped.OwnerMatches && !skipped.Retired && skipped.PriorCellCount==null && skipped.CellCount==null && skipped.WidthInCells==null);
    }
    private static OwnedReservoirLoadAttempt PreGameAttempt(OwnedReservoirLoadAttemptTracker tracker, bool selected=false, bool spawn=false)
    {
        var attempt=tracker.Reject("exact backend cancellation",Invalid(),selected,OwnedReservoirLoadCompletionKind.PreGameLoaderScene,spawn);
        Check(tracker.TryRequestFrontend(attempt));return attempt;
    }
    private static void CompletionKinds()
    {
        var pre=new OwnedReservoirLoadAttemptTracker();var p=PreGameAttempt(pre);
        Check(pre.Reject("reentrant Game",Invalid(),completionKind:OwnedReservoirLoadCompletionKind.ActualGameDestroyInstances).CompletionKind==OwnedReservoirLoadCompletionKind.PreGameLoaderScene);
        Check(pre.BeginTeardown(p)==null);pre.InvalidatePreGame(p,"late Game blocked");
        Check(pre.Current!.PreGameInvalidated && pre.Current.TeardownFailed && pre.BeginPreGameBody(p,OwnedReservoirLoadBodyKind.LoaderOnDestroy)==null);
        var game=new OwnedReservoirLoadAttemptTracker();var g=game.Reject("Game",Invalid());var token=game.BeginTeardown(g)!;
        Check(!game.CompleteTeardown(token,new IOException("native body failed")));
        Check(game.Reject("Game became null",Invalid(),completionKind:OwnedReservoirLoadCompletionKind.PreGameLoaderScene).CompletionKind==OwnedReservoirLoadCompletionKind.ActualGameDestroyInstances);
        Check(game.BeginPreGameBody(g,OwnedReservoirLoadBodyKind.SceneUnloaded)==null && game.IsFatal);
        var none=new OwnedReservoirLoadAttemptTracker();var n=none.Reject("unsupported owner",Invalid(),completionKind:OwnedReservoirLoadCompletionKind.None);
        Check(none.BeginTeardown(n)==null && none.BeginPreGameBody(n,OwnedReservoirLoadBodyKind.LoaderOnDestroy)==null);
        Throws<ArgumentOutOfRangeException>(()=>new OwnedReservoirLoadAttemptTracker().Reject("bad enum",Invalid(),completionKind:(OwnedReservoirLoadCompletionKind)99));
    }
    private static void PreGameJoin()
    {
        foreach(var reverse in new[]{false,true})
        {
            var t=new OwnedReservoirLoadAttemptTracker();var original=PreGameAttempt(t,true,true);
            var loader=t.BeginPreGameBody(original,OwnedReservoirLoadBodyKind.LoaderOnDestroy)!;
            var scene=t.BeginPreGameBody(original,OwnedReservoirLoadBodyKind.SceneUnloaded)!;
            Check(loader!=null && scene!=null && t.BeginPreGameBody(original,OwnedReservoirLoadBodyKind.LoaderOnDestroy)==null);
            Check(t.CompletePreGameBody((reverse?scene:loader)!,true,true,null));
            Check(!t.ReadyToClearOwnerReference(original) && !t.Current!.TeardownCompleted);
            Check(t.CompletePreGameBody((reverse?loader:scene)!,true,true,null));
            Check(t.Current!.LoaderDestroyCompleted && t.Current.SceneUnloadCompleted && !t.Current.OwnerReferenceCleared);
            Check(!t.CompletePreGameOwnerReference(original) && !t.ReleaseSelectedCall(original) && !t.ReadyToClearOwnerReference(original));
            Check(!t.ReleaseOnSpawnCall(original) && t.ReadyToClearOwnerReference(original));
            Check(t.CompletePreGameOwnerReference(original) && !t.IsFatal);
            Check(t.Last!.OwnerReferenceCleared && t.Last.TeardownCompleted && t.Last.CompletionKind==OwnedReservoirLoadCompletionKind.PreGameLoaderScene);
            Check(!original.LoaderDestroyCompleted && original.OnSpawnCallPending && original.SelectedCallPending,"Old observations remain immutable");
            Check(!t.Last.Observations.Any(o=>o.Boundary=="Game.DestroyInstances:complete"));
        }
    }
    private static void PreGameWitnessFailures()
    {
        foreach(var facts in new[]{(false,false,(Exception?)null),(true,false,(Exception?)null),(true,true,(Exception?)new IOException("destructor failed"))})
        {
            var t=new OwnedReservoirLoadAttemptTracker();var a=PreGameAttempt(t);var loader=t.BeginPreGameBody(a,OwnedReservoirLoadBodyKind.LoaderOnDestroy)!;
            Check(!t.CompletePreGameBody(loader,facts.Item1,facts.Item2,facts.Item3));
            Check(!t.CompletePreGameBody(loader,true,true,null) && t.IsFatal && t.Current!.TeardownFailed && !t.Current.TeardownCompleted);
            Check(t.BeginPreGameBody(a,OwnedReservoirLoadBodyKind.LoaderOnDestroy)==null && !t.ReadyToClearOwnerReference(a));
        }
        var early=new OwnedReservoirLoadAttemptTracker();var prior=early.Reject("not requested",Invalid(),completionKind:OwnedReservoirLoadCompletionKind.PreGameLoaderScene);
        Check(early.BeginPreGameBody(prior,OwnedReservoirLoadBodyKind.SceneUnloaded)==null);
    }
    private static void PreGameStaleTokens()
    {
        var t=new OwnedReservoirLoadAttemptTracker();var a=PreGameAttempt(t);var loader=t.BeginPreGameBody(a,OwnedReservoirLoadBodyKind.LoaderOnDestroy)!;
        var foreign=new OwnedReservoirLoadTeardown(a.Identity,OwnedReservoirLoadBodyKind.LoaderOnDestroy);
        Check(!t.CompletePreGameBody(foreign,true,true,null) && t.CompletePreGameBody(loader,true,true,null));
        Check(!t.CompletePreGameBody(loader,true,true,null));var scene=t.BeginPreGameBody(a,OwnedReservoirLoadBodyKind.SceneUnloaded)!;
        Check(t.CompletePreGameBody(scene,true,true,null) && t.CompletePreGameOwnerReference(a));
        var b=PreGameAttempt(t);Check(!t.CompletePreGameBody(loader,true,true,null) && !t.CompletePreGameOwnerReference(a));
        Check(t.Current!.Id==b.Id && !t.Current.LoaderDestroyCompleted && !t.Current.SceneUnloadCompleted);
    }
    private static OwnedReservoirPreGameSceneCapture TestCapture(OwnedReservoirLoadAttempt a,SaveLoader loader,KBatchedAnimUpdater updater,Array? grid=null)
        => new(a,loader,0,"backend","task-owned backend",1,updater,grid);
    private static void PreGameHookJoin()
    {
        var t=new OwnedReservoirLoadAttemptTracker();var a=PreGameAttempt(t,true,true);var loader=ManagedShell<SaveLoader>();
        var updater=ManagedShell<KBatchedAnimUpdater>();using var state=new BoundaryState(t,null,loader);
        var capture=TestCapture(a,loader,updater,new object[0,0]);state.Set("<LastPreGameSceneCapture>k__BackingField",capture);
        var call=new OwnedReservoirOnSpawnCall();state.Set("onSpawnCall",call);state.Set("onSpawnLoader",loader);
        var wrong=new object?[]{ManagedShell<SaveLoader>(),null};InvokeBoundary("LoaderDestroyPrefix",wrong);Check(wrong[1]==null);
        var destroy=new object?[]{loader,null};InvokeBoundary("LoaderDestroyPrefix",destroy);Check(destroy[1] is OwnedReservoirLoadBodyInvocation {Completion:not null});
        InvokeBoundary("LoaderBodyEntry",loader);InvokeBoundary("LoaderBodyReturn",loader);InvokeBoundary("BodyPostfix",destroy[1],true);InvokeBoundary("BodyFinalizer",destroy[1],null);
        Check(t.Current!.LoaderDestroyCompleted && !t.Current.SceneUnloadCompleted && !t.ReadyToClearOwnerReference(a));
        // default Scene has a zero value handle. Only the pinned managed handle
        // getter/conversions run; no Scene.IsValid/name/native APIs are invoked.
        var scene=default(Scene);var unload=new object?[]{scene,null};InvokeBoundary("SceneUnloadPrefix",unload);
        Check(unload[1] is OwnedReservoirLoadBodyInvocation {Completion:not null});
        InvokeBoundary("SceneBodyEntry",scene);InvokeBoundary("SceneBodyReturn",scene);InvokeBoundary("BodyPostfix",unload[1],true);InvokeBoundary("BodyFinalizer",unload[1],null);
        Check(t.Current!.LoaderDestroyCompleted && t.Current.SceneUnloadCompleted && t.Current.OnSpawnCallPending && t.Current.SelectedCallPending);
        InvokeBoundary("OnSpawnFinalizer",new OwnedReservoirOnSpawnCall(),null);Check(t.Current.OnSpawnCallPending);
        InvokeBoundary("OnSpawnFinalizer",call,null);Check(!t.Current!.OnSpawnCallPending && t.Current.SelectedCallPending && !t.ReadyToClearOwnerReference(a));
        Check(!t.ReleaseSelectedCall(a) && t.ReadyToClearOwnerReference(a));
        // A pure acknowledgement models the separately tested CAS; never call
        // the native-field finalization routine on this host.
        Check(t.CompletePreGameOwnerReference(a));
    }
    private static void GameWitnessFailures()
    {
        foreach(var entered in new[]{false,true})
        {
            var t=new OwnedReservoirLoadAttemptTracker();t.Reject("Game",Invalid());var game=ManagedShell<Game>();using var state=new BoundaryState(t,game);
            var args=new object?[]{game,null};InvokeBoundary("TeardownPrefix",args);
            if(entered)InvokeBoundary("GameBodyEntry",game); // exception swallowed: no original return
            InvokeBoundary("BodyFinalizer",args[1],null);
            Check(t.IsFatal && t.Current!.TeardownFailed && !t.Current.TeardownCompleted);
            InvokeBoundary("GameBodyReturn",game);InvokeBoundary("BodyFinalizer",args[1],null);Check(t.IsFatal && !t.Current!.TeardownCompleted);
        }
    }
    private static void NormalPathCertificates()
    {
        foreach(var mode in new[]{"later-throw-swallowed","skip-original","duplicate","normal"})
        {
            var t=new OwnedReservoirLoadAttemptTracker();t.Reject("Game",Invalid());var game=ManagedShell<Game>();using var state=new BoundaryState(t,game);
            var args=new object?[]{game,null};InvokeBoundary("TeardownPrefix",args);var witness=(OwnedReservoirLoadBodyInvocation)args[1]!;
            InvokeBoundary("GameBodyEntry",game);InvokeBoundary("GameBodyReturn",game);
            // A composed transpiler can throw after the pre-ret marker; a
            // foreign finalizer may suppress that exception. It cannot cause
            // Harmony's exception path to run the normal-path postfix.
            if(mode!="later-throw-swallowed")InvokeBoundary("BodyPostfix",witness,mode!="skip-original");
            if(mode=="duplicate")InvokeBoundary("BodyPostfix",witness,true);
            Check(witness.NormalPathCertified==(mode is "normal" or "duplicate"));
            Check(InvokeBoundary("BodyFinalizer",witness,null)==null);
            Check(witness.Closed);
            if(mode=="normal")Check(!t.IsFatal && t.Last!.TeardownCompleted && !t.Last.TeardownFailed);
            else Check(t.IsFatal && t.Current!.TeardownFailed && !t.Current.TeardownCompleted);
            // Re-entered catch-path finalizers and stale postfixes cannot turn
            // a consumed invocation into a second completion.
            InvokeBoundary("BodyPostfix",witness,true);InvokeBoundary("BodyFinalizer",witness,null);
            Check(mode=="normal"?t.Completed.Count==1:t.Completed.Count==0);
        }
        foreach(var body in OwnedReservoirLoadBoundary.GuardContracts().Where(c=>c.Transpiler==BoundaryMethod("BodyWitnessTranspiler")))
        {
            Check(body.Postfix==BoundaryMethod("BodyPostfix") && body.Finalizer==BoundaryMethod("BodyFinalizer"));
            Check(body.Postfix!.GetParameters().Single(p=>p.Name=="__runOriginal").ParameterType==typeof(bool));
        }
    }
    private static List<CodeInstruction> TransformBody(IEnumerable<CodeInstruction> code,MethodInfo target)
        => new((IEnumerable<CodeInstruction>)InvokeBoundary("BodyWitnessTranspiler",code,target)!);
    private static void BodyWitnessPlacement()
    {
        var bodies=OwnedReservoirLoadBoundary.GuardContracts().Where(c=>c.Transpiler==BoundaryMethod("BodyWitnessTranspiler")).ToArray();Check(bodies.Length==3);
        foreach(var body in bodies)
        {
            var code=PatchProcessor.GetOriginalInstructions(body.Target).ToList();var rets=code.Count(i=>i.opcode==System.Reflection.Emit.OpCodes.Ret);
            var dynamic=new System.Reflection.Emit.DynamicMethod("UnexecutedWitnessLabel",typeof(void),Type.EmptyTypes);var label=(object)dynamic.GetILGenerator().DefineLabel();
            InstructionLabels(code.First(i=>i.opcode==System.Reflection.Emit.OpCodes.Ret)).Add(label);
            var result=TransformBody(code,body.Target);
            Check(result.Count==code.Count+2+2*rets && result[0].opcode==System.Reflection.Emit.OpCodes.Ldarg_0);
            Check(result[1].operand is MethodInfo entry && entry.Name.EndsWith("BodyEntry",StringComparison.Ordinal));
            Check(result.Count(i=>i.operand is MethodInfo m && m.DeclaringType==typeof(OwnedReservoirLoadBoundary)&&m.Name.EndsWith("BodyReturn",StringComparison.Ordinal))==rets);
            var retIndex=result.FindIndex(i=>i.opcode==System.Reflection.Emit.OpCodes.Ret);
            Check(result[retIndex-2].opcode==System.Reflection.Emit.OpCodes.Ldarg_0 && InstructionLabels(result[retIndex-2]).Contains(label) && !InstructionLabels(result[retIndex]).Contains(label));
            Check(InstructionLabels(code.First(i=>i.opcode==System.Reflection.Emit.OpCodes.Ret)).Contains(label),"Do not mutate original instructions");
            var stripped=result.Where(i=>!(i.operand is MethodInfo m && m.DeclaringType==typeof(OwnedReservoirLoadBoundary)&&
                (m.Name.EndsWith("BodyEntry",StringComparison.Ordinal)||m.Name.EndsWith("BodyReturn",StringComparison.Ordinal)))).ToList();
            Check(stripped.Count==code.Count+1+rets,"Only callback argument loads and calls were added");
        }
    }
    private static void BodyWitnessRejection()
    {
        var target=typeof(Game).GetMethod("DestroyInstances",BindingFlags.NonPublic|BindingFlags.Instance)!;
        var code=PatchProcessor.GetOriginalInstructions(target).ToList();var once=TransformBody(code,target);
        foreach(var action in new System.Action[]{()=>TransformBody(once,target),()=>TransformBody(new[]{new CodeInstruction(System.Reflection.Emit.OpCodes.Nop)},target),
            ()=>TransformBody(code,typeof(Game).GetMethod("SetIsLoading")!)})
        { try{action();throw new Exception("Expected unsupported witness composition");}catch(TargetInvocationException error){Check(error.InnerException is NotSupportedException);} }
        code[0].blocks.Add(new ExceptionBlock(ExceptionBlockType.BeginExceptionBlock));
        try{TransformBody(code,target);throw new Exception("Expected EH rejection");}catch(TargetInvocationException error){Check(error.InnerException is NotSupportedException);}
    }
    private static void OnSpawnHoldWiring()
    {
        var prefix=PatchProcessor.GetOriginalInstructions(BoundaryMethod("OnSpawnPrefix")).ToArray();
        var acquire=Array.FindIndex(prefix,i=>i.operand is ConstructorInfo c&&c.DeclaringType==typeof(OwnedReservoirOnSpawnCall));
        var path=Array.FindIndex(prefix,i=>Calls(i,typeof(SaveLoader),"GetActiveSaveFilePath"));Check(acquire>=0 && acquire<path);
        var recursive=Array.FindIndex(prefix,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"FailBodyWitness"));
        Check(recursive>=0 && recursive<acquire && prefix.Take(acquire).Any(i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"HoldOnSpawnCall")),"Recursive/foreign OnSpawn retains outer hold and fails closed");
        var finalizer=PatchProcessor.GetOriginalInstructions(BoundaryMethod("OnSpawnFinalizer")).ToArray();
        Check(Array.FindIndex(finalizer,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"ReleaseOnSpawnCall"))<Array.FindIndex(finalizer,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"TryCompletePreGameScene")));
        var t=new OwnedReservoirLoadAttemptTracker();var a=PreGameAttempt(t,spawn:true);t.FailBodyWitness(a,"recursive B under outer A");t.InvalidatePreGame(a,"unsupported recursive owner");
        Check(t.Current!.OnSpawnCallPending && !t.ReadyToClearOwnerReference(a) && !t.ReleaseOnSpawnCall(a) && t.IsFatal);
    }
    private static void LateGameInvalidation()
    {
        var t=new OwnedReservoirLoadAttemptTracker();var a=PreGameAttempt(t);using var state=new BoundaryState(t,null,ManagedShell<SaveLoader>());
        Check(InvokeBoundary("LateGamePrefabPrefix",ManagedShell<Game>()) is false && t.Current!.PreGameInvalidated && t.Current.TeardownFailed);
        Check(t.BeginPreGameBody(a,OwnedReservoirLoadBodyKind.LoaderOnDestroy)==null && t.BeginTeardown(a)==null);
        Check(t.Current!.CompletionKind==OwnedReservoirLoadCompletionKind.PreGameLoaderScene);
    }
    private static void EmptyAnimationDecision()
    {
        var t=new OwnedReservoirLoadAttemptTracker();var a=PreGameAttempt(t);var loader=ManagedShell<SaveLoader>();var updater=ManagedShell<KBatchedAnimUpdater>();var other=ManagedShell<KBatchedAnimUpdater>();
        var capture=TestCapture(a,loader,updater);
        bool Fence(OwnedReservoirLoadAttempt? current,OwnedReservoirPreGameSceneCapture? currentCapture,KBatchedAnimUpdater value,KBatchedAnimUpdater? singleton,Array? grid,int thread=17)
            => (bool)InvokeBoundary("ShouldFenceEmptyAnimationGrid",true,17,thread,current,currentCapture,value,singleton,grid)!;
        foreach(var grid in new Array[]{new object[0,0],new object[0,7],new object[9,0]})Check(Fence(t.Current,capture,updater,updater,grid) && grid.Length==0);
        Check(!Fence(t.Current,capture,updater,updater,null) && !Fence(t.Current,capture,updater,updater,new object[1,1]) && !Fence(t.Current,capture,updater,updater,new object[0]));
        Check(!Fence(t.Current,capture,other,updater,new object[0,0]) && !Fence(t.Current,capture,updater,other,new object[0,0]) && !Fence(t.Current,capture,updater,updater,new object[0,0],18));
        Check(!Fence(null,capture,updater,updater,new object[0,0]));
        var foreign=new OwnedReservoirLoadAttemptTracker();var f=PreGameAttempt(foreign);Check(!Fence(foreign.Current,capture,updater,updater,new object[0,0]));
        var game=new OwnedReservoirLoadAttemptTracker().Reject("Game",Invalid());Check(!Fence(game,capture,updater,updater,new object[0,0]));
    }
    private static void CaptureReentry()
    {
        var selected=Selected();var spawn=new OwnedReservoirOnSpawnCall();var game=ManagedShell<Game>();var loader=ManagedShell<SaveLoader>();var updater=ManagedShell<KBatchedAnimUpdater>();
        bool Same(OwnedReservoirSelectedCall? observedSelected,OwnedReservoirOnSpawnCall? observedSpawn,Game? observedGame,SaveLoader? observedLoader,object? observedUpdater)
            => (bool)InvokeBoundary("SameFailureCapture",selected,observedSelected,spawn,observedSpawn,game,observedGame,loader,observedLoader,updater,observedUpdater)!;
        Check(Same(selected,spawn,game,loader,updater));
        Check(!Same(Selected(),spawn,game,loader,updater)&&!Same(selected,new OwnedReservoirOnSpawnCall(),game,loader,updater)&&!Same(selected,spawn,ManagedShell<Game>(),loader,updater));
        Check(!Same(selected,spawn,game,ManagedShell<SaveLoader>(),updater)&&!Same(selected,spawn,game,loader,ManagedShell<KBatchedAnimUpdater>()));
        var capture=PatchProcessor.GetOriginalInstructions(BoundaryMethod("CaptureFailureOwner")).ToArray();
        var current=Array.FindLastIndex(capture,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"get_Current"));
        Check(current>Array.FindLastIndex(capture,i=>Calls(i,typeof(Game),"get_Instance")) && current>Array.FindLastIndex(capture,i=>Calls(i,typeof(SaveLoader),"get_Instance")));
        var animation=PatchProcessor.GetOriginalInstructions(BoundaryMethod("EmptyAnimationGridPrefix")).ToArray();
        var singleton=Array.FindIndex(animation,i=>i.operand is MethodInfo m && m.Name=="get_Instance"&&m.DeclaringType==typeof(Singleton<KBatchedAnimUpdater>));
        var after=Array.FindLastIndex(animation,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"get_Current"));
        var decision=Array.FindIndex(animation,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"ShouldFenceEmptyAnimationGrid"));
        Check(singleton>=0 && singleton<after && after<decision,"Revalidate attempt/capture after the potentially patched singleton getter");
    }
    private static void ExactLoaderReferenceCas()
    {
        var old=ManagedShell<SaveLoader>();var successor=ManagedShell<SaveLoader>();
        var values=new object?[]{old,old};Check(InvokeBoundary("ClearExactLoaderReference",values)!.ToString()=="Cleared" && values[0]==null);
        values=new object?[]{null,old};Check(InvokeBoundary("ClearExactLoaderReference",values)!.ToString()=="AlreadyNull" && values[0]==null);
        values=new object?[]{successor,old};Check(InvokeBoundary("ClearExactLoaderReference",values)!.ToString()=="Foreign" && ReferenceEquals(values[0],successor));
        Check(InvokeBoundary("ClearExactLoaderReference",values)!.ToString()=="Foreign" && ReferenceEquals(values[0],successor));
        InvokeBoundary("RequirePreGameContracts"); // Metadata/IL only, no actual native field accessor invoked.
    }
    private static void PreGameFinalizationAuthority()
    {
        var t=new OwnedReservoirLoadAttemptTracker();var a=PreGameAttempt(t);var loader=ManagedShell<SaveLoader>();var updater=ManagedShell<KBatchedAnimUpdater>();var capture=TestCapture(a,loader,updater);
        bool Same(OwnedReservoirLoadAttempt? current,OwnedReservoirPreGameSceneCapture? observedCapture,SaveLoader? observedOwner,Game? observedGame=null,bool exiting=false,int thread=17,bool installed=true)
            => (bool)InvokeBoundary("SamePreGameFinalizationAuthority",installed,17,thread,a,current,capture,observedCapture,observedOwner,observedGame,exiting)!;
        var destroy=t.BeginPreGameBody(a,OwnedReservoirLoadBodyKind.LoaderOnDestroy)!;var scene=t.BeginPreGameBody(a,OwnedReservoirLoadBodyKind.SceneUnloaded)!;
        Check(t.CompletePreGameBody(destroy,true,true,null) && t.CompletePreGameBody(scene,true,true,null));
        Check(Same(t.Current,capture,loader) && t.ReadyToClearOwnerReference(a));
        Check(!Same(t.Current,TestCapture(a,loader,updater),loader) && !Same(t.Current,capture,ManagedShell<SaveLoader>()));
        Check(!Same(t.Current,capture,loader,ManagedShell<Game>()) && !Same(t.Current,capture,loader,exiting:true));
        Check(!Same(t.Current,capture,loader,thread:18) && !Same(t.Current,capture,loader,installed:false) && !Same(null,capture,loader));
        // A patched getter can add a hold without replacing identity. The
        // second Ready check, after those getters, must still prevent CAS.
        Check(t.HoldOnSpawnCall(a) && Same(t.Current,capture,loader) && !t.ReadyToClearOwnerReference(a));
        Check(!t.ReleaseOnSpawnCall(a) && t.ReadyToClearOwnerReference(a));
        t.InvalidatePreGame(a,"getter callback invalidated cleanup");Check(Same(t.Current,capture,loader) && !t.ReadyToClearOwnerReference(a));
        var other=new OwnedReservoirLoadAttemptTracker();var successor=PreGameAttempt(other);Check(!Same(successor,capture,loader));
    }
    private static void PreGameFinalizationWiring()
    {
        var code=PatchProcessor.GetOriginalInstructions(BoundaryMethod("TryCompletePreGameScene")).ToArray();
        var ready=Array.FindIndex(code,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"ReadyToClearOwnerReference"));
        var clear=Array.FindIndex(code,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"ClearExactLoaderReference"));
        var complete=Array.FindIndex(code,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"CompletePreGameOwnerReference"));
        var observations=Math.Max(Array.FindLastIndex(code,i=>Calls(i,typeof(Game),"get_Instance")),Array.FindLastIndex(code,i=>Calls(i,typeof(App),"get_IsExiting")));
        var authority=Array.FindIndex(code,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"SamePreGameFinalizationAuthority"));
        var recheck=Array.FindIndex(code,authority+1,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"ReadyToClearOwnerReference"));
        Check(ready>=0 && ready<observations && observations<authority && authority<recheck && recheck<clear && clear<complete && code.Count(i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"ReadyToClearOwnerReference"))==3);
        Check(!code.Skip(recheck+1).Take(clear-recheck-1).Any(i=>Calls(i,typeof(Game),"get_Instance")||Calls(i,typeof(App),"get_IsExiting")),"No foreign getter follows final authority/hold checks before CAS");
        Check(!code.Any(i=>Calls(i,typeof(SaveLoader),"DestroyInstance")) && !code.Any(i=>Calls(i,typeof(Sim),"Shutdown")));
        var install=PatchProcessor.GetOriginalInstructions(typeof(OwnedReservoirLoadBoundary).GetMethod("Install",BindingFlags.NonPublic|BindingFlags.Static)!).ToArray();
        Check(install.Any(i=>i.opcode==System.Reflection.Emit.OpCodes.Ldstr&&Equals(i.operand,"a9613c2b0f1258ac7f7d9323181f816f5849beb24114e4a7665037fd11ddd488")),"New Unity body callbacks require the exact module pin");
        Check(install.Any(i=>i.opcode==System.Reflection.Emit.OpCodes.Ldstr&&Equals(i.operand,"aec7446028fe31d00dfecc684e40011c4208bb036e82617b8de32002a8e55b53")),"Normal-path certificate requires the inspected Harmony emitter pin");
        foreach(var type in new[]{typeof(OwnedReservoirPreGameSceneCapture),typeof(OwnedReservoirEmptyAnimationGridSuppression)})
            Check(type.GetProperties(BindingFlags.Public|BindingFlags.Instance).All(p=>p.SetMethod==null));
    }
    private static void ReentrantSelection()
    {var t=new OwnedReservoirLoadAttemptTracker();var a=t.Reject("first",Invalid());var b=t.Reject("reentry",OwnedReservoirLoadHeaderV1.Inspect(Header(OwnedJson())));Check(a.Id==b.Id && b.Sequence==1 && b.Input.Kind==OwnedReservoirLoadInputKind.InvalidHeader && t.IsFatal);}
    private static OwnedReservoirSelectedCall Selected() => new OwnedReservoirSelectedCall("private selected save.sav");
    private static void SelectedFalseResults()
    {
        var t=new OwnedReservoirLoadAttemptTracker();var scope=Selected();
        Check(t.ObserveSelectedResult(null,"new-colony",false)==null && !t.IsFatal);
        Check(t.ObserveSelectedResult(scope,"outer",true)==null && !t.IsFatal);
        var failed=t.ObserveSelectedResult(scope,"Load(IReader)",false)!;
        Check(failed.Input.Kind==OwnedReservoirLoadInputKind.ReadError && !failed.Input.ReservedNamespaceObserved
            && failed.SelectedCallPending && failed.Origin=="Load(IReader):false-result" && t.IsFatal);
        Check(t.TryRequestFrontend(failed) && !t.TryRequestFrontend(failed));
        var outer=t.ObserveSelectedResult(scope,"Load(string)",true)!;
        Check(ReferenceEquals(failed.Identity,outer.Identity) && !failed.FrontendRequested && outer.FrontendRequested);
        var caught=new OwnedReservoirLoadAttemptTracker();var outerFalse=caught.ObserveSelectedResult(scope,"Load(string)",false)!;
        Check(outerFalse.Origin=="Load(string):false-result" && outerFalse.SelectedCallPending && caught.IsFatal);
        var token=t.BeginTeardown(outer)!;Check(!t.CompleteTeardown(token,null) && t.IsFatal);
        Check(t.ReleaseSelectedCall(outer) && !t.IsFatal);
    }
    private static MethodInfo BoundaryMethod(string name)
        => typeof(OwnedReservoirLoadBoundary).GetMethod(name,BindingFlags.NonPublic|BindingFlags.Static)!;
    private static object? InvokeBoundary(string name,params object?[] args) => BoundaryMethod(name).Invoke(null,args);
    private static bool Calls(CodeInstruction instruction,Type owner,string name)
        => instruction.operand is MethodBase method && method.DeclaringType==owner && method.Name==name;
    private static void EarlyRejectedInvocation()
    {
        Throws<ArgumentException>(()=>new OwnedReservoirSelectedCall(""));
        var tracker=new OwnedReservoirLoadAttemptTracker();var call=Selected();
        var rejected=tracker.Reject("owned header before original loader",Invalid(),selectedCallPending:true);
        Check(tracker.TryRequestFrontend(rejected));var token=tracker.BeginTeardown(rejected)!;
        Check(!tracker.CompleteTeardown(token,null) && tracker.Current!.TeardownCompleted && tracker.IsFatal);
        using var state=new BoundaryState(tracker,ManagedShell<Game>(),ManagedShell<SaveLoader>(),call,rejected);
        state.Set("selected",null); // The rejected header never enters ordinary native reading.
        InvokeBoundary("CompleteSelected",Selected());Check(tracker.IsFatal);
        InvokeBoundary("CompleteSelected",call);Check(!tracker.IsFatal && tracker.Last!.TeardownCompleted);
        var held=new OwnedReservoirLoadAttemptTracker();var prior=held.Reject("OnSpawn rejection",Invalid());
        var attached=held.HoldSelectedCall(prior);Check(attached.SelectedCallPending && !prior.SelectedCallPending);
        var cleanup=held.BeginTeardown(prior)!;Check(!held.CompleteTeardown(cleanup,null) && held.IsFatal);
        Check(held.ReleaseSelectedCall(attached));
        Throws<InvalidOperationException>(()=>held.HoldSelectedCall(prior));
        var prefix=PatchProcessor.GetOriginalInstructions(BoundaryMethod("LoadFilePrefix")).ToArray();
        var create=Array.FindIndex(prefix,i=>i.operand is ConstructorInfo c && c.DeclaringType==typeof(OwnedReservoirSelectedCall));
        var inspect=Array.FindIndex(prefix,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"InspectSelection"));
        Check(create>=0 && inspect>create,"Exact call token must exist before preflight can request synchronous frontend/teardown");
        var nativeInspect=typeof(OwnedReservoirLoadBoundary).GetMethods(BindingFlags.NonPublic|BindingFlags.Static).Single(m=>m.Name=="InspectSelection"&&m.GetParameters().Length==4);
        var il=PatchProcessor.GetOriginalInstructions(nativeInspect).ToArray();
        var reject=Array.FindIndex(il,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"BeginFailure"));
        Check(reject>=0 && il.Skip(reject+1).TakeWhile(i=>!Calls(i,typeof(OwnedReservoirLoadBoundary),"RequestFrontend"))
            .Any(i=>i.opcode==System.Reflection.Emit.OpCodes.Stsfld && i.operand is FieldInfo f && f.Name=="selectedFailure"),
            "Early rejection must retain failure before requesting a potentially synchronous frontend callback");
    }
    private static void ResultHookWiring()
    {
        var il=PatchProcessor.GetOriginalInstructions(BoundaryMethod("LoadResultPostfix")).ToArray();
        var decision=Array.FindIndex(il,i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"ObserveSelectedResult"));
        var request=Array.FindIndex(il,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"RequestFrontend"));
        Check(decision>0 && request>decision && il.Skip(decision+1).Any(i=>i.opcode==System.Reflection.Emit.OpCodes.Stind_I1),
            "Real result hook must decide selected false before fatal early-return, request frontend, and force the bool false");
        Check(!il.Take(decision).Any(i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"get_Current")),
            "No absent-current early return may bypass the false-result decision");
        var hooks=OwnedReservoirLoadBoundary.GuardContracts();
        Check(hooks.Single(c=>c.Target.DeclaringType==typeof(SaveLoader)&&c.Target.Name=="Load"&&c.Target.GetParameters()[0].ParameterType==typeof(IReader)).Postfix==BoundaryMethod("LoadResultPostfix"));
        Check(PatchProcessor.GetOriginalInstructions(BoundaryMethod("LoadFilePostfix")).Any(i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"LoadResultPostfix")));
    }
    private static void DeserializationContinuation()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();var game=ManagedShell<Game>();using var state=new BoundaryState(tracker,game);
        var hooks=OwnedReservoirLoadBoundary.GuardContracts();var manager=hooks.Single(c=>c.Target.DeclaringType==typeof(SaveManager)&&c.Target.Name=="Load");
        var load=hooks.Single(c=>c.Target.DeclaringType==typeof(Game)&&c.Target.Name=="Load");
        Check(manager.Postfix==BoundaryMethod("ManagerLoadResultPostfix") && load.Prefix==BoundaryMethod("RejectLoadContinuation") && load.Postfix==load.Prefix);
        var ordinary=new object?[]{manager.Target,true};InvokeBoundary("ManagerLoadResultPostfix",ordinary);Check(ordinary[1] is true);
        InvokeBoundary("RejectLoadContinuation",load.Target); // No fatal: native path remains available.
        tracker.Reject("last saved-root or Game.OnLoad callback",Invalid());
        var caught=new object?[]{manager.Target,true};InvokeBoundary("ManagerLoadResultPostfix",caught);Check(caught[1] is false && tracker.IsFatal);
        foreach(var boundary in new[]{load.Prefix,load.Postfix})
        {
            try{boundary!.Invoke(null,new object?[]{load.Target});throw new Exception("Fatal Game.Load entry/exit continued");}
            catch(TargetInvocationException error){Check(error.InnerException is InvalidDataException && tracker.IsFatal);}
        }
        var il=PatchProcessor.GetOriginalInstructions(BoundaryMethod("RejectLoadContinuation")).ToArray();
        Check(il.Any(i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"Blocked")) && il.Any(i=>i.opcode==System.Reflection.Emit.OpCodes.Throw));
    }
    // These managed shells are never constructed/spawned, compared through
    // Unity operators, or passed to Unity/native APIs. Only our identity checks
    // and SaveLoader's managed bool fields are exercised.
    private static T ManagedShell<T>() where T:class => (T)RuntimeHelpers.GetUninitializedObject(typeof(T));
    private sealed class BoundaryState : IDisposable
    {
        private readonly Dictionary<FieldInfo,object?> saved=new();
        private readonly List<OwnedReservoirLoadBodyInvocation> bodies;
        private readonly OwnedReservoirLoadBodyInvocation[] priorBodies;
        internal BoundaryState(OwnedReservoirLoadAttemptTracker tracker,Game? game,SaveLoader? loader=null,
            OwnedReservoirSelectedCall? scope=null,OwnedReservoirLoadAttempt? failure=null)
        {
            foreach(var name in new[]{"attempts","ownerGame","ownerLoader","selectedCall","selected","selectedLoader","selectedGame","selectedFailure","activationThread","<Installed>k__BackingField","onSpawnCall","onSpawnLoader","<LastPreGameSceneCapture>k__BackingField"})
            {var field=typeof(OwnedReservoirLoadBoundary).GetField(name,BindingFlags.NonPublic|BindingFlags.Static)!;saved.Add(field,field.GetValue(null));}
            Set("attempts",tracker);Set("ownerGame",game);Set("ownerLoader",loader);Set("selectedCall",scope);
            Set("selected",scope==null?null:new OwnedReservoirSelectedLoad(OwnedReservoirLoadHeaderV1.Inspect(Header("{}"))).WithVerifiedHeader());
            Set("selectedLoader",loader);Set("selectedGame",game);
            Set("selectedFailure",failure);Set("activationThread",Environment.CurrentManagedThreadId);Set("<Installed>k__BackingField",true);
            Set("onSpawnCall",null);Set("onSpawnLoader",null);Set("<LastPreGameSceneCapture>k__BackingField",null);
            bodies=(List<OwnedReservoirLoadBodyInvocation>)typeof(OwnedReservoirLoadBoundary).GetField("bodyInvocations",BindingFlags.NonPublic|BindingFlags.Static)!.GetValue(null)!;
            priorBodies=bodies.ToArray();bodies.Clear();
        }
        internal void Set(string name,object? value) => saved.Keys.Single(f=>f.Name==name).SetValue(null,value);
        public void Dispose(){bodies.Clear();bodies.AddRange(priorBodies);foreach(var row in saved)row.Key.SetValue(null,row.Value);}
    }
    private static void NestedTeardownHooks()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();tracker.Reject("outer",Invalid());var game=ManagedShell<Game>();
        using var state=new BoundaryState(tracker,game);
        var foreign=new object?[]{ManagedShell<Game>(),null};InvokeBoundary("TeardownPrefix",foreign);Check(foreign[1]==null && !tracker.Current!.TeardownStarted);
        var outer=new object?[]{game,null};InvokeBoundary("TeardownPrefix",outer);Check(outer[1] is OwnedReservoirLoadBodyInvocation {Completion:not null});
        InvokeBoundary("GameBodyEntry",game);
        var inner=new object?[]{game,null};InvokeBoundary("TeardownPrefix",inner);Check(inner[1] is OwnedReservoirLoadBodyInvocation {Completion:null});
        InvokeBoundary("GameBodyEntry",game);InvokeBoundary("GameBodyReturn",game);
        InvokeBoundary("BodyPostfix",inner[1],true);InvokeBoundary("BodyFinalizer",inner[1],null);Check(tracker.IsFatal && !tracker.Current!.TeardownCompleted);
        Check(((OwnedReservoirLoadBodyInvocation)outer[1]!).Entered && !((OwnedReservoirLoadBodyInvocation)outer[1]!).Returned,"Inner return cannot mark outer body returned");
        InvokeBoundary("GameBodyReturn",game);InvokeBoundary("BodyPostfix",outer[1],true);InvokeBoundary("BodyFinalizer",outer[1],null);Check(!tracker.IsFatal && tracker.Last!.TeardownCompleted && !tracker.Last.TeardownFailed);
    }
    private static void FailedTeardownHooks()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();tracker.Reject("outer",Invalid());var game=ManagedShell<Game>();
        using var state=new BoundaryState(tracker,game);
        var outer=new object?[]{game,null};InvokeBoundary("TeardownPrefix",outer);
        InvokeBoundary("GameBodyEntry",game);
        var inner=new object?[]{game,null};InvokeBoundary("TeardownPrefix",inner);Check(inner[1] is OwnedReservoirLoadBodyInvocation {Completion:null});
        InvokeBoundary("GameBodyEntry",game);InvokeBoundary("GameBodyReturn",game);InvokeBoundary("BodyPostfix",inner[1],true);InvokeBoundary("BodyFinalizer",inner[1],null);
        var error=new IOException("outer cleanup failed");Check(ReferenceEquals(error,InvokeBoundary("BodyFinalizer",outer[1],error)));
        InvokeBoundary("BodyFinalizer",inner[1],null);InvokeBoundary("BodyFinalizer",outer[1],null);
        var retry=new object?[]{game,null};InvokeBoundary("TeardownPrefix",retry);
        Check(retry[1] is OwnedReservoirLoadBodyInvocation {Completion:null} && tracker.IsFatal && tracker.Current!.TeardownFailed && !tracker.Current.TeardownCompleted && tracker.Completed.Count==0);
        InvokeBoundary("GameBodyEntry",game);InvokeBoundary("GameBodyReturn",game);InvokeBoundary("BodyPostfix",retry[1],true);InvokeBoundary("BodyFinalizer",retry[1],null);
    }
    private static void FatalResultHook()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();var scope=Selected();
        var failure=tracker.ObserveSelectedResult(scope,"actual inner false",false)!;
        Check(tracker.TryRequestFrontend(failure)); // No external callback on the host.
        var loader=ManagedShell<SaveLoader>();using var state=new BoundaryState(tracker,ManagedShell<Game>(),loader,scope,failure);
        var outer=typeof(SaveLoader).GetMethod("Load",new[]{typeof(string)})!;
        var args=new object?[]{loader,true,outer};InvokeBoundary("LoadResultPostfix",args);
        Check(args[1] is false && tracker.IsFatal && tracker.Current!.FrontendRequested && tracker.Current.SelectedCallPending);
        Check((bool)typeof(SaveLoader).GetField("mustRestartOnFail",BindingFlags.NonPublic|BindingFlags.Instance)!.GetValue(loader)!
            && (bool)typeof(SaveLoader).GetField("saveFileCorrupt",BindingFlags.NonPublic|BindingFlags.Instance)!.GetValue(loader)!);
        var inner=typeof(SaveLoader).GetMethod("Load",BindingFlags.NonPublic|BindingFlags.Instance,null,new[]{typeof(IReader)},null)!;
        args=new object?[]{loader,false,inner};InvokeBoundary("LoadResultPostfix",args);
        Check(args[1] is false && !tracker.TryRequestFrontend(failure) && tracker.Current!.Id==failure.Id);
    }
    private static void SelectedPathClearWiring()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();var scope=Selected();var failure=tracker.ObserveSelectedResult(scope,"inner",false)!;
        var loader=ManagedShell<SaveLoader>();using var state=new BoundaryState(tracker,ManagedShell<Game>(),loader,scope,failure);
        Check(ReferenceEquals(InvokeBoundary("ExactFailedSelection",loader,scope),tracker.Current));
        Check(InvokeBoundary("ExactFailedSelection",ManagedShell<SaveLoader>(),scope)==null && InvokeBoundary("ExactFailedSelection",loader,Selected())==null);
        var token=tracker.BeginTeardown(failure)!;Check(!tracker.CompleteTeardown(token,null));
        Check(InvokeBoundary("ExactFailedSelection",loader,scope)!=null && tracker.ReleaseSelectedCall(failure));
        var next=tracker.Reject("successor",Invalid(),selectedCallPending:true);
        Check(next.Id!=failure.Id && InvokeBoundary("ExactFailedSelection",loader,scope)==null);
        var clear=PatchProcessor.GetOriginalInstructions(BoundaryMethod("ClearFailedSelection")).ToArray();
        var gate=Array.FindIndex(clear,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"ExactFailedSelection"));
        var setter=Array.FindIndex(clear,i=>Calls(i,typeof(SaveLoader),"SetActiveSaveFilePath"));
        Check(gate>=0 && setter>gate && clear[setter-1].opcode==System.Reflection.Emit.OpCodes.Ldnull);
        foreach(var name in new[]{"LoadFilePostfix","LoadFileFinalizer"})
        {
            var il=PatchProcessor.GetOriginalInstructions(BoundaryMethod(name)).ToArray();
            var clears=Array.FindIndex(il,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"ClearFailedSelection"));Check(clears>=0);
            if(name=="LoadFileFinalizer")Check(clears<Array.FindIndex(il,i=>Calls(i,typeof(OwnedReservoirLoadBoundary),"CompleteSelected")),"Pointer must clear before releasing the exact original call");
        }
    }
    private static void OnSpawnContinuationDecision()
    {
        var tracker=new OwnedReservoirLoadAttemptTracker();var loader=ManagedShell<SaveLoader>();
        using var state=new BoundaryState(tracker,ManagedShell<Game>(),loader);
        var setter=AccessTools.PropertySetter(typeof(SaveLoader),"loadedFromSave")!;
        setter.Invoke(loader,new object?[]{true});Check(InvokeBoundary("StopFailedOnSpawnContinuation",loader) is false);
        setter.Invoke(loader,new object?[]{false});Check(InvokeBoundary("StopFailedOnSpawnContinuation",loader) is true);
        var failed=tracker.Reject("false loader before OnSpawn setter",Invalid());var token=tracker.BeginTeardown(failed)!;
        Check(tracker.CompleteTeardown(token,null) && !tracker.IsFatal);
        Check(InvokeBoundary("StopFailedOnSpawnContinuation",loader) is true,"A synchronous completed teardown cannot revive native false continuation");
        state.Set("<Installed>k__BackingField",false);Check(InvokeBoundary("StopFailedOnSpawnContinuation",loader) is false);
        var il=PatchProcessor.GetOriginalInstructions(BoundaryMethod("StopFailedOnSpawnContinuation")).ToArray();
        Check(il.Any(i=>Calls(i,typeof(SaveLoader),"get_loadedFromSave")) && !il.Any(i=>Calls(i,typeof(OwnedReservoirLoadAttemptTracker),"get_Current")));
    }
    private static System.Collections.IList InstructionLabels(CodeInstruction instruction)
        => (System.Collections.IList)typeof(CodeInstruction).GetField("labels")!.GetValue(instruction)!;
    private static (List<CodeInstruction> code,System.Reflection.Emit.ILGenerator generator,MethodInfo target) OnSpawnInstructions()
    {
        var target=typeof(SaveLoader).GetMethod("OnSpawn",BindingFlags.NonPublic|BindingFlags.Instance)!;
        var dynamic=new System.Reflection.Emit.DynamicMethod("InspectOnlyOnSpawn",typeof(void),new[]{typeof(SaveLoader)},typeof(Program).Module,true);
        var generator=dynamic.GetILGenerator();
        var reader=typeof(PatchProcessor).GetMethods(BindingFlags.Public|BindingFlags.Static).Single(m=>m.Name=="GetOriginalInstructions"&&m.GetParameters().Length==2&&!m.GetParameters()[1].ParameterType.IsByRef);
        return (new List<CodeInstruction>((IEnumerable<CodeInstruction>)reader.Invoke(null,new object?[]{target,generator})!),generator,target);
    }
    private static List<CodeInstruction> TransformOnSpawn(List<CodeInstruction> code,System.Reflection.Emit.ILGenerator generator,MethodInfo target)
        => new List<CodeInstruction>((IEnumerable<CodeInstruction>)InvokeBoundary("OnSpawnContinuationTranspiler",code,generator,target)!);
    private static void OnSpawnContinuationPlacement()
    {
        var (code,generator,target)=OnSpawnInstructions();Check(target.GetMethodBody()!.ExceptionHandlingClauses.Count==0);
        var load=Array.FindIndex(code.ToArray(),i=>Calls(i,typeof(SaveLoader),"Load")&&((MethodInfo)i.operand).GetParameters()[0].ParameterType==typeof(string));
        Check(load>=0 && Calls(code[load+1],typeof(SaveLoader),"set_loadedFromSave"));
        var sentinel=(object)generator.DefineLabel();InstructionLabels(code[load+2]).Add(sentinel);
        var oldLabels=InstructionLabels(code[load+2]).Count;var result=TransformOnSpawn(code,generator,target);
        Check(result.Count==code.Count+4 && result.Count(i=>i.opcode==System.Reflection.Emit.OpCodes.Ret)==code.Count(i=>i.opcode==System.Reflection.Emit.OpCodes.Ret)+1);
        Check(result[load+2].opcode==System.Reflection.Emit.OpCodes.Ldarg_0 && Calls(result[load+3],typeof(OwnedReservoirLoadBoundary),"StopFailedOnSpawnContinuation")
            && result[load+4].opcode==System.Reflection.Emit.OpCodes.Brfalse && result[load+5].opcode==System.Reflection.Emit.OpCodes.Ret);
        Check(InstructionLabels(result[load+6]).Contains(result[load+4].operand) && InstructionLabels(result[load+6]).Contains(sentinel)
            && InstructionLabels(code[load+2]).Count==oldLabels,"Keep original next-instruction labels and avoid mutating input");
        Check(InstructionLabels(result[load+2]).Count==0,"Unrelated native branches must skip the inserted fallthrough guard");
        result.RemoveRange(load+2,4);
        for(var i=0;i<code.Count;++i)Check(result[i].opcode==code[i].opcode && Equals(result[i].operand,code[i].operand),"Original native/no-selection instruction path must remain intact");
    }
    private static void OnSpawnContinuationRejection()
    {
        foreach(var mode in new[]{"separated-setter","duplicate-load","setter-branch","exception-region","wrong-original","already-guarded"})
        {
            var (code,generator,target)=OnSpawnInstructions();
            var load=code.FindIndex(i=>Calls(i,typeof(SaveLoader),"Load")&&((MethodInfo)i.operand).GetParameters()[0].ParameterType==typeof(string));
            if(mode=="separated-setter")code.Insert(load+1,new CodeInstruction(System.Reflection.Emit.OpCodes.Nop));
            if(mode=="duplicate-load")code.Add(new CodeInstruction(code[load].opcode,code[load].operand));
            if(mode=="setter-branch")InstructionLabels(code[load+1]).Add(generator.DefineLabel());
            if(mode=="exception-region")code[0].blocks.Add(new ExceptionBlock(ExceptionBlockType.BeginExceptionBlock));
            if(mode=="wrong-original")target=typeof(Game).GetMethod("OnSpawn",BindingFlags.NonPublic|BindingFlags.Instance)!;
            if(mode=="already-guarded")code=TransformOnSpawn(code,generator,target);
            try{TransformOnSpawn(code,generator,target);throw new Exception("Unsupported OnSpawn composition accepted: "+mode);}
            catch(TargetInvocationException error){Check(error.InnerException is NotSupportedException,mode+": "+error.InnerException);}
        }
    }
    private static void OnSpawnTranspilerContract()
    {
        var hooks=OwnedReservoirLoadBoundary.GuardContracts();var onSpawn=hooks.Single(c=>c.Target.DeclaringType==typeof(SaveLoader)&&c.Target.Name=="OnSpawn");
        Check(hooks.Count(c=>c.Transpiler!=null)==4 && onSpawn.Transpiler==BoundaryMethod("OnSpawnContinuationTranspiler"));
        var parameters=onSpawn.Transpiler!.GetParameters();Check(parameters.Length==3 && parameters[0].ParameterType==typeof(IEnumerable<CodeInstruction>)
            && parameters[1].ParameterType==typeof(System.Reflection.Emit.ILGenerator) && parameters[2].ParameterType==typeof(MethodBase));
        var install=PatchProcessor.GetOriginalInstructions(typeof(OwnedReservoirLoadBoundary).GetMethod("Install",BindingFlags.NonPublic|BindingFlags.Static)!).ToArray();
        Check(install.Count(i=>Calls(i,typeof(OwnedReservoirLoadHook),"get_Transpiler"))==2,"Install and exact-method rollback must both include transpiler");
        Check(install.Any(i=>Calls(i,typeof(Harmony),"Patch")) && install.Any(i=>Calls(i,typeof(Harmony),"Unpatch")));
    }
    private static void Teardown()
    {
        var t=new OwnedReservoirLoadAttemptTracker();var a=t.Reject("first",Invalid());
        Check(!t.CompleteTeardown(new OwnedReservoirLoadTeardown(a.Identity),null) && !t.CompleteTeardown(null!,null));
        var token=t.BeginTeardown(a);Check(token!=null && t.BeginTeardown(a)==null);
        Check(!t.CompleteTeardown(token!,new IOException("injected teardown failure")) && t.IsFatal && t.Current!.TeardownFailed);
        Check(!t.CompleteTeardown(token!,null) && t.BeginTeardown(a)==null && t.IsFatal && !t.Current!.TeardownCompleted);
        var pending=new OwnedReservoirLoadAttemptTracker();var held=pending.Reject("pending",Invalid(),selectedCallPending:true);
        var failed=pending.BeginTeardown(held)!;Check(!pending.CompleteTeardown(failed,new IOException("failed before unwind")));
        Check(!pending.ReleaseSelectedCall(held) && pending.IsFatal && pending.Current!.TeardownFailed && !pending.Current.SelectedCallPending);
    }
    private static void Stale()
    {
        var t=new OwnedReservoirLoadAttemptTracker();var old=t.Reject("first",Invalid());var oldToken=t.BeginTeardown(old)!;Check(t.CompleteTeardown(oldToken,null));
        var next=t.Reject("second",Invalid());var foreignTracker=new OwnedReservoirLoadAttemptTracker();var foreign=foreignTracker.Reject("foreign",Invalid());var foreignToken=foreignTracker.BeginTeardown(foreign)!;
        Check(next.Sequence==2 && next.Id!=old.Id && !t.CompleteTeardown(oldToken,null) && t.BeginTeardown(foreign)==null && t.IsFatal);
        var actual=t.BeginTeardown(next)!;Check(!t.CompleteTeardown(foreignToken,null) && !t.CompleteTeardown(new OwnedReservoirLoadTeardown(next.Identity),null) && t.IsFatal);
        Check(t.CompleteTeardown(actual,null) && !t.CompleteTeardown(actual,null));
        Check(!typeof(OwnedReservoirLoadAttempt).GetInterfaces().Contains(typeof(IDisposable)));
    }
    private static void PendingSelected()
    {var t=new OwnedReservoirLoadAttemptTracker();var a=t.Reject("actual-header",Invalid(),selectedCallPending:true);Check(t.TryRequestFrontend(a));var token=t.BeginTeardown(a)!;Check(!t.CompleteTeardown(token,null) && t.IsFatal && t.Current!.TeardownCompleted && t.Current.SelectedCallPending);Check(t.ReleaseSelectedCall(a) && !t.IsFatal);var next=t.Reject("next",Invalid(),selectedCallPending:true);Check(!t.ReleaseSelectedCall(a) && t.Current!.Id==next.Id);Check(!t.ReleaseSelectedCall(next) && t.IsFatal && !t.Current!.SelectedCallPending);}
    private static void TeardownNoRecursiveFrontend()
    {
        var t=new OwnedReservoirLoadAttemptTracker();var a=t.Reject("teardown-reentry",Invalid(),selectedCallPending:true);
        var token=t.BeginTeardown(a);Check(token!=null && !t.TryRequestFrontend(a) && !t.Current!.FrontendRequested);
        Check(!t.CompleteTeardown(token!,null) && t.IsFatal && !t.TryRequestFrontend(a));Check(t.ReleaseSelectedCall(a));
        var method=typeof(OwnedReservoirLoadBoundary).GetMethod("TeardownPrefix",BindingFlags.NonPublic|BindingFlags.Static)!;
        var il=PatchProcessor.GetOriginalInstructions(method).ToArray();
        var call=Array.FindIndex(il,i=>i.operand is MethodBase m && m.Name=="RejectSelected");
        Check(call>0 && il[call-1].opcode==System.Reflection.Emit.OpCodes.Ldc_I4_0,"Actual teardown prefix must explicitly suppress a recursive frontend request");
    }
    private static void BoundedHistory()
    {var t=new OwnedReservoirLoadAttemptTracker();for(var i=0;i<20;++i){var a=t.Reject("attempt",Invalid());for(var j=0;j<140;++j)t.Record(a,"observation",j.ToString());Check(t.Current!.Observations.Count==128 && t.Current.DroppedObservations>0);var token=t.BeginTeardown(a)!;t.CompleteTeardown(token,null);}Check(t.Completed.Count==16 && t.Completed[0].Sequence==5 && t.Completed[15].Sequence==20);}
    private static void Threading()
    {var t=new OwnedReservoirLoadAttemptTracker();var a=t.Reject("main",Invalid());var error=Task.Run(()=>{Check(t.Current!.Id==a.Id);try{t.Reject("wrong",Invalid());return false;}catch(InvalidOperationException){return true;}}).GetAwaiter().GetResult();Check(error && t.IsFatal && t.Current!.Sequence==1);}
    private static void Contracts()
    {
        var contracts=OwnedReservoirLoadBoundary.GuardContracts();Check(contracts.Count==27);
        foreach(var c in contracts)
        {
            Check(c.Target.GetMethodBody()!=null);
            foreach(var patch in new[]{c.Prefix,c.Postfix,c.Finalizer}.Where(p=>p!=null).Cast<MethodInfo>())
                foreach(var p in patch.GetParameters())
                {
                    var type=p.ParameterType.IsByRef?p.ParameterType.GetElementType()!:p.ParameterType;
                    if(p.Name=="__instance")Check(type==c.Target.DeclaringType);
                    else if(p.Name=="__result")Check(type==c.Target.ReturnType);
                    else if(p.Name=="__originalMethod")Check(type==typeof(MethodBase));
                    else if(p.Name=="__exception")Check(type==typeof(Exception));
                    else if(p.Name=="__runOriginal")Check(type==typeof(bool) && !p.ParameterType.IsByRef);
                    else if(p.Name=="__state")Check(type==typeof(OwnedReservoirLoadBodyInvocation)||type==typeof(OwnedReservoirSelectedCall)||type==typeof(OwnedReservoirOnSpawnCall));
                    else Check(c.Target.GetParameters().Any(x=>x.Name==p.Name && x.ParameterType==p.ParameterType));
                }
            var stateTypes = new[]{c.Prefix,c.Postfix,c.Finalizer}.Where(p=>p!=null).Cast<MethodInfo>().SelectMany(p=>p.GetParameters()).Where(p=>p.Name=="__state").Select(p=>p.ParameterType.IsByRef?p.ParameterType.GetElementType()!:p.ParameterType).Distinct().ToArray();
            Check(stateTypes.Length<=1,"Prefix/postfix/finalizer state types must agree for each exact target");
        }
        Check(contracts.Count(c=>c.Target.DeclaringType==typeof(SaveLoader)&&c.Target.Name=="Load")==2);
        Check(contracts.Single(c=>c.Target.Name=="DestroyInstances").Finalizer!=null);
    }
    private static void PinnedHashes()
    {
        var windows=System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
        var expectedGame=windows?OwnedReservoirLoadHeaderV1.AssemblyCSharpSha256:"6db652990ad1038e86b3493ed43004e12bfd61e62a4be60c787a9a9b9ac52ee1";
        var expectedFirstpass=windows?OwnedReservoirLoadHeaderV1.FirstpassSha256:"117fcc6962eeed82b4929347b9a466fb41e807a01b5a9a7acc8bdc446da276c7";
        Check(Hash(File.ReadAllBytes(typeof(SaveLoader).Assembly.Location))==expectedGame);
        Check(Hash(File.ReadAllBytes(typeof(IReader).Assembly.Location))==expectedFirstpass);
        Check(Hash(File.ReadAllBytes(typeof(Harmony).Assembly.Location))=="aec7446028fe31d00dfecc684e40011c4208bb036e82617b8de32002a8e55b53");
        var expectedUnity=windows?"a9613c2b0f1258ac7f7d9323181f816f5849beb24114e4a7665037fd11ddd488":"9ca8f23e752bd0c945f943c53468666f126faf2625fe8d2955ed787b6dbe47f5";
        Check(Hash(File.ReadAllBytes(typeof(SceneManager).Assembly.Location))==expectedUnity);
        // Linux host checks do not install or broaden the product's current
        // Windows-only assembly/header compatibility pin.
        if(!windows)Check(expectedGame!=OwnedReservoirLoadHeaderV1.AssemblyCSharpSha256 && expectedFirstpass!=OwnedReservoirLoadHeaderV1.FirstpassSha256);
    }
    private static void Dormant()
    {
        Check(!OwnedReservoirLoadBoundary.Installed && !OwnedReservoirLoadBoundary.IsFatal && OwnedReservoirLoadBoundary.CurrentAttempt==null && OwnedReservoirLoadBoundary.RejectionOnly);
        Check(!typeof(OwnedReservoirLoadBoundary).GetCustomAttributes().Any(a=>a is HarmonyPatch));
        typeof(OwnedReservoirLoadBoundary).GetMethod("ActualHeaderPrefix",BindingFlags.NonPublic|BindingFlags.Static)!.Invoke(null,new object?[]{null,"not a native selected filename"});
        foreach(var method in typeof(OwnedReservoirLoadBoundary).GetMethods(BindingFlags.NonPublic|BindingFlags.Static))
            foreach(var instruction in PatchProcessor.GetOriginalInstructions(method))
                if(instruction.operand is MethodBase called) Check(!(called.DeclaringType==typeof(Sim)&&called.Name==nameof(Sim.Shutdown)),"Boundary must not call uninitialized native shutdown");
    }
    private static void PrivateFixture()
    {
        if(fixture==null){Console.WriteLine("NOTE no private malformed .sav supplied; parser fixture is host-created only");return;}
        var before=Hash(File.ReadAllBytes(fixture));var r=OwnedReservoirLoadHeaderV1.InspectFile(fixture);
        Check(r.Kind==OwnedReservoirLoadInputKind.InvalidHeader && r.ReservedNamespaceObserved && r.MustReject);
        Check(before==Hash(File.ReadAllBytes(fixture)) && before=="411194693ac9690f09811f5d312a15f8705bc5fa8493c8d7bd1edc111f7c2733");
    }
}
