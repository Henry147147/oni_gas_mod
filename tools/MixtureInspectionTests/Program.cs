using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using HarmonyLib;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length != 1) throw new ArgumentException("Pass the licensed ONI installation path.");
        var managed = Path.Combine(Path.GetFullPath(args[0]), "OxygenNotIncluded_Data", "Managed");
        AssemblyLoadContext.Default.Resolving += (_, name) =>
        {
            var path = Path.Combine(managed, name.Name + ".dll");
            return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
        };
        return Run();
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Run()
    {
        var tests = new (string name, System.Action test)[]
        {
            ("terminal hover injection preserves every vanilla instruction and building block", HoverContract),
            ("details injection runs before single vanilla Commit", DetailsContract),
            ("unknown IL contracts reject explicitly", RejectUnknownContracts),
            ("complete inspection retains trace mass and partial pressure", FullTrace),
            ("hover lists every non-trace gas without a top-N limit", HoverAllNonTrace),
            ("hover excludes traces while retaining their warnings and full details", CompactContaminant),
            ("hover uses the shared trace threshold including its exact boundary", HoverTraceBoundary),
            ("liquid inspection does not invent gas partial pressure", LiquidRows),
            ("breathing warnings use actual mixture model", BreathingWarnings),
            ("state uses player units and explicit breathing status", PlayerUnitsAndStatus),
            ("breathing warnings precede independently laid out constituent rows", DetailRows),
            ("cached native detail labels are explicitly reordered on refresh", DetailSiblingOrderingContract),
            ("owned container warnings precede unchanged native description across cached refreshes", OwnedContainerDetailPriority),
            ("owned unavailable details precede description while unowned fallback keeps native ordering", OwnedUnavailableAndNativeDetailPriority),
            ("selected header is one line while assessment remains visible", HeaderAndAssessment),
            ("unspawned and out-of-range native ports are safely unavailable", NativePortGuards),
            ("native port bounds races remain unavailable without swallowing other errors", NativePortReadFailures),
            ("detail text layout properties exist in actual game", DetailLayoutContract),
            ("native pipe packet accessor matches installed game", PipeSurface),
            ("reservoir and machine surfaces enumerate IStorage packets", StorageSurfaces),
            ("bottle surface reads actual fluid PrimaryElement properties", BottleSurface),
            ("equipped suit surface follows actual equipment and tank storage", SuitSurface),
            ("vent output port accessor matches installed game", VentSurface),
            ("inspection never constructs or infers a fluid inventory", ProjectionOnly),
            ("owner unregister removes runtime inventory adapter", UnregisterSurface),
            ("owned object binding resolves only its exact completed allocation", OwnedAllocationResolution),
            ("owned object registration creates no independent inventory provider", OwnedBindingSurface),
            ("registered adapter errors have an explicit unavailable path", AdapterFailureSurface),
            ("game teardown clears persistent prefab bindings and cached readings", SessionBindingReset)
        };
        var failed = 0;
        foreach (var (name, test) in tests)
        {
            try { test(); Console.WriteLine("PASS " + name); }
            catch (Exception error) { ++failed; Console.WriteLine("FAIL " + name + ": " + error); }
        }
        return failed == 0 ? 0 : 1;
    }

    private static MethodInfo Transpiler(string type) => typeof(MixtureInspection).Assembly.GetType("MixedAtmospheres.Oni." + type)!
        .GetMethod("Transpiler", BindingFlags.Static | BindingFlags.NonPublic)!;
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static List<CodeInstruction> Apply(string type, MethodBase original, out List<CodeInstruction> before)
    {
        before = PatchProcessor.GetOriginalInstructions(original, out var generator);
        var method = Transpiler(type);
        return ((IEnumerable<CodeInstruction>)method.Invoke(null, method.GetParameters().Length == 2 ? new object[] { before, generator } : new object[] { before })!).ToList();
    }

    private static void HoverContract()
    {
        var original = AccessTools.Method(typeof(SelectToolHoverTextCard), nameof(SelectToolHoverTextCard.UpdateHoverElements));
        var after = Apply("MixtureCellHoverPatch", original, out var before);
        Check(after.Count == before.Count + 11, "Unexpected hover instruction replacement.");
        Check(before.All(item => after.Contains(item)), "A vanilla instruction was removed.");
        var draw = after.FindIndex(item => item.operand is MethodInfo method && method.Name == "TryDrawCell");
        Check(draw > 0 && after[draw + 1].opcode == OpCodes.Brtrue, "Missing conditional owned-cell bypass.");
        var target = (Label)after[draw + 1].operand;
        var destination = after.FindIndex(item => item.labels.Contains(target));
        var mass = after.FindIndex(item => item.Calls(AccessTools.Method(typeof(HoverTextHelper), nameof(HoverTextHelper.MassStringsReadOnly))));
        var wall = after.FindIndex(destination, item => item.operand is MethodInfo method && method.DeclaringType == typeof(BackwallManager) && method.Name == "HasBackwall");
        Check(draw < mass && mass < destination && wall >= destination, "Bypass did not isolate the terminal cell block.");
        var previousEnd = after.FindLastIndex(draw, item => item.operand is MethodInfo method && method.Name == "EndShadowBar");
        Check(previousEnd >= 0 && previousEnd < draw, "Existing selectable shadow bars were not retained.");
        var containers = after.FindIndex(item => item.operand is MethodInfo method && method.Name == "DrawContainers");
        var end = after.FindLastIndex(item => item.Calls(AccessTools.Method(typeof(HoverTextDrawer), nameof(HoverTextDrawer.EndDrawing))));
        Check(containers > destination && containers < end, "Container readout is outside drawing lifetime.");
        Check(MixtureInspection.CellHoverHookReady, "Hover hook diagnostic was not set.");
    }

    private static void DetailsContract()
    {
        var original = AccessTools.Method(typeof(SimpleInfoScreen), "RefreshInfoPanel");
        var after = Apply("MixtureDetailsPanelPatch", original, out var before);
        Check(after.Count == before.Count + 3 && before.All(item => after.Contains(item)), "Details patch removed vanilla content.");
        var append = after.FindIndex(item => item.operand is MethodInfo method && method.Name == "AddDetails");
        var commit = after.FindIndex(item => item.Calls(AccessTools.Method(typeof(CollapsibleDetailContentPanel), nameof(CollapsibleDetailContentPanel.Commit))));
        Check(append > 0 && append + 2 == commit, "Inspection was not inserted immediately before vanilla Commit.");
        Check(MixtureInspection.DetailsHookReady, "Details hook diagnostic was not set.");
    }

    private static void RejectUnknownContracts()
    {
        var method = Transpiler("MixtureDetailsPanelPatch"); var rejected = false;
        try { _ = method.Invoke(null, new object[] { Array.Empty<CodeInstruction>() }); }
        catch (TargetInvocationException error) when (error.InnerException is InvalidOperationException) { rejected = true; }
        Check(rejected, "Unknown details IL was accepted.");
    }

    private static (FluidMaterialCatalog catalog, MixturePresentationModel model) Model()
    {
        var catalog = new FluidMaterialCatalog("inspection-tests-v1", new[]
        {
            new FluidMaterial("oxygen", NativeMatterPhase.Gas, .032, 660),
            new FluidMaterial("nitrogen", NativeMatterPhase.Gas, .028, 740),
            new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, .044, 655),
            new FluidMaterial("helium", NativeMatterPhase.Gas, .004, 3116),
            new FluidMaterial("chlorine", NativeMatterPhase.Gas, .071, 360),
            new FluidMaterial("water", NativeMatterPhase.Liquid, .018, 4184, 997)
        });
        var styles = catalog.Materials.Keys.Select(id => new MixtureSpeciesStyle(id, id, new MixtureSrgbColor(.5, .5, .5)));
        return (catalog, new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide",
            new Dictionary<string, double> { ["chlorine"] = 1e-6 }));
    }

    private static void FullTrace()
    {
        var (catalog, model) = Model();
        var reading = model.Describe(catalog.CreateInventory(new Dictionary<string, double>
        { ["oxygen"] = .23, ["nitrogen"] = .77, ["helium"] = 1e-28 }, 300), 1);
        var text = MixtureInspection.FormatFullComposition(reading);
        Check(text.Contains("1E-28 kg") && text.Contains("helium") && text.Contains("partial pressure"), "Positive trace inventory disappeared.");
        Check(text.Contains("by moles") && text.Contains("300 K") && text.Contains("Gas pressure"), "Required mixture measurements are absent.");
        Check(reading.Constituents.All(row => text.Contains(row.DisplayName)), "Full inspection omitted a species.");
        Check(reading.Constituents.All(row => text.Contains(row.MolesText)), "Full inspection omitted a constituent's mole quantity.");
    }

    private static void CompactContaminant()
    {
        var (catalog, model) = Model();
        var reading = model.Describe(catalog.CreateInventory(new Dictionary<string, double>
        { ["oxygen"] = .23, ["nitrogen"] = .77, ["helium"] = .01, ["chlorine"] = .0001 }, 300), 1);
        var lines = MixtureInspection.FormatCompactComposition(reading);
        Check(reading.GasConstituents.Single(row => row.MaterialId == "chlorine").IsTrace, "Fixture contaminant is not a trace.");
        Check(!lines.Any(line => line.StartsWith("chlorine ", StringComparison.Ordinal)), "Trace leaked into the non-trace contents list.");
        Check(reading.Warnings.Any(warning => warning.Contains("chlorine")), "Omitting a trace discarded its hazard warning.");
        Check(MixtureInspection.FormatFullComposition(reading).Contains("chlorine"), "Expanded details discarded a trace.");
        Check(lines.Any(line => line.Contains("complete composition")), "No expansion hint for omitted species.");
    }

    private static void HoverAllNonTrace()
    {
        var (catalog, model) = Model();
        var fractions = new Dictionary<string, double>
        { ["nitrogen"] = .45, ["oxygen"] = .25, ["carbon_dioxide"] = .15, ["helium"] = .149998, ["chlorine"] = .000002 };
        var inventory = catalog.CreateInventory(fractions.ToDictionary(item => item.Key,
            item => item.Value * catalog.Materials[item.Key].MolarMassKgPerMol), 300);
        var reading = model.Describe(inventory, 1); var lines = MixtureInspection.FormatCompactComposition(reading);
        var gases = reading.GasConstituents.Where(row => !row.IsTrace).ToArray();
        Check(gases.Length == 4, "Fixture does not exceed the old three-gas cap.");
        foreach (var row in gases)
            Check(lines.Any(line => line.StartsWith(row.DisplayName + " " + row.MolePercentText + " by moles", StringComparison.Ordinal)),
                "A non-trace gas or its original mole percentage is missing: " + row.MaterialId);
        Check(lines.Count(line => line.Contains(" by moles")) == gases.Length, "Hover composition duplicated or omitted a gas.");
        Check(gases.Sum(row => row.GasMoleFraction!.Value) < 1, "Fixture lacks excluded trace moles.");
    }

    private static void HoverTraceBoundary()
    {
        var (catalog, model) = Model();
        var inventory = catalog.CreateInventory(new Dictionary<string, double>
        { ["oxygen"] = .032, ["nitrogen"] = .028 * 98, ["helium"] = .004 }, 300);
        var threshold = model.Describe(inventory, 1).GasConstituents.Single(row => row.MaterialId == "oxygen").GasMoleFraction!.Value;
        MixturePresentation At(double value) => new MixturePresentationModel(catalog,
            catalog.Materials.Keys.Select(id => new MixtureSpeciesStyle(id, id, new MixtureSrgbColor(.5, .5, .5))),
            new AtmosphereSettings(), "oxygen", "carbon_dioxide", new Dictionary<string, double>(),
            new MixtureVisualOptions { MeaningfulGasMoleFraction = value }).Describe(inventory, 1);
        var boundary = At(threshold);
        Check(!boundary.GasConstituents.Single(row => row.MaterialId == "oxygen").IsTrace &&
            MixtureInspection.FormatCompactComposition(boundary).Any(line => line.StartsWith("oxygen ", StringComparison.Ordinal)),
            "Gas at the trace threshold was omitted.");
        var above = At(Math.BitIncrement(threshold));
        Check(above.GasConstituents.Single(row => row.MaterialId == "oxygen").IsTrace &&
            !MixtureInspection.FormatCompactComposition(above).Any(line => line.StartsWith("oxygen ", StringComparison.Ordinal)),
            "Hover ignored the shared trace classification.");
    }

    private static void LiquidRows()
    {
        var (catalog, model) = Model();
        var reading = model.Describe(catalog.CreateInventory(new Dictionary<string, double> { ["water"] = 100 }, 300), 1);
        var text = MixtureInspection.FormatFullComposition(reading);
        Check(text.Contains("gas partial pressure not applicable") && text.Contains("No gas phase"), "Liquid was assigned a fake gas partial pressure.");
        Check(reading.Constituents.All(row => text.Contains(row.MolesText)), "Liquid constituent mole amount was omitted.");
    }

    private static void BreathingWarnings()
    {
        var (catalog, model) = Model();
        var reading = model.Describe(catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = .05, ["nitrogen"] = .8, ["carbon_dioxide"] = .15 }, 300), 1);
        var text = MixtureInspection.FormatFullComposition(reading);
        Check(!reading.IsBreathable && reading.Warnings.Count > 0 && reading.Warnings.All(text.Contains), "Mixture breathing warning was discarded.");
        Check(MixtureInspection.FormatState(reading).Contains("Unsafe atmosphere"), "Unsafe mixture lacks explicit status.");
    }

    private static void PlayerUnitsAndStatus()
    {
        var (catalog, model) = Model();
        var moles = 101325 / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300);
        var reading = model.Describe(catalog.CreateInventory(new Dictionary<string, double>
        { ["oxygen"] = .21 * moles * .032, ["nitrogen"] = .79 * moles * .028 }, 300), 1);
        var text = MixtureInspection.FormatState(reading);
        Check(text.Contains("101.325 kPa") && text.Contains("101325 Pa") && text.Contains("26.85 °C (300 K)"),
            "State omitted player pressure/temperature units or exact reference values.");
        Check(reading.IsBreathable && text.Contains("Breathable"), "Safe mixture lacks explicit status.");
        var vacuum = MixtureInspection.FormatState(model.Describe(FluidInventory.Empty, 1));
        Check(vacuum.Contains("Vacuum / no atmosphere") && vacuum.Contains("temperature unavailable"), "Vacuum was assigned a normal temperature or safe status.");
    }

    private static void DetailRows()
    {
        var (catalog, model) = Model();
        var reading = model.Describe(catalog.CreateInventory(new Dictionary<string, double>
        { ["oxygen"] = .1, ["nitrogen"] = .8, ["carbon_dioxide"] = .1, ["chlorine"] = .0001, ["helium"] = 1e-28, ["water"] = 100 }, 300), 1);
        var rows = MixtureInspection.BuildDetailRows(reading).ToList();
        var firstConstituent = rows.FindIndex(row => row.Key.StartsWith("Constituent.", StringComparison.Ordinal));
        Check(firstConstituent > 0 && rows.FindIndex(row => row.Key == "Warnings") < firstConstituent,
            "Breathing warnings remain below the composition fold.");
        Check(reading.Warnings.All(warning => rows.Take(firstConstituent).Any(row => row.Text == warning)), "A warning was hidden after constituent rows.");
        Check(rows.Any(row => row.Key == "GasDensity" && row.Text.Contains("kg/m³")), "Full details omitted gas density.");
        var constituents = rows.Where(row => row.Key.StartsWith("Constituent.", StringComparison.Ordinal)).ToArray();
        Check(constituents.Length == reading.Constituents.Count, "Species do not have separate layout rows.");
        foreach (var row in reading.Constituents)
        {
            var detail = constituents.Single(value => value.Key == "Constituent." + row.MaterialId);
            Check(detail.Text.Contains(row.MassTextKg) && detail.Text.Contains(row.MolesText) && detail.Text.Count(character => character == '\n') == 2,
                "Constituent table row lost measurements or its bounded three-line layout.");
            if (row.GasMoleFraction.HasValue) Check(detail.Text.Contains(row.MolePercentText) && detail.Text.Contains(row.PartialPressureText), "Gas row omitted fraction/partial pressure.");
        }
        var text = MixtureInspection.FormatFullComposition(reading);
        Check(text.IndexOf("Breathing warnings", StringComparison.Ordinal) < text.IndexOf(MixtureInspection.FormatConstituent(reading.Constituents[0]), StringComparison.Ordinal),
            "Diagnostic full text still puts critical warnings below the species.");
    }

    private static void HeaderAndAssessment()
    {
        var (catalog, model) = Model();
        var reading = model.Describe(catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = .1, ["nitrogen"] = .9 }, 300), 1);
        Check(!MixtureInspection.FormatHeader(reading).Contains('\n') && MixtureInspection.FormatHeader(reading) == reading.DisplayName.Split('\n')[0],
            "Selected header includes the shared assessment's second line.");
        Check(MixtureInspection.FormatState(reading).Contains("Unsafe atmosphere"), "Single-line header caused the actual assessment to disappear.");
    }

    private static void NativePortGuards()
    {
        var reads = 0; ConduitFlow.ConduitContents Read() { ++reads; return default; }
        foreach (var condition in new[] { (false, 0, 100, 100), (true, -1, 100, 100), (true, 45, 100, 40),
                     (true, 25, 20, 40), (true, int.MaxValue, 100, 100), (true, 0, 100, 0) })
        {
            Check(!MixtureInspection.TryReadNativePort(condition.Item1, condition.Item2, condition.Item3, condition.Item4, Read, out _, out var reason) && reason.Length > 0,
                "An invalid native port was accepted.");
        }
        Check(reads == 0, "Invalid native ports still invoked the unsafe packet reader.");
        Check(!MixtureInspection.TryReadNativePort(true, 0, 100, 100, null, out _, out var missing) && missing.Contains("not initialized"), "Missing manager was not identified.");
        Check(MixtureInspection.TryReadNativePort(true, 39, 100, 40, Read, out var empty, out var available) && empty.mass == 0 && available.Length == 0 && reads == 1,
            "A valid empty native packet was treated as an error.");
    }

    private static void NativePortReadFailures()
    {
        Check(!MixtureInspection.TryReadNativePort(true, 0, 100, 100, () => throw new IndexOutOfRangeException(), out _, out var changed) && changed.Contains("changed"),
            "A native network bounds race escaped into the inspection UI.");
        var propagated = false;
        try { MixtureInspection.TryReadNativePort(true, 0, 100, 100, () => throw new InvalidOperationException("unexpected contract failure"), out _, out _); }
        catch (InvalidOperationException) { propagated = true; }
        Check(propagated, "An unexpected native contract failure was silently swallowed.");
    }

    private static void DetailLayoutContract()
    {
        var text = typeof(DetailLabel).GetField("label")!.FieldType;
        Check(text.GetProperty("maxVisibleLines")?.CanWrite == true && text.GetProperty("overflowMode")?.CanWrite == true,
            "Actual detail label layout cannot lift description truncation.");
    }

    private static void DetailSiblingOrderingContract()
    {
        // Native SetLabel only instantiates missing IDs. A safe reading creates
        // density/composition first; a later unsafe reading's new warning IDs
        // otherwise remain below those cached rows regardless of SetLabel order.
        var native = PatchProcessor.GetOriginalInstructions(AccessTools.Method(typeof(CollapsibleDetailContentPanel), nameof(CollapsibleDetailContentPanel.SetLabel)));
        var sibling = AccessTools.Method(typeof(UnityEngine.Transform), nameof(UnityEngine.Transform.SetAsLastSibling));
        Check(!native.Any(item => item.Calls(sibling)), "Native SetLabel changed its hierarchy-order contract; review the mixture ordering adapter.");
        var refresh = PatchProcessor.GetOriginalInstructions(AccessTools.Method(typeof(MixtureInspection), "AddReadingDetails"));
        var setLabel = refresh.FindIndex(item => item.Calls(AccessTools.Method(typeof(CollapsibleDetailContentPanel), nameof(CollapsibleDetailContentPanel.SetLabel))));
        var moves = refresh.Select((item, index) => (item, index)).Where(value => value.item.Calls(sibling)).Select(value => value.index).ToArray();
        Check(setLabel >= 0 && moves.Length == 2 && moves.All(index => index > setLabel),
            "Mixture refresh no longer moves the inventory title and current detail rows after populating cached labels.");
        var prioritize = typeof(MixtureInspection).GetMethod(nameof(MixtureInspection.PrioritizeOwnedDetailRows), BindingFlags.Static | BindingFlags.NonPublic)!;
        var append = PatchProcessor.GetOriginalInstructions(AccessTools.Method(typeof(MixtureInspection), nameof(MixtureInspection.AddDetails)));
        var priorityCall = append.FindIndex(item => item.operand is MethodInfo method && method.IsGenericMethod && method.GetGenericMethodDefinition() == prioritize);
        var nativeLabels = append.Select((item, index) => (item, index)).Where(value => value.item.Calls(
            AccessTools.Method(typeof(CollapsibleDetailContentPanel), nameof(CollapsibleDetailContentPanel.SetLabel)))).Select(value => value.index).ToArray();
        Check(priorityCall >= 0 && nativeLabels.All(index => index < priorityCall), "Container priority does not run after all current labels are populated.");
        var moveFirst = PatchProcessor.GetOriginalInstructions(AccessTools.Method(typeof(MixtureInspection), "MoveDetailToFirst"));
        Check(moveFirst.Any(item => item.Calls(AccessTools.Method(typeof(UnityEngine.Transform), nameof(UnityEngine.Transform.SetAsFirstSibling)))),
            "Owned-container priority no longer moves actual native cached labels to the front.");
    }

    private sealed class CachedDetail
    {
        internal readonly string Key, Text, Tooltip;
        internal CachedDetail(string key, string text = "", string tooltip = "") { Key = key; Text = text; Tooltip = tooltip; }
    }

    private static void OwnedContainerDetailPriority()
    {
        var (catalog, model) = Model();
        var nativeDescription = new CachedDetail("Description", "Native <link=GASPIPING>pipe</link> description.\nPreserve this entire text.", "Native tooltip");
        var nativeContents = new CachedDetail("Contents", "Native packet contents and game status");
        var nativeExtra = new CachedDetail("Effects", "Native effects");
        var nativeRows = new[] { nativeDescription, nativeContents, nativeExtra };
        var cache = nativeRows.ToList();
        var ownedKeys = new List<string>(); var observedMoves = new List<string>();
        var moles = 101325 / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300);
        var safe = model.Describe(catalog.CreateInventory(new Dictionary<string, double>
            { ["oxygen"] = .21 * moles * .032, ["nitrogen"] = .79 * moles * .028 }, 300), 1);
        var unsafeReading = model.Describe(catalog.CreateInventory(new Dictionary<string, double>
            { ["oxygen"] = .21 * moles * .032, ["nitrogen"] = .79 * moles * .028, ["chlorine"] = .0001 }, 300), 1);
        void Populate(string prefix, MixturePresentation reading)
        {
            ownedKeys.Add(prefix);
            foreach (var row in MixtureInspection.BuildDetailRows(reading)) ownedKeys.Add(prefix + "." + row.Key);
            foreach (var key in ownedKeys)
                if (!cache.Any(row => row.Key == key)) cache.Add(new CachedDetail(key));
        }
        void Reorder() => MixtureInspection.PrioritizeOwnedDetailRows(cache, ownedKeys, row => row.Key, row =>
        { observedMoves.Add(row.Key); cache.Remove(row); cache.Insert(0, row); });
        void CheckNativePreserved()
        {
            Check(cache.Where(row => nativeRows.Contains(row)).SequenceEqual(nativeRows), "Priority changed native objects or their relative hierarchy order.");
            Check(nativeDescription.Text == "Native <link=GASPIPING>pipe</link> description.\nPreserve this entire text." &&
                nativeDescription.Tooltip == "Native tooltip" && nativeContents.Text == "Native packet contents and game status",
                "Mixture ordering overwrote native description, tooltip or packet contents.");
        }
        Populate("MixedAtmospheres.Inventory.0", safe); Reorder(); CheckNativePreserved();
        ownedKeys.Clear(); Populate("MixedAtmospheres.Inventory.0", unsafeReading);
        // These warning labels were created after cached native and constituent labels.
        Check(cache.FindIndex(row => row.Key.Contains(".Warning.", StringComparison.Ordinal)) > cache.IndexOf(nativeDescription),
            "The fixture did not reproduce newly created warnings below native description.");
        Populate("MixedAtmospheres.Inventory.1", unsafeReading); Reorder(); CheckNativePreserved();
        Check(cache.Take(ownedKeys.Count).Select(row => row.Key).SequenceEqual(ownedKeys), "Current owned inventory blocks were not moved first in their declared display order.");
        var descriptionIndex = cache.IndexOf(nativeDescription);
        foreach (var prefix in new[] { "MixedAtmospheres.Inventory.0", "MixedAtmospheres.Inventory.1" })
        {
            Check(cache.FindIndex(row => row.Key == prefix + ".State") < descriptionIndex, "An owned assessment remains below native description.");
            Check(cache.Where(row => row.Key.StartsWith(prefix + ".Warning.", StringComparison.Ordinal)).All(row => cache.IndexOf(row) < descriptionIndex),
                "A newly created breathing warning remains below native description.");
        }
        var count = cache.Count; var order = cache.ToArray(); Reorder();
        Check(cache.Count == count && cache.SequenceEqual(order), "Repeated refresh duplicated or shuffled cached detail labels.");
        Check(observedMoves.Count > 0, "Production priority path was not exercised.");
    }

    private static void OwnedUnavailableAndNativeDetailPriority()
    {
        var native = new[] { new CachedDetail("Description", "Keep native description"), new CachedDetail("Contents", "Native contents") };
        var oldOwned = new CachedDetail("MixedAtmospheres.Inventory.0.Warning.0", "Inactive prior selection warning");
        var unavailable = new CachedDetail("MixedAtmospheres.InventoryUnavailable", "Owned inventory unavailable: provider failed");
        var cache = new List<CachedDetail> { native[0], oldOwned, native[1], unavailable };
        var moved = new List<string>();
        void Move(CachedDetail row) { moved.Add(row.Key); cache.Remove(row); cache.Insert(0, row); }
        MixtureInspection.PrioritizeOwnedDetailRows(cache, new[] { unavailable.Key }, row => row.Key, Move);
        Check(ReferenceEquals(cache[0], unavailable) && moved.SequenceEqual(new[] { unavailable.Key }), "Explicit adapter failure did not precede description or moved stale owned rows.");
        Check(cache.Where(row => native.Contains(row)).SequenceEqual(native), "Adapter failure changed native label order.");
        var beforeNativeFallback = cache.ToArray(); moved.Clear();
        MixtureInspection.PrioritizeOwnedDetailRows(cache, Array.Empty<string>(), row => row.Key, Move);
        Check(moved.Count == 0 && cache.SequenceEqual(beforeNativeFallback), "Unowned fallback reordered native or stale cached labels.");
        Check(native[0].Text == "Keep native description" && native[1].Text == "Native contents", "Native fallback contents changed.");
    }

    private static void PipeSurface()
    {
        Check(typeof(Conduit).GetMethod(nameof(Conduit.GetFlowManager), Type.EmptyTypes)?.ReturnType == typeof(ConduitFlow), "Pipe flow accessor changed.");
        Check(typeof(Conduit).GetProperty(nameof(Conduit.Cell))?.PropertyType == typeof(int), "Pipe cell accessor changed.");
        var packet = typeof(ConduitFlow.ConduitContents);
        Check(packet.GetProperty("mass")?.PropertyType == typeof(float) && packet.GetField("temperature")?.FieldType == typeof(float) &&
            packet.GetField("element")?.FieldType == typeof(SimHashes), "Pipe packet fields changed.");
    }
    private static void StorageSurfaces()
    {
        Check(typeof(IStorage).GetMethod(nameof(IStorage.GetItems)) != null && typeof(IStorage).IsAssignableFrom(typeof(Storage)), "Storage inspection contract changed.");
        var resolve = typeof(MixtureInspection).GetMethod("ResolveContainers", BindingFlags.Static | BindingFlags.NonPublic)!;
        var code = PatchProcessor.GetOriginalInstructions(resolve);
        Check(code.Any(item => item.operand is MethodInfo method && method.Name == nameof(IStorage.GetItems) && method.DeclaringType == typeof(IStorage)),
            "Reservoir/machine inspection no longer enumerates actual stored packets.");
    }
    private static void BottleSurface()
    {
        foreach (var property in new[] { nameof(PrimaryElement.Mass), nameof(PrimaryElement.Temperature) })
            Check(typeof(PrimaryElement).GetProperty(property) != null, "Bottle inventory accessor changed: " + property);
        Check(typeof(PrimaryElement).GetField(nameof(PrimaryElement.ElementID))?.FieldType == typeof(SimHashes), "Bottle element identity accessor changed.");
    }
    private static void SuitSurface()
    {
        Check(typeof(SuitTank).GetField(nameof(SuitTank.storage))?.FieldType == typeof(Storage), "Suit tank storage contract changed.");
        Check(typeof(MinionIdentity).GetMethod(nameof(MinionIdentity.GetEquipment))?.ReturnType == typeof(Equipment), "Equipped suit lookup changed.");
        Check(typeof(Assignables).GetProperty(nameof(Assignables.Slots)) != null && typeof(Equippable).GetField(nameof(Equippable.isEquipped)) != null,
            "Equipped slot inspection contract changed.");
    }
    private static void VentSurface()
    {
        Check(typeof(ConduitDispenser).GetProperty(nameof(ConduitDispenser.ConduitContents))?.PropertyType == typeof(ConduitFlow.ConduitContents),
            "Vent output packet accessor changed.");
        Check(AccessTools.Field(typeof(ConduitConsumer), "utilityCell")?.FieldType == typeof(int) && typeof(Vent).GetField(nameof(Vent.conduitType)) != null,
            "Actual gas/liquid vent input packet contract changed.");
    }
    private static void ProjectionOnly()
    {
        foreach (var method in typeof(MixtureInspection).GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic))
        {
            if (method.GetMethodBody() == null) continue;
            // Harmony's dynamic IL reader cannot import an open generic
            // parameter. Closing it preserves the method body and every call
            // site, so generic UI helpers receive the same projection scan.
            var inspected = method.IsGenericMethodDefinition
                ? method.MakeGenericMethod(Enumerable.Repeat(typeof(object), method.GetGenericArguments().Length).ToArray())
                : method;
            foreach (var item in PatchProcessor.GetOriginalInstructions(inspected))
                if (item.operand is MethodBase called)
                    Check(called.DeclaringType != typeof(FluidInventory) && called.DeclaringType != typeof(MixturePresentationModel) && called.DeclaringType != typeof(FluidMaterialCatalog),
                        "UI layer started creating/inferencing authoritative inventory or chemistry: " + called.Name);
        }
    }
    private static void UnregisterSurface()
    {
        var method = typeof(MixtureInspection).GetMethod(nameof(MixtureInspection.UnregisterInventory))!;
        var calls = PatchProcessor.GetOriginalInstructions(method).Where(item => item.operand is MethodInfo).Select(item => (MethodInfo)item.operand).ToArray();
        Check(calls.Any(item => item.Name == nameof(MixtureInspection.UnregisterInventoryOwner)) &&
            calls.Any(item => item.Name == "UnregisterInventory" && item.DeclaringType?.Name == "MixturePresentationRuntime"),
            "Unregister left a runtime mixture provider attached.");
    }
    private static void AdapterFailureSurface()
    {
        var method = typeof(MixtureInspection).GetMethod("ResolveContainers", BindingFlags.Static | BindingFlags.NonPublic)!;
        Check(PatchProcessor.GetOriginalInstructions(method).Any(item => item.operand is MethodInfo called && called.Name == "TryGetInventoryError"),
            "Registered adapter errors silently fall back to a projected native element.");
    }

    private static void OwnedAllocationResolution()
    {
        var (catalog, model) = Model();
        MixturePresentationRuntime.ResetProviders(); MixturePresentationRuntime.Configure(model);
        var state = TestState(catalog); FluidContainerHandle first, replacement;
        using (var tx = state.BeginTransaction())
        {
            tx.Inject(0, catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = .2, ["nitrogen"] = .8, ["chlorine"] = 1e-5 }, 300), "test");
            first = tx.CreateContainer("owned", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas));
            tx.TransferAllCellToContainer(0, first); tx.Commit();
        }
        MixturePresentationRuntime.Publish(state.CaptureSnapshot());
        var old = MixturePresentationRuntime.AcquireFrameFor(100)!;
        Check(MixtureInspection.TryResolveContainerBinding(old, first, out var reading, out var error) &&
            reading!.Constituents.Any(row => row.MaterialId == "chlorine" && row.MassKg > 0) && reading.Warnings.Count > 0 && error.Length == 0,
            "Exact allocation did not resolve its trace or warning.");
        using (var tx = state.BeginTransaction())
        {
            tx.RetireContainerToCell(first, 0);
            replacement = tx.CreateContainer(first.Id, new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas));
            tx.TransferAllCellToContainer(0, replacement); tx.Commit();
        }
        MixturePresentationRuntime.Publish(state.CaptureSnapshot());
        var next = MixturePresentationRuntime.AcquireFrameFor(101)!;
        Check(!MixtureInspection.TryResolveContainerBinding(next, first, out var missing, out error) && missing == null && error.Contains("retired, replaced"),
            "Retired allocation silently resolved the replacement.");
        Check(MixtureInspection.TryResolveContainerBinding(next, replacement, out _, out error) && error.Length == 0 &&
            MixtureInspection.TryResolveContainerBinding(old, first, out var prior, out _) && ReferenceEquals(prior, reading),
            "New allocation or retained immutable frame failed.");
        Check(!MixtureInspection.TryResolveContainerBinding(null, first, out _, out error) && error.Contains("completed"), "Missing frame was not explicit.");
        MixturePresentationLifecycle.EndSession();
    }

    private static void OwnedBindingSurface()
    {
        var registration = typeof(MixtureInspection).GetMethod(nameof(MixtureInspection.RegisterContainerOwner))!;
        var called = PatchProcessor.GetOriginalInstructions(registration).Where(item => item.operand is MethodBase).Select(item => (MethodBase)item.operand).ToArray();
        Check(called.All(item => item.DeclaringType != typeof(MixturePresentationRuntime)), "Owned object binding registered a second provider.");
        var resolver = typeof(MixtureInspection).GetMethod("ResolveContainers", BindingFlags.Static | BindingFlags.NonPublic)!;
        var instructions = PatchProcessor.GetOriginalInstructions(resolver);
        Check(instructions.Any(item => item.operand is MethodInfo method && method.Name == "TryResolveContainerBinding"),
            "Object inspection bypassed exact-handle resolution.");
        Check(instructions.Any(item => item.operand is MethodInfo method && method.Name == "TryResolveIndependentInventory") &&
            instructions.All(item => item.operand is not MethodInfo method || method.Name != "TryResolveInventory"),
            "An ID-only independent object binding can adopt an owned container.");
        var rejected = false;
        try { MixtureInspection.RegisterContainerOwner(null!, default); }
        catch (ArgumentException) { rejected = true; }
        Check(rejected, "Invalid allocation reached the Unity object API.");
    }

    private static ColonyFluidState TestState(FluidMaterialCatalog catalog)
    {
        using var memory = new MemoryStream(); using var writer = new BinaryWriter(memory);
        const int count = 9;
        writer.Write(29 + 36 * count); writer.Write(System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0")); writer.Write(15);
        writer.Write(3); writer.Write(3); writer.Write(0); writer.Write(0); writer.Write((byte)0);
        for (var i = 0; i < count; ++i) { writer.Write(1); writer.Write(0f); writer.Write(0f); writer.Write(0f); }
        for (var i = 0; i < count; ++i) { writer.Write(0); writer.Write(0); }
        for (var i = 0; i < count; ++i) { writer.Write(1); writer.Write(0f); writer.Write(0f); }
        var imported = NativeColonyImport.StageNewColony(1, 1, new[] { new NativeWorldPlacement(0, 1, 1, 0, 0) },
            new[] { new NativeWorldSource(0, memory.ToArray()) }, new Dictionary<int, NativeMatterPhase> { [1] = NativeMatterPhase.Vacuum },
            new Dictionary<int, NativeFluidBinding>(), 1);
        return ColonyFluidState.Import(imported, catalog);
    }

    private static void SessionBindingReset()
    {
        // A live Asset prefab can retain its managed key between games. Use an
        // identity-only key here; no Unity native object or constructor runs.
        var prefab = RuntimeHelpers.GetUninitializedObject(typeof(UnityEngine.GameObject));
        foreach (var name in new[] { "Owners", "Cache" })
        {
            var field = typeof(MixtureInspection).GetField(name, BindingFlags.Static | BindingFlags.NonPublic)!;
            var table = field.GetValue(null)!;
            var value = Activator.CreateInstance(table.GetType().GenericTypeArguments[1], true)!;
            table.GetType().GetMethod("Add")!.Invoke(table, new[] { prefab, value });
        }
        typeof(MixtureInspection).GetField("sessionBindingCount", BindingFlags.Static | BindingFlags.NonPublic)!.SetValue(null, 2);
        MixtureInspection.CellHoverObserver = (_, _, _, _) => { };
        MixturePresentationLifecycle.EndSession();
        Check(MixtureInspection.SessionBindingCount == 0, "Previous colony retained its binding count.");
        Check(MixtureInspection.CellHoverObserver == null, "Previous colony retained its private hover observer.");
        foreach (var name in new[] { "Owners", "Cache" })
        {
            var table = typeof(MixtureInspection).GetField(name, BindingFlags.Static | BindingFlags.NonPublic)!.GetValue(null)!;
            var arguments = new object?[] { prefab, null };
            Check(!(bool)table.GetType().GetMethod("TryGetValue")!.Invoke(table, arguments)!, "A previous colony retained prefab " + name);
        }
    }
}
