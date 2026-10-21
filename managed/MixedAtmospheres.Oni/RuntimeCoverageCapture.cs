using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Read-only discovery of the instantiated prefab and recipe registries.
    // Export stays beside the private contract, never in a release package.
    // Discovery does not certify gameplay behavior or completeness of dispatch.
    internal static class RuntimeCoverageCapture
    {
        internal static void Export(string directory)
        {
            var report = new RuntimeCoverageReport
            {
                AssemblySha256 = IntegrationProbe.VerifyContract(),
                Platform = Application.platform.ToString(),
                LoadedDlcIds = DlcManager.GetActiveDLCIds().OrderBy(id => id, StringComparer.Ordinal).ToArray(),
                PrefabRegistryInitialized = Assets.Prefabs != null && Assets.Prefabs.Count > 0
            };
            if (Assets.Prefabs != null)
                foreach (var prefab in Assets.Prefabs.Where(prefab => prefab != null).OrderBy(prefab => prefab.PrefabTag.ToString(), StringComparer.Ordinal))
                {
                    var record = new CapturedPrefab { Id = prefab.PrefabTag.ToString() };
                    var components = prefab.GetComponentsInChildren<Component>(true);
                    for (var ordinal = 0; ordinal < components.Length; ++ordinal)
                    {
                        var component = components[ordinal];
                        if (component == null) { ++record.MissingComponents; continue; }
                        record.Components.Add(component.GetType().FullName ?? component.GetType().Name);
                        if (component is ElementConverter converter)
                        {
                            var conversion = new CapturedConverter { ComponentOrdinal = ordinal, InputIsCategory = converter.inputIsCategory };
                            if (converter.consumedElements != null)
                                foreach (var input in converter.consumedElements)
                                    conversion.Inputs.Add(new CapturedConversionInput
                                    { Tag = input.Tag.ToString(), MassKgPerSecond = input.MassConsumptionRate, Active = input.IsActive });
                            if (converter.outputElements != null)
                                foreach (var output in converter.outputElements)
                                    conversion.Outputs.Add(new CapturedConversionOutput
                                    {
                                        Element = output.elementHash.ToString(), ElementHash = (int)output.elementHash,
                                        MassKgPerSecond = output.massGenerationRate, MinimumTemperatureKelvin = output.minOutputTemperature,
                                        StoreOutput = output.storeOutput, UseEntityTemperature = output.useEntityTemperature, Active = output.IsActive
                                    });
                            record.Converters.Add(conversion);
                        }
                    }
                    report.Prefabs.Add(record);
                }
            // Element identities and phases (no properties); disabled elements
            // belong to content that is not loaded.
            if (ElementLoader.elements != null)
                foreach (var element in ElementLoader.elements.Where(element => element != null).OrderBy(element => element.idx))
                    report.Elements.Add(new CapturedMaterialIdentity { Name = element.id.ToString(), Disabled = element.disabled,
                        Phase = element.IsVacuum ? "vacuum" : element.IsSolid ? "solid" : element.IsLiquid ? "liquid" : "gas" });
            // Do not use Get(): it can create a singleton. A missing registry
            // is explicit evidence of incomplete discovery, not an empty game.
            var manager = AccessTools.Field(typeof(ComplexRecipeManager), "_Instance")?.GetValue(null) as ComplexRecipeManager;
            report.RecipeRegistryInitialized = manager != null && manager.recipes.Count > 0;
            if (manager != null)
                foreach (var recipe in manager.recipes.OrderBy(recipe => recipe.id, StringComparer.Ordinal))
                    report.Recipes.Add(new CapturedRecipe
                    {
                        Id = recipe.id,
                        Fabricators = recipe.fabricators?.Select(tag => tag.ToString()).ToArray() ?? Array.Empty<string>(),
                        Inputs = CaptureElements(recipe.ingredients), Outputs = CaptureElements(recipe.results),
                        RequiredDlcIds = recipe.GetRequiredDlcIds() ?? Array.Empty<string>(),
                        ForbiddenDlcIds = recipe.GetForbiddenDlcIds() ?? Array.Empty<string>(),
                        ConsumedHighEnergyParticles = recipe.consumedHEP, ProducedHighEnergyParticles = recipe.producedHEP
                    });
            File.WriteAllText(Path.Combine(directory, "runtime-coverage.json"), ReportJson.Serialize(report));
            UnityEngine.Debug.Log("[Mixed Atmospheres] Runtime discovery captured " + report.Prefabs.Count +
                " prefabs and " + report.Recipes.Count + " recipes; ownership and coverage completeness remain unverified.");
        }

        private static List<CapturedRecipeElement> CaptureElements(ComplexRecipe.RecipeElement[] elements)
        {
            var result = new List<CapturedRecipeElement>();
            if (elements != null)
                foreach (var element in elements)
                    result.Add(new CapturedRecipeElement
                    {
                        Material = element.material.ToString(),
                        PossibleMaterials = element.possibleMaterials?.Select(tag => tag.ToString()).ToArray() ?? Array.Empty<string>(),
                        PossibleAmounts = element.possibleMaterialAmounts == null ? Array.Empty<float>() : (float[])element.possibleMaterialAmounts.Clone(),
                        TemperatureOperation = element.temperatureOperation.ToString(),
                        StoreElement = element.storeElement, InheritElement = element.inheritElement, DoNotConsume = element.doNotConsume
                    });
            return result;
        }
    }

    [DataContract]
    internal sealed class RuntimeCoverageReport
    {
        [DataMember] public int SchemaVersion = 1;
        [DataMember] public string OniBuild = "744825", Kind = "runtime-registry-discovery", AssemblySha256 = "", Platform = "";
        [DataMember] public bool CoverageComplete = false, AuthoritativeColonySimulationReady = false;
        [DataMember] public bool PrefabRegistryInitialized, RecipeRegistryInitialized;
        // Main-menu loaded content is not a colony's selected content mask.
        [DataMember] public bool SaveDlcConfigurationKnown = false;
        [DataMember] public string[] LoadedDlcIds = Array.Empty<string>();
        [DataMember] public List<CapturedPrefab> Prefabs = new List<CapturedPrefab>();
        [DataMember] public List<CapturedRecipe> Recipes = new List<CapturedRecipe>();
        [DataMember] public List<CapturedMaterialIdentity> Elements = new List<CapturedMaterialIdentity>();
        [DataMember] public string[] Unresolved =
        {
            "No colony simulation was exercised by this capture.",
            "Runtime-created components, state machines, dynamic recipes and indirect dispatch need separate discovery.",
            "All components are recorded; fluid relevance and per-prefab ownership have not been reviewed.",
            "Recipe tags may represent categories or substitutes; identities do not establish chemical or energy balance.",
            "Native registries, transient sources and unobserved DLC configurations remain unverified."
        };
    }
    [DataContract]
    internal sealed class CapturedMaterialIdentity
    {
        [DataMember] public string Name = "", Phase = "";
        [DataMember] public bool Disabled;
    }
    [DataContract]
    internal sealed class CapturedPrefab
    {
        [DataMember] public string Id = "";
        [DataMember] public int MissingComponents;
        [DataMember] public List<string> Components = new List<string>();
        [DataMember] public List<CapturedConverter> Converters = new List<CapturedConverter>();
    }
    [DataContract]
    internal sealed class CapturedConverter
    {
        [DataMember] public int ComponentOrdinal;
        [DataMember] public bool InputIsCategory;
        [DataMember] public List<CapturedConversionInput> Inputs = new List<CapturedConversionInput>();
        [DataMember] public List<CapturedConversionOutput> Outputs = new List<CapturedConversionOutput>();
    }
    [DataContract]
    internal sealed class CapturedConversionInput
    {
        [DataMember] public string Tag = "";
        [DataMember] public float MassKgPerSecond;
        [DataMember] public bool Active;
    }
    [DataContract]
    internal sealed class CapturedConversionOutput
    {
        [DataMember] public string Element = "";
        [DataMember] public int ElementHash;
        [DataMember] public float MassKgPerSecond, MinimumTemperatureKelvin;
        [DataMember] public bool StoreOutput, UseEntityTemperature, Active;
    }
    [DataContract]
    internal sealed class CapturedRecipe
    {
        [DataMember] public string Id = "";
        [DataMember] public string[] Fabricators = Array.Empty<string>(), RequiredDlcIds = Array.Empty<string>(), ForbiddenDlcIds = Array.Empty<string>();
        [DataMember] public List<CapturedRecipeElement> Inputs = new List<CapturedRecipeElement>(), Outputs = new List<CapturedRecipeElement>();
        [DataMember] public int ConsumedHighEnergyParticles, ProducedHighEnergyParticles;
    }
    [DataContract]
    internal sealed class CapturedRecipeElement
    {
        [DataMember] public string Material = "", TemperatureOperation = "";
        [DataMember] public string[] PossibleMaterials = Array.Empty<string>();
        [DataMember] public float[] PossibleAmounts = Array.Empty<float>();
        [DataMember] public bool StoreElement, InheritElement, DoNotConsume;
    }
}
