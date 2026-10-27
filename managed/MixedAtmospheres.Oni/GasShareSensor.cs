using System.Collections.Generic;
using HarmonyLib;
using KSerialization;
using STRINGS;
using TUNING;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Gas Share Sensor (docs/nitrogen.md): an automation sensor that compares
    // one gas's mole share of its cell's gas with a threshold, as the Atmo
    // Sensor does with pressure. Hard mode needs it to hold a room inside its
    // oxygen window. It reuses the gas element sensor's animation and research.
    public sealed class GasShareSensorConfig : IBuildingConfig
    {
        public const string ID = "MixedAtmospheresGasShareSensor";
        private const string Prefix = "STRINGS.BUILDINGS.PREFABS.MIXEDATMOSPHERESGASSHARESENSOR.";

        internal static void InitializeStrings()
        {
            Strings.Add(Prefix + "NAME", UI.FormatAsLink("Gas Share Sensor", ID));
            Strings.Add(Prefix + "DESC", "Gas share sensors watch the makeup of the air, not just its pressure.");
            Strings.Add(Prefix + "EFFECT", "Sends a " + UI.FormatAsAutomationState("Green Signal", UI.AutomationState.Active) + " or a " +
                UI.FormatAsAutomationState("Red Signal", UI.AutomationState.Standby) +
                " when the selected gas's share of the air in its cell, by moles, rises above or falls below the chosen percentage.");
            Strings.Add(Prefix + "LOGIC_PORT", "Gas share");
            Strings.Add(Prefix + "LOGIC_PORT_ACTIVE", "Sends a " + UI.FormatAsAutomationState("Green Signal", UI.AutomationState.Active) + " while the gas's share is on the chosen side of the threshold");
            Strings.Add(Prefix + "LOGIC_PORT_INACTIVE", "Otherwise, sends a " + UI.FormatAsAutomationState("Red Signal", UI.AutomationState.Standby));
            Strings.Add(Prefix + "SIDESCREEN_TITLE", "Gas Share");
            Strings.Add(Prefix + "THRESHOLD_NAME", "Share");
        }
        internal static string Text(string key) => Strings.Get(Prefix + key).String;

        public override BuildingDef CreateBuildingDef()
        {
            var def = BuildingTemplates.CreateBuildingDef(ID, 1, 1, "world_element_sensor_kanim", 30, 30f, TUNING.BUILDINGS.CONSTRUCTION_MASS_KG.TIER0,
                MATERIALS.REFINED_METALS, 1600f, BuildLocationRule.Anywhere, TUNING.BUILDINGS.DECOR.PENALTY.TIER0, NOISE_POLLUTION.NONE, 0.2f);
            def.Overheatable = false; def.Floodable = true; def.Entombable = true;
            def.ViewMode = OverlayModes.Logic.ID; def.AudioCategory = "Metal"; def.SceneLayer = Grid.SceneLayer.Building;
            def.LogicOutputPorts = new List<LogicPorts.Port>
            {
                LogicPorts.Port.OutputPort(LogicSwitch.PORT_ID, new CellOffset(0, 0), Text("LOGIC_PORT"), Text("LOGIC_PORT_ACTIVE"), Text("LOGIC_PORT_INACTIVE"), true, false)
            };
            GeneratedBuildings.RegisterWithOverlay(OverlayModes.Logic.HighlightItemIDs, ID);
            return def;
        }
        public override void DoPostConfigureComplete(GameObject go)
        {
            go.AddOrGet<Filterable>().filterElementState = Filterable.ElementState.Gas;
            go.AddOrGet<GasShareSensor>().manuallyControlled = false;
        }
    }

    [SerializationConfig(MemberSerialization.OptIn)]
    public sealed class GasShareSensor : Switch, ISaveLoadable, IThresholdSwitch, ISim200ms
    {
        [MyCmpGet] private Filterable filterable = null!;
        [MyCmpGet] private KBatchedAnimController animation = null!;
        [Serialize] private float threshold = 25f;
        [Serialize] private bool activateAbove = true;
        private float share;
        private bool shownOn, shown;

        protected override void OnSpawn()
        {
            base.OnSpawn();
            OnToggle += _ => Publish();
            Publish();
        }
        public void Sim200ms(float dt)
        {
            var element = filterable != null && filterable.SelectedTag.IsValid ? ElementLoader.GetElement(filterable.SelectedTag) : null;
            share = (float)(100 * ColonySensors.GasShare(Grid.PosToCell(this), element));
            var on = element != null && (activateAbove ? share > threshold : share < threshold);
            if (on != switchedOn) Toggle();
        }
        private void Publish()
        {
            GetComponent<LogicPorts>()?.SendSignal(LogicSwitch.PORT_ID, switchedOn ? 1 : 0);
            if (animation == null || (shown && shownOn == switchedOn)) return;
            animation.Play(switchedOn ? "on_pre" : "on_pst");
            animation.Queue(switchedOn ? "on" : "off");
            shown = true; shownOn = switchedOn;
        }

        public float Threshold { get => threshold; set => threshold = value; }
        public bool ActivateAboveThreshold { get => activateAbove; set => activateAbove = value; }
        public float CurrentValue => share;
        public float RangeMin => 0f;
        public float RangeMax => 100f;
        public float GetRangeMinInputField() => 0f;
        public float GetRangeMaxInputField() => 100f;
        public LocString Title => new LocString(GasShareSensorConfig.Text("SIDESCREEN_TITLE"));
        public LocString ThresholdValueName => new LocString(GasShareSensorConfig.Text("THRESHOLD_NAME"));
        public string AboveToolTip => "Sends a green signal while the gas is above {0} of the air";
        public string BelowToolTip => "Sends a green signal while the gas is below {0} of the air";
        public string Format(float value, bool units) => GameUtil.FloatToString(value, "0.#") + (units ? "%" : "");
        public LocString ThresholdValueUnits() => new LocString("%");
        public float ProcessedSliderValue(float input) => Mathf.Round(input * 10f) / 10f;
        public float ProcessedInputValue(float input) => Mathf.Clamp(input, 0f, 100f);
        public ThresholdScreenLayoutType LayoutType => ThresholdScreenLayoutType.SliderBar;
        public int IncrementScale => 1;
        public NonLinearSlider.Range[] GetRanges => NonLinearSlider.GetDefaultRange(RangeMax);
    }

    // The building joins the automation menu beside the gas element sensor
    // and needs the same research.
    [HarmonyPatch(typeof(GeneratedBuildings), nameof(GeneratedBuildings.LoadGeneratedBuildings))]
    internal static class GasShareSensorPlanScreen
    {
        private static void Prefix()
        {
            var sorting = TUNING.BUILDINGS.PLANSUBCATEGORYSORTING;
            var subcategory = sorting.TryGetValue(LogicElementSensorGasConfig.ID, out var vanilla) ? vanilla : "sensors";
            sorting[GasShareSensorConfig.ID] = subcategory;
            ModUtil.AddBuildingToPlanScreen("Automation", GasShareSensorConfig.ID, subcategory, LogicElementSensorGasConfig.ID);
        }
    }
    [HarmonyPatch(typeof(Db), nameof(Db.Initialize))]
    internal static class GasShareSensorResearch
    {
        private static void Postfix()
        {
            var tech = Db.Get().Techs.TryGetTechForTechItem(LogicElementSensorGasConfig.ID);
            if (tech == null) { Debug.LogWarning("[Mixed Atmospheres] The gas element sensor has no research; the gas share sensor needs none."); return; }
            if (!tech.unlockedItemIDs.Contains(GasShareSensorConfig.ID)) tech.unlockedItemIDs.Add(GasShareSensorConfig.ID);
        }
    }
}
