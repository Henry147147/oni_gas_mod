using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Runs only inside the disposable presentation colony. These are native
    // pure-material items, not registered read-only mixture inventories.
    internal sealed class NitrogenVisualValidation : IDisposable
    {
        private readonly List<GameObject> objects = new List<GameObject>();
        private readonly List<object> evidence = new List<object>();
        private bool allPassed = true;
        private readonly SimHashes[] hashes = { OniNitrogenElements.Gas, OniNitrogenElements.Liquid, OniNitrogenElements.Solid };
        private bool disposed;

        internal NitrogenVisualValidation(int x, int y)
        {
            if (ColonyRuntimeValidation.Settings?.Mode != "presentation")
                throw new InvalidOperationException("Nitrogen fixtures require the explicit private presentation mode.");
            OniNitrogenElements.ValidateLoaded();
            var cells = new List<int>();
            // World clearing leaves native loose debris intact. Reserve three
            // genuinely empty nearby cells before spawning any test item.
            for (var i = 0; i < hashes.Length; ++i)
            {
                var candidates = Enumerable.Range(x - 3, 39).OrderBy(column => Math.Abs(column - (x + i * 10)));
                var found = false;
                foreach (var column in candidates)
                {
                    if (column < 0 || column >= Grid.WidthInCells) continue;
                    var cell = Grid.XYToCell(column, y);
                    if (!Grid.IsValidBuildingCell(cell) || !Grid.IsActiveWorld(cell) || Grid.Solid[cell] ||
                        !Grid.Solid[Grid.CellBelow(cell)] || cells.Contains(cell)) continue;
                    var empty = true;
                    for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer)
                        if (Grid.Objects[cell, layer] != null) { empty = false; break; }
                    if (!empty) continue;
                    cells.Add(cell); found = true; break;
                }
                Require(found, "No empty supported cell is available for a native nitrogen test item.");
            }
            try
            {
                for (var i = 0; i < hashes.Length; ++i)
                {
                    var element = ElementLoader.FindElementByHash(hashes[i]);
                    var cell = cells[i];
                    Require(Grid.IsValidBuildingCell(cell) && Grid.IsActiveWorld(cell) && !Grid.Solid[cell] && Grid.Solid[Grid.CellBelow(cell)],
                        "The nitrogen test item needs an empty supported cell.");
                    for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer)
                        Require(Grid.Objects[cell, layer] == null, "The nitrogen test cell is occupied.");
                    Require(hashes[i].CreateTag() == element.tag && GameTagExtensions.Create(hashes[i]) == element.tag &&
                        Assets.GetPrefab(element.tag) != null, "A nitrogen hash-to-tag or prefab lookup is invalid.");
                    var item = element.substance.SpawnResource(Grid.CellToPosCCC(cell, Grid.SceneLayer.Ore), 1,
                        element.defaultValues.temperature, byte.MaxValue, 0, prevent_merge: true, forceTemperature: true);
                    Require(item != null, "The native nitrogen resource factory failed.");
                    objects.Add(item!);
                }
            }
            catch { Dispose(); throw; }
        }

        internal void Select(int index)
        {
            Require(!disposed && index >= 0 && index < objects.Count, "Invalid nitrogen fixture selection.");
            var item = objects[index];
            Require(item.GetComponents<KMonoBehaviour>().Where(component => component.enabled).All(component => component.isSpawned),
                "The nitrogen test item has not spawned.");
            var point = item.transform.position;
            CameraController.Instance.SnapTo(point); CameraController.Instance.SetTargetPos(point, 20, false);
            CameraController.Instance.OrthographicSize = 10;
            SelectTool.Instance.Select(item.GetComponent<KSelectable>(), true);
        }

        internal void ValidateSelected(int index)
        {
            var item = objects[index]; var element = ElementLoader.FindElementByHash(hashes[index]);
            var primary = item.GetComponent<PrimaryElement>();
            var selected = SelectTool.Instance.selected == item.GetComponent<KSelectable>();
            var screen = Resources.FindObjectsOfTypeAll<SimpleInfoScreen>().FirstOrDefault(value => value.gameObject.activeInHierarchy &&
                ReferenceEquals(AccessTools.Field(typeof(SimpleInfoScreen), "selectedTarget").GetValue(value), item));
            var panel = screen == null ? null : AccessTools.Field(typeof(SimpleInfoScreen), "infoPanel").GetValue(screen) as CollapsibleDetailContentPanel;
            var labels = panel == null ? Array.Empty<MixtureContainerVisualValidation.LabelEvidence>() : panel.Content.GetComponentsInChildren<DetailLabel>(true)
                .Select(ReadLabel).ToArray();
            var live = labels.Where(label => label.ActiveInHierarchy && label.TextEnabled && label.Height > 0).ToArray();
            var description = live.SingleOrDefault(label => label.Name == "Description");
            var rawDescription = element.FullDescription(addHardnessColor: false);
            var descriptionPreserved = MixtureContainerVisualValidation.DescriptionIsPreserved(rawDescription, description);
            var nativeReading = !element.IsSolid && live.Any(label => label.Name == "MixedAtmospheres.NativeInventory" &&
                label.Text.Contains(element.name) && label.Tooltip.Contains("gas pressure are unavailable"));
            var noFakeMixture = live.All(label => !label.Name.StartsWith("MixedAtmospheres.Inventory.", StringComparison.Ordinal));
            var passed = selected && screen != null && primary.isSpawned && primary.ElementID == hashes[index] && primary.Mass == 1 &&
                primary.Temperature == element.defaultValues.temperature && descriptionPreserved && noFakeMixture && (element.IsSolid || nativeReading);
            evidence.Add(new
            {
                ElementId = OniNitrogenElements.Identity(hashes[index]), Hash = (int)hashes[index], element.idx, element.name,
                Tag = element.tag.ToString(), TemplateIsolated = true, NativeSpawned = primary.isSpawned,
                primary.Mass, primary.Temperature, Selected = selected, PanelTarget = screen != null,
                NativeDescriptionPreserved = descriptionPreserved, NativeSingleMaterialReading = nativeReading,
                RawNativeDescription = rawDescription, ExpectedNativeDescription = MixtureContainerVisualValidation.NativeLabelText(rawDescription),
                ActualNativeDescription = description?.Text ?? "",
                NoFakeMixture = noFakeMixture, Passed = passed, UnityFrame = Time.frameCount, Labels = labels
            });
            allPassed &= passed;
            WriteReport();
        }
        internal void RequireAllPassed() => Require(evidence.Count == hashes.Length && allPassed,
            "Native nitrogen resource inspection failed; inspect nitrogen-validation.json and all three screenshots.");

        private static MixtureContainerVisualValidation.LabelEvidence ReadLabel(DetailLabel detail)
        {
            var text = typeof(DetailLabel).GetField("label")!.GetValue(detail);
            return new MixtureContainerVisualValidation.LabelEvidence
            {
                Name = detail.name, Text = (string?)text?.GetType().GetProperty("text")?.GetValue(text) ?? "",
                ActiveSelf = detail.gameObject.activeSelf, ActiveInHierarchy = detail.gameObject.activeInHierarchy,
                TextEnabled = text is Behaviour behaviour && behaviour.isActiveAndEnabled,
                Height = (detail.transform as RectTransform)?.rect.height ?? 0,
                Sibling = detail.transform.GetSiblingIndex(),
                Tooltip = detail.toolTip != null && detail.toolTip.multiStringCount > 0 ? detail.toolTip.GetMultiString(0) : ""
            };
        }
        private void WriteReport() => File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "nitrogen-validation.json"), ReportJson.Serialize(new
        {
            Kind = "actual-Unity-native-nitrogen-registration-and-item-inspection",
            OniNitrogenElements.ModelVersion, PhysicsOwner = "native ONI; nominal pure-phase definitions", MixturePhasePhysicsValidated = false,
            Evidence = evidence
        }));
        public void Dispose()
        {
            if (disposed) return; disposed = true;
            foreach (var item in objects) if (item != null)
            {
                if (SelectTool.Instance?.selected == item.GetComponent<KSelectable>()) SelectTool.Instance.Select(null);
                Util.KDestroyGameObject(item);
            }
            objects.Clear();
        }
        private static void Require(bool condition, string message) { if (!condition) throw new InvalidDataException(message); }
    }
}
