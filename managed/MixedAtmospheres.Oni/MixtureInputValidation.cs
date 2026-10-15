using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using UnityEngine;
using UnityEngine.EventSystems;
using UiEventSystem = UnityEngine.EventSystems.EventSystem;

namespace MixedAtmospheres.Oni
{
    // Private opt-in observation only. No event is dispatched, no control is
    // clicked, and no input/focus/raycast setting is changed by this recorder.
    // A bounded recent-event ring remains available during keep-open testing.
    internal static class MixtureInputValidation
    {
        private const int MaximumEvents = 512;
        private static readonly Queue<InputRecord> Recent = new Queue<InputRecord>();
        private static readonly Dictionary<string, long> Counts = new Dictionary<string, long>(StringComparer.Ordinal);
        private static readonly List<RaycastResult> Raycasts = new List<RaycastResult>();
        private static readonly List<object> Captures = new List<object>();
        private static bool initialized, dirty, previousFocused, previousAppFocused, previousGamepad, reportedError;
        private static float nextHeartbeat, nextWrite;
        private static long dropped;
        private static string lastError = "";
        private static string pendingCapture = "";
        private static float captureAt;
        private static int captureCount;
        private static bool Enabled => ColonyRuntimeValidation.Settings?.Mode == "presentation";

        private sealed class InputRecord
        {
            public string Kind = "", Target = "", Callbacks = "", Detail = "";
            public int Frame, ScreenWidth, ScreenHeight, PointerButton = -1, ClickCount;
            public double Seconds;
            public float MouseX, MouseY;
            public bool Focused, ApplicationFocused, Gamepad, MouseHeld, EventSystemEnabled;
            public string InputModule = "", Overlay = "", FirstRaycast = "", ClickHandler = "", DownHandler = "";
            public string[] RaycastOrder = Array.Empty<string>();
        }

        private static void Tick()
        {
            if (!Enabled) return;
            try
            {
                var now = Time.realtimeSinceStartup;
                var focused = KInputManager.isFocused;
                var appFocused = Application.isFocused;
                var gamepad = KInputManager.currentControllerIsGamepad;
                if (!initialized || focused != previousFocused || appFocused != previousAppFocused || gamepad != previousGamepad)
                {
                    Record("focus", "", null, null, "Native focus/controller state changed.", true);
                    previousFocused = focused; previousAppFocused = appFocused; previousGamepad = gamepad;
                    initialized = true;
                }
                if (Input.GetMouseButtonDown(0)) Record("mouse.down", "", null, null, "Actual Unity left-button down.", true);
                if (Input.GetMouseButtonUp(0)) Record("mouse.up", "", null, null, "Actual Unity left-button up.", true);
                if (Input.mouseScrollDelta.y != 0) Record("mouse.scroll", "", null, null, "Actual Unity scroll input.", true);
                if (pendingCapture.Length != 0 && now >= captureAt)
                {
                    var reason = pendingCapture; pendingCapture = "";
                    var frame = MixturePresentationRuntime.AcquireFrame();
                    if (Game.Instance != null && OverlayScreen.Instance != null)
                    {
                        var filename = "interactive-" + (++captureCount).ToString("00") + "-" + reason + ".png";
                        ColonyRuntimeValidation.CaptureScreenshot(filename);
                        Captures.Add(new { Screenshot = filename, Reason = reason, UnityFrame = Time.frameCount,
                            PublicationGeneration = frame?.PublicationGeneration, Revision = frame?.Revision,
                            Overlay = OverlayScreen.Instance.GetMode().ToString(),
                            Appearance = frame?.Model.VisualOptions, MixtureInspection.HoverDrawCount,
                            MixtureInspection.DetailDrawCount, MixturePresentationRuntime.HasWorldProvider,
                            MixturePresentationRuntime.InventoryProviderCount, MixtureInspection.SessionBindingCount,
                            PhysicsOwner = "native ONI; mixture readings only while explicit fixtures are published" });
                        dirty = true;
                    }
                }
                if (now >= nextHeartbeat)
                {
                    nextHeartbeat = now + 10;
                    Record("heartbeat", "", null, null, "Read-only mouse-position raycast.", true);
                }
                if (dirty && now >= nextWrite)
                {
                    nextWrite = now + .5f;
                    Write();
                }
            }
            catch (Exception error) { Failed(error); }
        }

