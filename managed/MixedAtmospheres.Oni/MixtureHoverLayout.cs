using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Move the game's pooled text, icons, backgrounds and selection borders
    // together. Fonts, content, pointer position and native drawing stay intact.
    internal static class MixtureHoverLayout
    {
        private static ConditionalWeakTable<HoverTextDrawer, Layout> layouts = new ConditionalWeakTable<HoverTextDrawer, Layout>();
        private static readonly List<WeakReference<Layout>> live = new List<WeakReference<Layout>>();
        private static readonly string[] PoolNames = { "shadowBars", "selectBorders", "textWidgets", "iconWidgets" };
        private sealed class Layout
        {
            internal readonly RectTransform[] Roots = new RectTransform[4];
            internal readonly Vector2[] Original = new Vector2[4];
            internal readonly Vector3[] Corners = new Vector3[4];
            internal readonly RectTransform Parent;
            internal bool Requested, Shifted;
            internal Layout(HoverTextDrawer drawer)
            {
                for (var index = 0; index < PoolNames.Length; ++index)
                {
                    var pool = AccessTools.Field(typeof(HoverTextDrawer), PoolNames[index])?.GetValue(drawer)
                        ?? throw new NotSupportedException("Missing native hover pool: " + PoolNames[index]);
                    Roots[index] = AccessTools.Field(pool.GetType(), "root")?.GetValue(pool) as RectTransform
                        ?? throw new NotSupportedException("Missing native hover pool canvas.");
                }
                Parent = Roots[0].parent as RectTransform ?? throw new NotSupportedException("Missing hover canvas parent.");
                foreach (var root in Roots)
                    if (root.parent != Parent) throw new NotSupportedException("Native hover pools no longer share one canvas.");
            }
            internal void Restore()
            {
                if (Shifted)
                    for (var index = 0; index < Roots.Length; ++index)
                        if (Roots[index] != null) Roots[index].anchoredPosition = Original[index];
                Shifted = Requested = false;
            }
            internal void Fit()
            {
                if (!Requested || Parent == null || Parent.rect.width <= 16 || Parent.rect.height <= 16) return;
                var left = double.PositiveInfinity; var bottom = double.PositiveInfinity;
                var right = double.NegativeInfinity; var top = double.NegativeInfinity;
                // EndDrawing has hidden unused pooled bars. Their live rects
                // bound all wrapped text, icons and the selected-cell outline.
                for (var pool = 0; pool < 2; ++pool)
                    for (var index = 0; index < Roots[pool].childCount; ++index)
                    {
                        var child = Roots[pool].GetChild(index) as RectTransform;
                        if (child == null || !child.gameObject.activeSelf) continue;
                        child.GetWorldCorners(Corners);
                        foreach (var corner in Corners)
                        {
                            var point = Parent.InverseTransformPoint(corner);
                            left = Math.Min(left, point.x); bottom = Math.Min(bottom, point.y);
                            right = Math.Max(right, point.x); top = Math.Max(top, point.y);
                        }
                    }
                if (double.IsPositiveInfinity(left)) return;
                var view = Parent.rect;
                var fit = MixtureHoverViewport.Fit(new HoverBounds(left, bottom, right, top),
                    new HoverBounds(view.xMin, view.yMin, view.xMax, view.yMax), 8);
                if (fit.X == 0 && fit.Y == 0) return;
                for (var index = 0; index < Roots.Length; ++index)
                {
                    Original[index] = Roots[index].anchoredPosition;
                    Roots[index].anchoredPosition = Original[index] + new Vector2((float)fit.X, (float)fit.Y);
                }
                Shifted = true;
            }
        }
        static MixtureHoverLayout() { MixturePresentationRuntime.SessionEnding += Reset; }
        internal static void Request(HoverTextDrawer drawer)
        {
            var layout = layouts.GetValue(drawer, value => {
                var created = new Layout(value); live.Add(new WeakReference<Layout>(created)); return created;
            });
            layout.Requested = true;
        }
        internal static void Begin(HoverTextDrawer drawer) { if (layouts.TryGetValue(drawer, out var layout)) layout.Restore(); }
        internal static void End(HoverTextDrawer drawer) { if (layouts.TryGetValue(drawer, out var layout)) layout.Fit(); }
        private static void Reset()
        {
            foreach (var reference in live) if (reference.TryGetTarget(out var layout)) layout.Restore();
            live.Clear(); layouts = new ConditionalWeakTable<HoverTextDrawer, Layout>();
        }
        internal static void RequireContract()
        {
            foreach (var name in PoolNames)
            {
                var pool = AccessTools.Field(typeof(HoverTextDrawer), name) ?? throw new NotSupportedException("Missing native hover pool.");
                if (AccessTools.Field(pool.FieldType, "root")?.FieldType != typeof(RectTransform))
                    throw new NotSupportedException("Native hover pool root changed.");
            }
        }
    }

    [HarmonyPatch(typeof(HoverTextDrawer), nameof(HoverTextDrawer.BeginDrawing))]
    internal static class MixtureHoverBeginPatch
    {
        private static void Prefix(HoverTextDrawer __instance) => MixtureHoverLayout.Begin(__instance);
        private static bool Prepare() { MixtureHoverLayout.RequireContract(); return true; }
    }
    [HarmonyPatch(typeof(HoverTextDrawer), nameof(HoverTextDrawer.EndDrawing))]
    internal static class MixtureHoverEndPatch
    {
        private static void Postfix(HoverTextDrawer __instance) => MixtureHoverLayout.End(__instance);
    }
}
