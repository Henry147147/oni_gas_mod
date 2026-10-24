using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using HarmonyLib;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal readonly struct MixtureInspectionDetailRow
    {
        internal readonly string Key, Text, Tooltip;
        internal MixtureInspectionDetailRow(string key, string text, string tooltip = "") { Key = key; Text = text; Tooltip = tooltip; }
    }
    internal sealed class MixturePipeReading
    {
        internal readonly string Title;
        internal readonly IReadOnlyList<string> Hover,Details;
        internal MixturePipeReading(string title,IReadOnlyList<string> hover,IReadOnlyList<string> details){Title=title;Hover=hover;Details=details;}
    }

    /// <summary>Read-only cell and container surfaces over the shared completed-frame projection.</summary>
    public static class MixtureInspection
    {
        private sealed class Binding
        {
            internal readonly string Id, Label;
            internal readonly bool SuppressNative;
            internal readonly FluidContainerHandle? Handle;
            internal Binding(string id, string label, bool suppressNative) { Id = id; Label = label; SuppressNative = suppressNative; }
            internal Binding(FluidContainerHandle handle, string label) { Id = handle.Id; Handle = handle; Label = label; SuppressNative = true; }
        }
        private sealed class OwnerBindings { internal readonly List<Binding> Items = new List<Binding>(); }
        private sealed class OwnerCache { internal int Frame = -1, Generation; internal InspectionData? Data; }
        private sealed class InspectionData
        {
            internal readonly List<(string label, MixturePresentation reading)> Owned = new List<(string, MixturePresentation)>();
            internal readonly List<MixturePipeReading> Pipes=new List<MixturePipeReading>();
            internal readonly List<string> Native = new List<string>();
            internal readonly List<string> NativeUnavailable = new List<string>();
            internal readonly List<string> Errors = new List<string>();
        }
        private static ConditionalWeakTable<GameObject, OwnerBindings> Owners = new ConditionalWeakTable<GameObject, OwnerBindings>();
        private static ConditionalWeakTable<GameObject, OwnerCache> Cache = new ConditionalWeakTable<GameObject, OwnerCache>();
        private static readonly FieldInfo ConsumerCell = AccessTools.Field(typeof(ConduitConsumer), "utilityCell");
        private static readonly FieldInfo DispenserCell = AccessTools.Field(typeof(ConduitDispenser), "utilityCell");
        private static readonly FieldInfo FlowGrid = AccessTools.Field(typeof(ConduitFlow), "grid");
        private static readonly FieldInfo FilterPort=AccessTools.Field(typeof(ElementFilter),"portInfo"),
            FilterInput=AccessTools.Field(typeof(ElementFilter),"inputCell"),FilterOutput=AccessTools.Field(typeof(ElementFilter),"outputCell"),
            FilterSelected=AccessTools.Field(typeof(ElementFilter),"filteredCell");
        private static readonly FieldInfo FilterType=AccessTools.Field(FilterPort.FieldType,"conduitType");
        private static int generation, sessionBindingCount;
        private static Func<ConduitFlow,int,string,MixturePipeReading?>? pipeReader;
        private static Func<Storage,MixturePipeReading?>? conduitStorageReader;
        private static Func<int,MixturePipeReading?>? cellSpillReader;
        internal static void SetCellSpillReader(Func<int,MixturePipeReading?>? reader){cellSpillReader=reader;++generation;}
        internal static MixturePipeReading? ReadCellSpill(int cell)=>cellSpillReader?.Invoke(cell);
        internal static void SetConduitStorageReader(Func<Storage,MixturePipeReading?>? reader){conduitStorageReader=reader;++generation;}
        internal static void SetPipeReader(Func<ConduitFlow,int,string,MixturePipeReading?>? reader){pipeReader=reader;++generation;}
        private static long hoverDraws, detailsDraws, ownedContainerDraws, nativeContainerDraws;
        // Private in-game evidence only. Ordinary hover has no observer and
        // performs no diagnostic capture or additional list allocation.
        internal static System.Action<int, MixturePresentation, IReadOnlyList<string>, bool>? CellHoverObserver;
        public static bool CellHoverHookReady { get; internal set; }
        public static bool DetailsHookReady { get; internal set; }
        public static long HoverDrawCount => Interlocked.Read(ref hoverDraws);
        public static long DetailDrawCount => Interlocked.Read(ref detailsDraws);
        public static long OwnedContainerDrawCount => Interlocked.Read(ref ownedContainerDraws);
        public static long NativeContainerDrawCount => Interlocked.Read(ref nativeContainerDraws);
        // Diagnostic upper bound until session reset: weak owners can be
        // collected without unregistering. Zero proves the session tables are empty.
        internal static int SessionBindingCount => sessionBindingCount;

        static MixtureInspection() { MixturePresentationRuntime.SessionEnding += ResetBindings; }

        // Asset prefabs may survive a scene change, so weak keys alone do not
        // retire their old colony's bindings or cached immutable readings.
        internal static void ResetBindings()
        {
            Owners = new ConditionalWeakTable<GameObject, OwnerBindings>();
            Cache = new ConditionalWeakTable<GameObject, OwnerCache>();
            sessionBindingCount = 0;
            CellHoverObserver = null;
            ++generation;
            Interlocked.Exchange(ref hoverDraws, 0); Interlocked.Exchange(ref detailsDraws, 0);
            Interlocked.Exchange(ref ownedContainerDraws, 0); Interlocked.Exchange(ref nativeContainerDraws, 0);
        }

        // Registration is explicit: a storage/building's projected dominant
        // element is never evidence of an owned mixed inventory.
        public static void RegisterInventoryOwner(GameObject owner, string inventoryId, string label = "Fluid contents", bool suppressNativeFallback = true)
        {
            if (owner == null) throw new ArgumentNullException(nameof(owner));
            if (string.IsNullOrWhiteSpace(inventoryId)) throw new ArgumentException("An inventory identity is required.", nameof(inventoryId));
            RequireIndependentOwnerBinding(owner, inventoryId);
            var bindings = Owners.GetValue(owner, _ => new OwnerBindings()).Items;
            sessionBindingCount -= bindings.RemoveAll(item => !item.Handle.HasValue && item.Id == inventoryId);
            bindings.Add(new Binding(inventoryId, string.IsNullOrWhiteSpace(label) ? "Fluid contents" : label, suppressNativeFallback));
            ++sessionBindingCount;
            generation++;
        }

        public static void RegisterInventory(GameObject owner, string inventoryId, Func<FluidInventory> inventory,
            double volumeCubicMetres, string label = "Fluid contents")
        {
            if (owner == null) throw new ArgumentNullException(nameof(owner));
            RequireIndependentOwnerBinding(owner, inventoryId);
            MixturePresentationRuntime.RegisterInventory(inventoryId, inventory, volumeCubicMetres, () => owner != null);
            RegisterInventoryOwner(owner, inventoryId, label);
        }

        /// <summary>Binds a game object to an exact owned allocation in the shared completed snapshot. This creates no inventory provider.</summary>
        public static void RegisterContainerOwner(GameObject owner, FluidContainerHandle handle, string label = "Fluid contents")
        {
            if (handle.ColonyId == Guid.Empty || string.IsNullOrWhiteSpace(handle.Id) || handle.Generation <= 0 || handle.AllocationToken == Guid.Empty)
                throw new ArgumentException("A complete owned container allocation is required.", nameof(handle));
            if (owner == null) throw new ArgumentNullException(nameof(owner));
            var bindings = Owners.GetValue(owner, _ => new OwnerBindings()).Items;
            if (bindings.Any(item => !item.Handle.HasValue && item.Id == handle.Id))
                throw new InvalidOperationException("Unregister the independent inventory binding before assigning an owned allocation.");
            sessionBindingCount -= bindings.RemoveAll(item => item.Id == handle.Id);
            bindings.Add(new Binding(handle, string.IsNullOrWhiteSpace(label) ? "Fluid contents" : label));
            ++sessionBindingCount; ++generation;
        }

        public static void UnregisterContainerOwner(GameObject owner, FluidContainerHandle handle)
        {
            if (ReferenceEquals(owner, null)) return;
            if (Owners.TryGetValue(owner, out var bindings))
                sessionBindingCount -= bindings.Items.RemoveAll(item => item.Handle.HasValue && item.Handle.Value.Equals(handle));
            ++generation;
        }

        internal static bool TryResolveContainerBinding(RuntimeMixturePresentationFrame? frame, FluidContainerHandle handle,
            out MixturePresentation? reading, out string error)
        {
            reading = null;
            if (frame == null) { error = "waiting for a completed inventory frame"; return false; }
            if (!frame.TryResolveInventory(handle, out reading) || reading == null)
            { error = "owned container allocation is retired, replaced, foreign or unpublished"; return false; }
            error = ""; return true;
        }

        public static void UnregisterInventoryOwner(GameObject owner, string inventoryId)
        {
            if (ReferenceEquals(owner, null)) return;
            if (Owners.TryGetValue(owner, out var bindings)) sessionBindingCount -= bindings.Items.RemoveAll(item => !item.Handle.HasValue && item.Id == inventoryId);
            generation++;
        }

        private static void RequireIndependentOwnerBinding(GameObject owner, string inventoryId)
        {
            if (Owners.TryGetValue(owner, out var bindings) && bindings.Items.Any(item => item.Handle.HasValue && item.Id == inventoryId))
                throw new InvalidOperationException("An independent binding cannot replace an exact owned allocation.");
        }

        public static void UnregisterInventory(GameObject owner, string inventoryId)
        {
            UnregisterInventoryOwner(owner, inventoryId);
            MixturePresentationRuntime.UnregisterInventory(inventoryId);
        }

        // Unity fixture diagnostics use the exact same resolver and immutable
        // readings as hover/details. This does not select, spawn or mutate an
        // object, create inventory, or change any game simulation state.
        public static string GetContainerInspectionText(GameObject target)
        {
            if (target == null) throw new ArgumentNullException(nameof(target));
            var data = ResolveContainers(target);
            var text = new StringBuilder();
            foreach (var owned in data.Owned) text.Append(owned.label).Append('\n').Append(FormatFullComposition(owned.reading)).Append('\n');
            foreach(var pipe in data.Pipes)text.Append(pipe.Title).Append('\n').Append(string.Join("\n",pipe.Details)).Append('\n');
            if (data.Native.Count > 0) text.Append("Native inventory · one material per packet\n").Append(string.Join("\n", data.Native)).Append('\n');
            foreach (var unavailable in data.NativeUnavailable) text.Append(unavailable).Append('\n');
            foreach (var error in data.Errors) text.Append(error).Append('\n');
            return text.ToString();
        }

        public static string FormatState(MixturePresentation reading)
        {
            if (reading == null) throw new ArgumentNullException(nameof(reading));
            var temperature = reading.TotalMassKg > 0
                ? Number(reading.TemperatureKelvin - 273.15) + " °C (" + Number(reading.TemperatureKelvin) + " K)"
                : "temperature unavailable in vacuum";
            var kilopascals = reading.TotalPressurePa / 1000;
            var pressure = reading.GasMoles > 0
                ? "Gas pressure " + (kilopascals == 0 && reading.TotalPressurePa > 0 ? "below numeric range" : Number(kilopascals)) +
                    " kPa (" + Number(reading.TotalPressurePa) + " Pa)"
                : "No gas phase";
            var state = pressure + " · " + temperature + " · " + Number(reading.TotalMassKg) + " kg";
            if (reading.LiquidVolumeFraction > 0) state += " · " + FormatFluidPressure(reading);
            return state + " · " + reading.BreathingStatus;
        }

        public static string FormatHeader(MixturePresentation reading)
        {
            if (reading == null) throw new ArgumentNullException(nameof(reading));
            var newline = reading.DisplayName.IndexOf('\n');
            return (newline < 0 ? reading.DisplayName : reading.DisplayName.Substring(0, newline)).TrimEnd('\r');
        }

        private static string FormatFluidPressure(MixturePresentation reading)
        {
            if (!reading.MechanicalPressurePa.HasValue) return "Fluid pressure unavailable";
            var pressure = reading.MechanicalPressurePa.Value;
            return "Fluid pressure " + (pressure >= 1e6 ? Number(pressure / 1e6) + " MPa" : Number(pressure / 1000) + " kPa") +
                " (" + Number(pressure) + " Pa)";
        }

        public static IReadOnlyList<string> FormatCompactComposition(MixturePresentation reading)
        {
            if (reading == null) throw new ArgumentNullException(nameof(reading));
            var result = new List<string>();
            // The short mixture name is not a complete contents list. Hover
            // lists every non-trace gas, with no top-N limit or renormalization.
            // DrawCell/DrawContainers append every warning separately, including
            // hazardous traces omitted from this contents list.
            foreach (var row in reading.GasConstituents)
                if (!row.IsTrace)
                    result.Add(row.DisplayName + " " + row.MolePercentText + " by moles" + (row.IsContaminant ? " — CONTAMINANT" : ""));
            if (reading.LiquidVolumeFraction > 0)
                result.Add("Liquid volume " + Number(100 * reading.LiquidVolumeFraction) + "%");
            if (reading.GasConstituents.Any(row => row.IsTrace))
                result.Add("Trace gases omitted from contents · select for complete composition");
            else result.Add("Select for constituent masses and partial pressures");
            return result.AsReadOnly();
        }

        public static string FormatConstituent(MixtureConstituentPresentation row)
        {
            if (row == null) throw new ArgumentNullException(nameof(row));
            var result = row.DisplayName + " · " + row.MassTextKg + " (" + Number(row.MassKg * 1000) + " g) · " + row.MolesText;
            if (row.GasMoleFraction.HasValue)
                result += " · " + row.MolePercentText + " by moles · partial pressure " + row.PartialPressureText;
            else result += " · liquid constituent · gas partial pressure not applicable";
            if (row.IsTrace) result += " · trace";
            if (row.IsContaminant) result += " · CONTAMINANT";
            return result;
        }

        public static string FormatFullComposition(MixturePresentation reading)
        {
            if (reading == null) throw new ArgumentNullException(nameof(reading));
            var text = new StringBuilder(FormatHeader(reading)).Append('\n').Append(FormatState(reading));
            if (reading.Warnings.Count > 0) text.Append("\nBreathing warnings");
            foreach (var warning in reading.Warnings) text.Append("\nWarning: ").Append(warning);
            text.Append("\nGas density ").Append(Number(reading.GasDensityKgPerCubicMetre)).Append(" kg/m³");
            if (reading.LiquidVolumeFraction > 0)
                text.Append("\nLiquid volume fraction ").Append(Number(reading.LiquidVolumeFraction)).Append("; ").Append(FluidPressureSource(reading));
            foreach (var row in reading.Constituents) text.Append('\n').Append(FormatConstituent(row));
            return text.ToString();
        }

        internal static IReadOnlyList<MixtureInspectionDetailRow> BuildDetailRows(MixturePresentation reading)
        {
            var rows = new List<MixtureInspectionDetailRow>
            {
                new MixtureInspectionDetailRow("State", FormatState(reading), "The same completed-frame assessment drives all mixture readouts.")
            };
            if (reading.Warnings.Count > 0)
            {
                rows.Add(new MixtureInspectionDetailRow("Warnings", "Breathing warnings"));
                for (var i = 0; i < reading.Warnings.Count; ++i)
                    rows.Add(new MixtureInspectionDetailRow("Warning." + i.ToString(CultureInfo.InvariantCulture), reading.Warnings[i]));
            }
            rows.Add(new MixtureInspectionDetailRow("GasDensity", "Gas density " + Number(reading.GasDensityKgPerCubicMetre) + " kg/m³"));
            if (reading.LiquidVolumeFraction > 0)
                rows.Add(new MixtureInspectionDetailRow("LiquidVolume", "Liquid volume " + Number(100 * reading.LiquidVolumeFraction) + "%",
                    FluidPressureSource(reading)));
            rows.Add(new MixtureInspectionDetailRow("Composition", "Constituents · mole % · mass · mol · partial pressure"));
            foreach (var row in reading.Constituents)
            {
                var name = row.DisplayName + (row.IsTrace ? " (trace)" : "") + (row.IsContaminant ? " · CONTAMINANT" : "");
                var details = row.GasMoleFraction.HasValue ? row.MolePercentText + " by moles" : "Liquid constituent";
                details += " · " + row.MassTextKg + " · " + row.MolesText;
                var pressure = row.PartialPressurePa.HasValue ? "Partial pressure " + row.PartialPressureText : "Gas partial pressure not applicable";
                rows.Add(new MixtureInspectionDetailRow("Constituent." + row.MaterialId,
                    name + "\n" + details + "\n" + pressure + " · " + Number(row.MassKg * 1000) + " g", FormatConstituent(row)));
            }
            return rows.AsReadOnly();
        }

        private static string FluidPressureSource(MixturePresentation reading) => reading.MechanicalPressurePa.HasValue
            ? "Fluid pressure comes from the supplied thermodynamic reading; gas partial pressures apply only to gas constituents."
            : "Fluid pressure is unavailable for this inventory; liquid mechanical pressure is not inferred from gas pressure.";

        internal static bool TryDrawCell(SelectToolHoverTextCard card, HoverTextDrawer drawer, int cell, bool selected)
        {
            var spill=ReadCellSpill(cell);
            if(spill!=null)
            {
                MixtureHoverLayout.Request(drawer);drawer.BeginShadowBar(false);
                drawer.DrawText(spill.Title,card.Styles_Title.Standard);
                foreach(var line in spill.Hover)Line(drawer,card,line);
                drawer.EndShadowBar();
            }
            if (!MixturePresentationRuntime.TryGetCell(cell, out var reading) || reading == null) return false;
            MixtureHoverLayout.Request(drawer);
            drawer.BeginShadowBar(selected);
            drawer.DrawText(FormatHeader(reading).ToUpperInvariant(), card.Styles_Title.Standard);
            Line(drawer, card, FormatState(reading));
            var composition = FormatCompactComposition(reading);
            foreach (var line in composition) Line(drawer, card, line);
            if (ColonyWorldGerms.Describe(cell) is string germs) Line(drawer, card, germs);
            if (reading.Warnings.Count == 0 && reading.IsBreathable) Line(drawer, card, "Breathing checks passed");
            foreach (var warning in reading.Warnings) Line(drawer, card, warning, true);
            drawer.EndShadowBar();
            Interlocked.Increment(ref hoverDraws);
            CellHoverObserver?.Invoke(cell, reading, composition, selected);
            return true;
        }

        internal static void UpdateSelection(CellSelectionObject selection, bool selectedValues)
        {
            if (selection == null || selection.Selectable == null) return;
            var selected = SelectTool.Instance != null && SelectTool.Instance.selected == selection.Selectable;
            var cell = selectedValues || selected ? selection.SelectedCell : selection.mouseCell;
            if (!MixturePresentationRuntime.TryGetCell(cell, out var reading) || reading == null) return;
            selection.Selectable.SetName(FormatHeader(reading));
            if (selectedValues || selected)
            {
                selection.ElementName = FormatHeader(reading);
                selection.Mass = (float)reading.TotalMassKg;
                selection.temperature = (float)reading.TemperatureKelvin;
                // Vanilla's category item closes over one projected Element.
                // Its replacement is the shared composition in the info panel.
                selection.Selectable.RemoveStatusItem(Db.Get().MiscStatusItems.ElementalCategory, immediate: true);
            }
        }

        internal static void AddDetails(CollapsibleDetailContentPanel panel, GameObject target)
        {
            if (panel == null || target == null) return;
            var selection = target.GetComponent<CellSelectionObject>();
            if(selection!=null&&ReadCellSpill(selection.SelectedCell) is MixturePipeReading spill)
                panel.SetLabel("MixedAtmospheres.PipeSpill",spill.Title+"\n"+string.Join("\n",spill.Details),"Retained fluid will return when nearby space becomes available.");
            if (selection != null && MixturePresentationRuntime.TryGetCell(selection.SelectedCell, out var cell) && cell != null)
            {
                // Replace the selected CELL's dominant-material description;
                // other selected objects retain their ordinary description.
                panel.SetLabel("Description", FormatHeader(cell), "Read-only completed-frame mixture inventory.");
                AddReadingDetails(panel, "MixedAtmospheres.Cell", cell);
                Interlocked.Increment(ref detailsDraws);
                return;
            }
            var data = ResolveContainers(target);
            var ownedDetailKeys = new List<string>();
            for (var i = 0; i < data.Owned.Count; ++i)
            {
                var item = data.Owned[i];
                var key = "MixedAtmospheres.Inventory." + i.ToString(CultureInfo.InvariantCulture);
                panel.SetLabel(key, item.label + " · " + FormatHeader(item.reading), "Completed-frame inventory: " + item.reading.SourceId);
                ownedDetailKeys.Add(key);
                foreach (var row in AddReadingDetails(panel, key, item.reading)) ownedDetailKeys.Add(key + "." + row.Key);
                Interlocked.Increment(ref ownedContainerDraws);
            }
            for(var i=0;i<data.Pipes.Count;++i)
            {
                var pipe=data.Pipes[i];var key="MixedAtmospheres.Pipe."+i.ToString(CultureInfo.InvariantCulture);
                panel.SetLabel(key,pipe.Title+"\n"+string.Join("\n",pipe.Details),"Complete mixed pipe contents.");
                ownedDetailKeys.Add(key);Interlocked.Increment(ref ownedContainerDraws);
            }
            if (data.Native.Count != 0)
            {
                panel.SetLabel("MixedAtmospheres.NativeInventory", "Native inventory · one material per packet\n" + string.Join("\n", data.Native),
                    "Actual native packets are shown individually by material. Their container volume and gas pressure are unavailable; no mixture composition is inferred.");
                Interlocked.Increment(ref nativeContainerDraws);
            }
            if (data.Errors.Count != 0)
            {
                panel.SetLabel("MixedAtmospheres.InventoryUnavailable", string.Join("\n", data.Errors),
                    "An explicitly registered mixture adapter has no valid completed-frame reading. Native projection is not used as a substitute.");
                ownedDetailKeys.Add("MixedAtmospheres.InventoryUnavailable");
            }
            if (data.NativeUnavailable.Count != 0)
                panel.SetLabel("MixedAtmospheres.NativeUnavailable", string.Join("\n", data.NativeUnavailable),
                    "No initialized native packet can currently be read from this object.");
            if (ownedDetailKeys.Count != 0)
                PrioritizeOwnedDetailRows(panel.Content.GetComponentsInChildren<DetailLabel>(true), ownedDetailKeys,
                    detail => detail.gameObject.name, MoveDetailToFirst);
        }

        // Native labels are cached across refreshes and selections. Move only
        // this refresh's explicit owned keys, in reverse order, so their title,
        // assessment and warnings precede the unchanged native description.
        // Other native labels keep their text, objects and relative ordering;
        // Commit still hides cached rows that were not populated this refresh.
        internal static void PrioritizeOwnedDetailRows<T>(IReadOnlyList<T> cachedLabels, IReadOnlyList<string> currentOwnedKeys,
            Func<T, string> getKey, Action<T> moveToFirst)
        {
            if (currentOwnedKeys.Count == 0) return;
            var labelsByKey = new Dictionary<string, T>(StringComparer.Ordinal);
            foreach (var label in cachedLabels) labelsByKey[getKey(label)] = label;
            for (var i = currentOwnedKeys.Count - 1; i >= 0; --i)
                if (labelsByKey.TryGetValue(currentOwnedKeys[i], out var label)) moveToFirst(label);
        }

        private static void MoveDetailToFirst(DetailLabel detail) => detail.transform.SetAsFirstSibling();

        private static IReadOnlyList<MixtureInspectionDetailRow> AddReadingDetails(CollapsibleDetailContentPanel panel, string prefix, MixturePresentation reading)
        {
            var rows = BuildDetailRows(reading);
            foreach (var row in rows) panel.SetLabel(prefix + "." + row.Key, row.Text, row.Tooltip);
            var labels = panel.Content.GetComponentsInChildren<DetailLabel>(true);
            // SetLabel reuses prior objects without reordering them. A warning
            // first encountered AFTER a safe atmosphere would otherwise stay
            // below all previously created constituent labels.
            foreach (var title in labels)
                if (title.gameObject.name == prefix) title.transform.SetAsLastSibling();
            foreach (var row in rows)
                foreach (var detail in labels)
                    if (detail.gameObject.name == prefix + "." + row.Key) detail.transform.SetAsLastSibling();
            // The native description template may cap visible text. Give only
            // our separate, short rows unrestricted wrapped layout, preserving
            // ordinary building/item labels and their own formatting.
            foreach (var detail in labels)
            {
                if (!detail.gameObject.name.StartsWith(prefix, StringComparison.Ordinal)) continue;
                var label = typeof(DetailLabel).GetField("label")!.GetValue(detail);
                if (label == null) continue;
                var type = label.GetType();
                foreach (var property in new[] { "maxVisibleLines", "maxVisibleCharacters", "maxVisibleWords" })
                {
                    var value = type.GetProperty(property);
                    if (value?.CanWrite == true && value.PropertyType == typeof(int)) value.SetValue(label, int.MaxValue);
                }
                var wrap = type.GetProperty("enableWordWrapping");
                if (wrap?.CanWrite == true && wrap.PropertyType == typeof(bool)) wrap.SetValue(label, true);
                var overflow = type.GetProperty("overflowMode");
                if (overflow?.CanWrite == true && overflow.PropertyType.IsEnum)
                    overflow.SetValue(label, Enum.Parse(overflow.PropertyType, "Overflow"));
            }
            return rows;
        }

        internal static void DrawContainers(SelectToolHoverTextCard card, HoverTextDrawer drawer, List<KSelectable> hoverObjects)
        {
            foreach (var selectable in hoverObjects)
            {
                if (selectable == null || selectable.GetComponent<CellSelectionObject>() != null) continue;
                var data = ResolveContainers(selectable.gameObject);
                foreach (var item in data.Owned)
                {
                    MixtureHoverLayout.Request(drawer);
                    drawer.BeginShadowBar(false);
                    drawer.DrawText(item.label + " · " + FormatHeader(item.reading), card.Styles_Title.Standard);
                    Line(drawer, card, FormatState(item.reading));
                    foreach (var line in FormatCompactComposition(item.reading)) Line(drawer, card, line);
                    foreach (var warning in item.reading.Warnings) Line(drawer, card, warning, true);
                    drawer.EndShadowBar();
                    Interlocked.Increment(ref ownedContainerDraws);
                }
                foreach(var pipe in data.Pipes)
                {
                    MixtureHoverLayout.Request(drawer);drawer.BeginShadowBar(false);drawer.DrawText(pipe.Title,card.Styles_Title.Standard);
                    foreach(var line in pipe.Hover)Line(drawer,card,line);
                    drawer.EndShadowBar();Interlocked.Increment(ref ownedContainerDraws);
                }
                if (data.Errors.Count != 0)
                {
                    MixtureHoverLayout.Request(drawer);
                    drawer.BeginShadowBar(false);
                    foreach (var error in data.Errors) Line(drawer, card, error, true);
                    drawer.EndShadowBar();
                }
                if (data.NativeUnavailable.Count != 0)
                {
                    MixtureHoverLayout.Request(drawer);
                    drawer.BeginShadowBar(false);
                    foreach (var unavailable in data.NativeUnavailable) Line(drawer, card, unavailable);
                    drawer.EndShadowBar();
                }
                if (data.Native.Count == 0) continue;
                MixtureHoverLayout.Request(drawer);
                drawer.BeginShadowBar(false);
                drawer.DrawText("NATIVE FLUID INVENTORY", card.Styles_Title.Standard);
                foreach (var line in data.Native.Take(3)) Line(drawer, card, line);
                if (data.Native.Count > 3) Line(drawer, card, "+ " + (data.Native.Count - 3).ToString(CultureInfo.InvariantCulture) + " materials · select for all packets");
                Line(drawer, card, "Native single-material packets · pressure unavailable");
                drawer.EndShadowBar();
                Interlocked.Increment(ref nativeContainerDraws);
            }
        }

        private static InspectionData ResolveContainers(GameObject target)
        {
            var cached = Cache.GetValue(target, _ => new OwnerCache());
            if (cached.Frame == Time.frameCount && cached.Generation == generation && cached.Data != null) return cached.Data;
            var result = new InspectionData();
            var roots = new HashSet<GameObject> { target };
            var identity = target.GetComponent<MinionIdentity>();
            var equipment = target.GetComponent<Equipment>();
            if (equipment == null && identity != null && identity.assignableProxy != null && identity.assignableProxy.Get() != null)
                equipment = identity.GetEquipment();
            if (equipment != null)
                foreach (var slot in equipment.Slots)
                    if (slot.assignable is Equippable item && item != null && item.isEquipped) roots.Add(item.gameObject);
            var owners = new HashSet<GameObject>(roots);
            var storages = new HashSet<IStorage>();
            var storageRoots = new Dictionary<IStorage, GameObject>();
            foreach (var root in roots)
                foreach (var storage in root.GetComponentsInChildren<IStorage>())
                {
                    storages.Add(storage);
                    storageRoots[storage] = root;
                    if (storage is Component component && component != null) owners.Add(component.gameObject);
                    foreach (var item in storage.GetItems()) if (item != null) owners.Add(item);
                }
            var seenIds = new HashSet<string>(StringComparer.Ordinal);
            var suppressed = new HashSet<GameObject>();
            foreach (var owner in owners)
            {
                if (!Owners.TryGetValue(owner, out var bindings)) continue;
                foreach (var binding in bindings.Items)
                {
                    if (binding.SuppressNative) suppressed.Add(owner);
                    MixturePresentation? reading;
                    if (binding.Handle.HasValue)
                    {
                        if (!TryResolveContainerBinding(MixturePresentationRuntime.AcquireFrame(), binding.Handle.Value, out reading, out var error))
                        { result.Errors.Add(binding.Label + " unavailable: " + error); continue; }
                    }
                    else if (!MixturePresentationRuntime.TryResolveIndependentInventory(binding.Id, out reading) || reading == null)
                    {
                        MixturePresentationRuntime.TryGetInventoryError(binding.Id, out var error);
                        result.Errors.Add(binding.Label + " unavailable: " + (error ?? "waiting for a completed inventory frame"));
                        continue;
                    }
                    if (seenIds.Add(binding.Id)) result.Owned.Add((binding.Label, reading!));
                }
            }
            var seenPackets = new HashSet<GameObject>();
            foreach (var root in roots)
                if (!suppressed.Contains(root)) AddNativePrimary(root, "Bottle / fluid item", result, seenPackets);
            foreach (var storage in storages)
            {
                if (suppressed.Contains(storageRoots[storage])) continue;
                if (storage is Component component && suppressed.Contains(component.gameObject)) continue;
                if(conduitStorageReader!=null&&storage is Storage nativeStorage)
                {
                    try
                    {
                        var reading=conduitStorageReader(nativeStorage);
                        if(reading!=null){result.Pipes.Add(reading);continue;}
                    }
                    catch(Exception error){result.Errors.Add("Conduit storage unavailable: "+error.Message);continue;}
                }
                foreach (var item in storage.GetItems()) if (item != null && !suppressed.Contains(item)) AddNativePrimary(item, "Stored packet", result, seenPackets);
            }
            var conduit = target.GetComponent<Conduit>();
            if (conduit != null && !suppressed.Contains(target))
            {
                AddNativePort(conduit, conduit.type, () => conduit.Cell, "Pipe packet", result);
            }
            var dispenser = target.GetComponent<ConduitDispenser>();
            if (dispenser != null && !suppressed.Contains(target))
            {
                AddNativePort(dispenser, dispenser.conduitType, () => (int)DispenserCell.GetValue(dispenser)!, "Output port packet", result);
            }
            var consumer = target.GetComponent<ConduitConsumer>();
            if (consumer != null && !suppressed.Contains(target) &&
                (consumer.conduitType == ConduitType.Gas || consumer.conduitType == ConduitType.Liquid))
            {
                // Read the consumer's actual initialized port, including
                // secondary inputs; do not guess from the building origin.
                AddNativePort(consumer, consumer.conduitType, () => (int)ConsumerCell.GetValue(consumer)!, "Input port packet", result);
            }
            var filter=target.GetComponent<ElementFilter>();
            if(filter!=null&&!suppressed.Contains(target))
            {
                var type=(ConduitType)FilterType.GetValue(FilterPort.GetValue(filter))!;
                if(type==ConduitType.Gas||type==ConduitType.Liquid)
                {
                    AddNativePort(filter,type,()=>(int)FilterInput.GetValue(filter)!,"Input port packet",result);
                    AddNativePort(filter,type,()=>(int)FilterSelected.GetValue(filter)!,"Filtered port packet",result);
                    AddNativePort(filter,type,()=>(int)FilterOutput.GetValue(filter)!,"Other port packet",result);
                }
            }
            cached.Frame = Time.frameCount; cached.Generation = generation; cached.Data = result;
            return result;
        }

        private static void AddNativePrimary(GameObject item, string label, InspectionData result, HashSet<GameObject> seen)
        {
            if (!seen.Add(item)) return;
            var primary = item.GetComponent<PrimaryElement>();
            if (primary == null) return;
            var element = ElementLoader.FindElementByHash(primary.ElementID);
            if (element == null || (!element.IsGas && !element.IsLiquid) || element.IsVacuum) return;
            if (!primary.isSpawned) { result.NativeUnavailable.Add("Native " + label + " unavailable: item has not spawned."); return; }
            AddNativePacket(label, primary.ElementID, primary.Mass, primary.Temperature, result.Native);
        }

        private static void AddNativePort(KMonoBehaviour owner, ConduitType type, Func<int> getCell, string label, InspectionData result)
        {
            ConduitFlow? flow = null;
            if (owner.isSpawned && Game.Instance != null)
                flow = type == ConduitType.Gas ? Game.Instance.gasConduitFlow : type == ConduitType.Liquid ? Game.Instance.liquidConduitFlow : null;
            var grid = flow == null ? null : FlowGrid.GetValue(flow) as Array;
            var cell = owner.isSpawned ? getCell() : -1;
            if(pipeReader!=null&&flow!=null&&cell>=0&&cell<Grid.WidthInCells*Grid.HeightInCells&&cell<(grid?.Length??0))
            {
                try
                {
                    var reading=pipeReader(flow,cell,label.Replace(" packet"," contents"));
                    if(reading!=null){result.Pipes.Add(reading);return;}
                }
                catch(Exception error){result.Errors.Add(label+" unavailable: "+error.Message);return;}
            }
            Func<ConduitFlow.ConduitContents>? read = flow == null ? null : () => flow.GetContents(cell);
            if (!TryReadNativePort(owner.isSpawned, cell, Grid.WidthInCells * Grid.HeightInCells, grid?.Length ?? 0, read,
                out var contents, out var unavailable))
            { result.NativeUnavailable.Add("Native " + label + " unavailable: " + unavailable); return; }
            AddNativePacket(label, contents.element, contents.mass, contents.temperature, result.Native);
        }

        // Testable safety boundary: invalid/uninitialized ports never invoke the
        // native reader. A port can also disappear during a network rebuild;
        // only expected bounds races become unavailable, not arbitrary errors.
        internal static bool TryReadNativePort(bool spawned, int cell, int gridCellCount, int flowCellCount,
            Func<ConduitFlow.ConduitContents>? read, out ConduitFlow.ConduitContents contents, out string unavailable)
        {
            contents = default;
            if (!spawned) { unavailable = "object has not spawned."; return false; }
            if (read == null || gridCellCount <= 0 || flowCellCount <= 0)
            { unavailable = "conduit flow is not initialized."; return false; }
            if (cell < 0 || cell >= gridCellCount || cell >= flowCellCount)
            { unavailable = "utility cell is outside the initialized conduit grid."; return false; }
            try { contents = read(); unavailable = ""; return true; }
            catch (IndexOutOfRangeException) { unavailable = "utility port changed during a conduit update."; return false; }
            catch (ArgumentOutOfRangeException) { unavailable = "utility port changed during a conduit update."; return false; }
        }
        private static void AddNativePacket(string label, SimHashes id, float mass, float temperature, List<string> output)
        {
            if (mass <= 0) return;
            var element = ElementLoader.FindElementByHash(id);
            if (element == null || (!element.IsGas && !element.IsLiquid) || element.IsVacuum) return;
            output.Add(label + ": " + element.name + " · " + Number(mass) + " kg · " + Number(temperature) + " K");
        }
        private static string Number(double value) => value.ToString("G10", CultureInfo.InvariantCulture);
        private static void Line(HoverTextDrawer drawer, SelectToolHoverTextCard card, string text, bool warning = false)
        { drawer.NewLine(); drawer.DrawText(text, warning ? card.Styles_Warning.Standard : card.Styles_BodyText.Standard); }
    }

    [HarmonyPatch(typeof(CellSelectionObject), "Update")]
    internal static class MixtureCellSelectionUpdatePatch
    {
        private static void Postfix(CellSelectionObject __instance) => MixtureInspection.UpdateSelection(__instance, false);
    }
    [HarmonyPatch(typeof(CellSelectionObject), nameof(CellSelectionObject.UpdateValues))]
    internal static class MixtureCellSelectionValuesPatch
    {
        private static void Postfix(CellSelectionObject __instance)
        {
            MixtureInspection.UpdateSelection(__instance, true);
            if (DetailsScreen.Instance != null && (MixturePresentationRuntime.TryGetCell(__instance.SelectedCell, out _)||MixtureInspection.ReadCellSpill(__instance.SelectedCell)!=null))
                DetailsScreen.Instance.Trigger(-1514841199);
        }
    }

    [HarmonyPatch(typeof(SelectToolHoverTextCard), nameof(SelectToolHoverTextCard.UpdateHoverElements))]
    internal static class MixtureCellHoverPatch
    {
        internal static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> source, ILGenerator generator)
        {
            var code = source.ToList();
            var masses = AccessTools.Method(typeof(HoverTextHelper), nameof(HoverTextHelper.MassStringsReadOnly));
            var begin = AccessTools.Method(typeof(HoverTextDrawer), nameof(HoverTextDrawer.BeginShadowBar));
            var end = AccessTools.Method(typeof(HoverTextDrawer), nameof(HoverTextDrawer.EndShadowBar));
            var massCalls = code.Select((instruction, index) => (instruction, index)).Where(item => item.instruction.Calls(masses)).ToArray();
            if (massCalls.Length != 1) throw new InvalidOperationException("ONI mixture hover contract changed: expected one terminal cell mass block.");
            var massIndex = massCalls[0].index;
            var beginIndex = code.FindLastIndex(massIndex, item => item.Calls(begin));
            var endIndex = code.FindIndex(massIndex, item => item.Calls(end));
            if (beginIndex < 2 || endIndex < 0 || endIndex + 1 >= code.Count || !IsLoadLocal(code[beginIndex - 2]) ||
                !IsLoadLocal(code[beginIndex - 1]) || !IsLoadLocal(code[massIndex - 1]))
                throw new InvalidOperationException("ONI mixture hover local/branch contract changed.");
            object skip = generator.DefineLabel(); MixtureInstructionLabels.Add(code[endIndex + 1], skip);
            var replacement = new List<CodeInstruction>
            {
                new CodeInstruction(OpCodes.Ldarg_0), Clone(code[beginIndex - 2]), Clone(code[massIndex - 1]), Clone(code[beginIndex - 1]),
                new CodeInstruction(OpCodes.Call, AccessTools.Method(typeof(MixtureInspection), nameof(MixtureInspection.TryDrawCell))),
                new CodeInstruction(OpCodes.Brtrue, skip)
            };
            MixtureInstructionLabels.Move(code[beginIndex - 2], replacement[0]);
            code.InsertRange(beginIndex - 2, replacement);
            var finish = code.FindLastIndex(item => item.Calls(AccessTools.Method(typeof(HoverTextDrawer), nameof(HoverTextDrawer.EndDrawing))));
            var objects = AccessTools.Field(typeof(SelectToolHoverTextCard), "overlayValidHoverObjects");
            if (finish < 1 || !IsLoadLocal(code[finish - 1]) || objects == null)
                throw new InvalidOperationException("ONI mixture hover drawing contract changed.");
            var containers = new List<CodeInstruction>
            {
                new CodeInstruction(OpCodes.Ldarg_0), Clone(code[finish - 1]), new CodeInstruction(OpCodes.Ldarg_0),
                new CodeInstruction(OpCodes.Ldfld, objects),
                new CodeInstruction(OpCodes.Call, AccessTools.Method(typeof(MixtureInspection), nameof(MixtureInspection.DrawContainers)))
            };
            MixtureInstructionLabels.Move(code[finish - 1], containers[0]);
            code.InsertRange(finish - 1, containers);
            MixtureInspection.CellHoverHookReady = true;
            return code;
        }
        private static bool IsLoadLocal(CodeInstruction instruction) => instruction.opcode == OpCodes.Ldloc || instruction.opcode == OpCodes.Ldloc_S ||
            instruction.opcode == OpCodes.Ldloc_0 || instruction.opcode == OpCodes.Ldloc_1 || instruction.opcode == OpCodes.Ldloc_2 || instruction.opcode == OpCodes.Ldloc_3;
        private static CodeInstruction Clone(CodeInstruction instruction) => new CodeInstruction(instruction.opcode, instruction.operand);
    }

    [HarmonyPatch(typeof(SimpleInfoScreen), "RefreshInfoPanel")]
    internal static class MixtureDetailsPanelPatch
    {
        internal static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> source)
        {
            var code = source.ToList();
            var commits = code.Select((instruction, index) => (instruction, index)).Where(item => item.instruction.Calls(
                AccessTools.Method(typeof(CollapsibleDetailContentPanel), nameof(CollapsibleDetailContentPanel.Commit)))).ToArray();
            if (commits.Length != 1 || commits[0].index == 0 || code[commits[0].index - 1].opcode != OpCodes.Ldarg_0)
                throw new InvalidOperationException("ONI mixture details contract changed: expected one final panel Commit.");
            var index = commits[0].index - 1;
            var first = new CodeInstruction(OpCodes.Ldarg_0); MixtureInstructionLabels.Move(code[index], first);
            code.InsertRange(index, new[] { first, new CodeInstruction(OpCodes.Ldarg_1),
                new CodeInstruction(OpCodes.Call, AccessTools.Method(typeof(MixtureInspection), nameof(MixtureInspection.AddDetails))) });
            MixtureInspection.DetailsHookReady = true;
            return code;
        }
    }

    // The shipped net-framework Harmony labels field is List<mscorlib.Label>.
    // .NET Standard's facade does not forward that reference at compile time;
    // Unity resolves the very same runtime Label type. Avoid a build-time
    // framework replacement by touching only this metadata list through IList.
    internal static class MixtureInstructionLabels
    {
        private static readonly FieldInfo Labels = AccessTools.Field(typeof(CodeInstruction), "labels");
        internal static void Add(CodeInstruction instruction, object label) => ((IList)Labels.GetValue(instruction)!).Add(label);
        internal static void Move(CodeInstruction source, CodeInstruction destination)
        {
            var from = (IList)Labels.GetValue(source)!; var to = (IList)Labels.GetValue(destination)!;
            foreach (var label in from) to.Add(label);
            from.Clear();
        }
    }
}