        private static void Record(string kind, string target, Delegate? callbacks, PointerEventData? pointer, string detail = "", bool raycast = false)
        {
            if (!Enabled) return;
            try
            {
                var mouse = Input.mousePosition;
                var system = UiEventSystem.current;
                var record = new InputRecord
                {
                    Kind = kind, Target = Limit(target), Callbacks = CallbackNames(callbacks), Detail = Limit(detail),
                    Frame = Time.frameCount, Seconds = Time.realtimeSinceStartup,
                    MouseX = mouse.x, MouseY = mouse.y, ScreenWidth = Screen.width, ScreenHeight = Screen.height,
                    Focused = KInputManager.isFocused, ApplicationFocused = Application.isFocused,
                    Gamepad = KInputManager.currentControllerIsGamepad, MouseHeld = Input.GetMouseButton(0),
                    EventSystemEnabled = system != null && system.isActiveAndEnabled,
                    InputModule = system != null && system.currentInputModule != null ? system.currentInputModule.GetType().FullName ?? "" : "none",
                    Overlay = OverlayScreen.Instance != null ? OverlayScreen.Instance.GetMode().ToString() : "none",
                    PointerButton = pointer == null ? -1 : (int)pointer.button, ClickCount = pointer?.clickCount ?? 0
                };
                if (pointer != null)
                {
                    record.FirstRaycast = PathOf(pointer.pointerCurrentRaycast.gameObject);
                    record.ClickHandler = PathOf(ExecuteEvents.GetEventHandler<IPointerClickHandler>(pointer.pointerCurrentRaycast.gameObject));
                    record.DownHandler = PathOf(ExecuteEvents.GetEventHandler<IPointerDownHandler>(pointer.pointerCurrentRaycast.gameObject));
                }
                if (raycast && system != null)
                {
                    Raycasts.Clear();
                    // This queries the native raycasters at the actual mouse
                    // position. It never invokes Process/ExecuteEvents/input.
                    system.RaycastAll(new PointerEventData(system) { position = mouse }, Raycasts);
                    record.RaycastOrder = Raycasts.Take(5).Select(hit => PathOf(hit.gameObject) +
                        " [depth " + hit.depth + ", sort " + hit.sortingOrder + "]").ToArray();
                    var first = Raycasts.Count == 0 ? null : Raycasts[0].gameObject;
                    record.FirstRaycast = PathOf(first);
                    record.ClickHandler = PathOf(ExecuteEvents.GetEventHandler<IPointerClickHandler>(first));
                    record.DownHandler = PathOf(ExecuteEvents.GetEventHandler<IPointerDownHandler>(first));
                    Raycasts.Clear();
                }
                Counts.TryGetValue(kind, out var count); Counts[kind] = count + 1;
                if (kind == "overlay.selected") RequestCapture("overlay");
                else if (kind == "parameters.changed") RequestCapture("appearance");
                else if (kind == "mouse.scroll") RequestCapture("inspection-scroll");
                else if (kind == "mouse.up" && record.FirstRaycast == "none" && CameraController.Instance?.baseCamera != null)
                {
                    var position = CameraController.Instance.baseCamera.ScreenToWorldPoint(mouse);
                    if (MixturePresentationRuntime.TryGetCell(Grid.PosToCell(position), out var reading) && reading != null)
                        RequestCapture("mixture-hover");
                }
                if (Recent.Count == MaximumEvents) { Recent.Dequeue(); ++dropped; }
                Recent.Enqueue(record); dirty = true;
            }
            catch (Exception error) { Failed(error); }
        }

        private static string CallbackNames(Delegate? callbacks) => callbacks == null ? "none" : Limit(string.Join("; ",
            callbacks.GetInvocationList().Take(8).Select(callback => callback.Method.DeclaringType?.FullName + "." + callback.Method.Name)));

        private static string PathOf(GameObject? value)
        {
            if (value == null) return "none";
            var names = new List<string>(); var current = value.transform;
            for (var depth = 0; depth < 8 && current != null; ++depth, current = current.parent) names.Add(current.name);
            names.Reverse(); return Limit(string.Join("/", names));
        }

