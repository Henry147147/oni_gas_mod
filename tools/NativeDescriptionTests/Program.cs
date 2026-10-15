using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text.Json;
using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length < 1 || args.Length > 2) throw new ArgumentException("Pass ONI installation and optional actual-game mixture-visual-validation.json.");
        var managed = Path.Combine(Path.GetFullPath(args[0]), "OxygenNotIncluded_Data", "Managed");
        AssemblyLoadContext.Default.Resolving += (_, name) =>
        {
            var path = Path.Combine(managed, name.Name + ".dll");
            return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
        };
        return Run(args.Length == 2 ? args[1] : null);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Run(string? evidence)
    {
        var tests = new (string name, System.Action test)[]
        {
            ("native plain text remains exact", () => Check(MixtureContainerVisualValidation.NativeLabelText("A fixture and its contents.") == "A fixture and its contents.", "Plain text changed.")),
            ("native link decoration is expected without stripping targets", DecoratedLinks),
            ("preservation still rejects changed hidden disabled and zero-height descriptions", StrictPreservation),
            ("native decorated text remains idempotent", Idempotent),
            ("spawned fixtures reject missing presentation opt-in before Unity access", DisabledGuard)
        };
        var failed = 0;
        foreach (var (name, test) in tests)
        {
            try { test(); Console.WriteLine("PASS " + name); }
            catch (Exception error) { ++failed; Console.WriteLine("FAIL " + name + ": " + error); }
        }
        if (evidence != null)
        {
            try { ActualPipe(evidence); Console.WriteLine("PASS recorded actual-game pipe Description matches native construction and display pipeline"); }
            catch (Exception error) { ++failed; Console.WriteLine("FAIL actual-game pipe Description: " + error); }
        }
        return failed == 0 ? 0 : 1;
    }

    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private const string Raw = "Carries <link=\"TEST-GAS\">test gas</link>.";
    private static MixtureContainerVisualValidation.LabelEvidence Visible(string text) => new()
    { Name = "Description", Text = text, ActiveInHierarchy = true, TextEnabled = true, Height = 20 };

    private static void DecoratedLinks()
    {
        var expected = "Carries <b><style=\"KLink\"><link=\"TEST-GAS\">test gas</style></b></link>.";
        Check(MixtureContainerVisualValidation.NativeLabelText(Raw) == expected, "Native rich-text processing differs from the known installed contract.");
        Check(MixtureContainerVisualValidation.DescriptionIsPreserved(Raw, Visible(expected)), "Correct native-rendered link text was rejected.");
    }

    private static void StrictPreservation()
    {
        var correct = MixtureContainerVisualValidation.NativeLabelText(Raw);
        Check(!MixtureContainerVisualValidation.DescriptionIsPreserved(Raw, Visible(Raw)), "Raw text was accepted despite the active native link formatting contract.");
        Check(!MixtureContainerVisualValidation.DescriptionIsPreserved(Raw, Visible(correct.Replace("TEST-GAS", "WRONG-TARGET"))), "A modified native link target passed preservation.");
        Check(!MixtureContainerVisualValidation.DescriptionIsPreserved(Raw, Visible(correct + " changed")), "Changed description content passed preservation.");
        var hidden = Visible(correct); hidden.ActiveInHierarchy = false;
        var disabled = Visible(correct); disabled.TextEnabled = false;
        var collapsed = Visible(correct); collapsed.Height = 0;
        Check(!MixtureContainerVisualValidation.DescriptionIsPreserved(Raw, hidden) &&
            !MixtureContainerVisualValidation.DescriptionIsPreserved(Raw, disabled) &&
            !MixtureContainerVisualValidation.DescriptionIsPreserved(Raw, collapsed), "A hidden or collapsed native description passed.");
        Check(MixtureContainerVisualValidation.DescriptionIsPreserved("", null) &&
            !MixtureContainerVisualValidation.DescriptionIsPreserved("", Visible("unexpected")), "The native no-description branch was waived.");
    }

    private static void Idempotent()
    {
        var rendered = MixtureContainerVisualValidation.NativeLabelText(Raw);
        Check(MixtureContainerVisualValidation.NativeLabelText(rendered) == rendered, "Already decorated native links were decorated again.");
    }

    private static void DisabledGuard()
    {
        try { MixtureContainerVisualValidation.Create(new Dictionary<string, int>(), FluidInventory.Empty, 1); }
        catch (InvalidOperationException error) when (error.Message.Contains("explicit private presentation mode")) { return; }
        throw new Exception("Private fixture creation did not reject an absent opt-in.");
    }

    private static void ActualPipe(string path)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var pipe = document.RootElement.GetProperty("SpawnedContainerEvidence").EnumerateArray().Single(item => item.GetProperty("Surface").GetString() == "pipe");
        var description = pipe.GetProperty("Labels").EnumerateArray().Single(item => item.GetProperty("Name").GetString() == "Description" && item.GetProperty("ActiveInHierarchy").GetBoolean());
        var raw = (string)STRINGS.BUILDINGS.PREFABS.GASCONDUIT.EFFECT + "\n\n" + (string)STRINGS.BUILDINGS.PREFABS.GASCONDUIT.DESC;
        var actual = Visible(description.GetProperty("Text").GetString()!);
        actual.Height = description.GetProperty("Height").GetSingle();
        actual.TextEnabled = description.GetProperty("TextEnabled").GetBoolean();
        Check(MixtureContainerVisualValidation.DescriptionIsPreserved(raw, actual), "Recorded native Description differs from its actual installed native construction/display rules.");
    }
}