        private static string Limit(string text) => text.Length <= 768 ? text : text.Substring(0, 768);

        private static void RequestCapture(string reason)
        {
            if (captureCount >= 24 || Game.Instance == null) return;
            pendingCapture = reason; captureAt = Time.realtimeSinceStartup + .6f;
        }

        private static void Write()
        {
            var destination = Path.Combine(ColonyRuntimeValidation.OutputDirectory, "mixture-input-validation.json");
            Directory.CreateDirectory(ColonyRuntimeValidation.OutputDirectory);
            var temporary = destination + ".tmp";
            File.WriteAllText(temporary, ReportJson.Serialize(new
            {
                Kind = "actual-native-input-observation", ObservesOnly = true,
                UpdatedUtc = System.DateTime.UtcNow.ToString("O"), MaximumRecentEvents = MaximumEvents,
                DroppedEvents = dropped, Counts, LastError = lastError, Recent = Recent.ToArray(), Captures
            }));
            if (File.Exists(destination)) File.Replace(temporary, destination, null);
            else File.Move(temporary, destination);
            dirty = false;
        }

        private static void Failed(Exception error)
        {
            lastError = error.GetType().Name + ": " + error.Message;
            if (reportedError) return;
            reportedError = true;
            Debug.LogWarning("[Mixed Atmospheres] Private input observation failed: " + lastError);
        }

        private static Delegate? ToggleCallbacks(KToggle toggle) => AccessTools.Field(typeof(KToggle), "onClick").GetValue(toggle) as Delegate;

        [HarmonyPatch(typeof(Game), "Update")]
        private static class ObserveGameUpdate
        { private static void Postfix() => Tick(); }

        [HarmonyPatch(typeof(KToggle), nameof(KToggle.OnPointerClick))]
        private static class ObserveTogglePointer
        {
            private static void Prefix(KToggle __instance, PointerEventData eventData)
            {
                if (Enabled) Record("ktoggle.pointer", PathOf(__instance.gameObject), ToggleCallbacks(__instance), eventData,
                    "interactable=" + __instance.IsInteractable() + "; isOn=" + __instance.isOn);
            }
        }

        [HarmonyPatch(typeof(KToggle), nameof(KToggle.Click))]
        private static class ObserveToggleClick
        {
            private static void Prefix(KToggle __instance)
            {
                if (Enabled) Record("ktoggle.click", PathOf(__instance.gameObject), ToggleCallbacks(__instance), null,
                    "interactable=" + __instance.IsInteractable() + "; isOn=" + __instance.isOn);
            }
        }

        [HarmonyPatch(typeof(MultiToggle), nameof(MultiToggle.OnPointerClick))]
        private static class ObserveMultiTogglePointer
        {
            private static void Prefix(MultiToggle __instance, PointerEventData eventData)
            {
                if (Enabled) Record("multitoggle.pointer", PathOf(__instance.gameObject), __instance.onClick, eventData,
                    "state=" + __instance.CurrentState);
            }
        }

        [HarmonyPatch(typeof(OverlayMenu), "OnToggleSelect")]
        private static class ObserveOverlaySelection
        {
            private static void Postfix(KIconToggleMenu.ToggleInfo __0)
            {
                if (Enabled) Record("overlay.selected", __0.text, null, null,
                    "simView=" + AccessTools.Field(__0.GetType(), "simView").GetValue(__0));
            }
        }

        [HarmonyPatch(typeof(ToolParameterMenu), "OnChange")]
        private static class ObserveFilterChanged
        {
            private static void Postfix(ToolParameterMenu __instance)
            {
                if (!Enabled) return;
                var toggles = AccessTools.Field(typeof(ToolParameterMenu), "currentTogglesData").GetValue(__instance) as ToolParameterMenu.ToggleData[];
                Record("parameters.changed", PathOf(__instance.gameObject), null, null,
                    toggles == null ? "No current toggle data." : string.Join("; ", toggles.Select(toggle => toggle.name + "=" + toggle.state)));
            }
        }
    }
}
