using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static class AtmospherePanelStyle
    {
        // The slate body, mauve title strip and flat buttons of ONI's pause menu.
        internal static readonly Color Panel = new Color32(59, 59, 72, 250);
        internal static readonly Color Rule = new Color32(101, 99, 115, 255);
        private static readonly Color Header = new Color32(113, 86, 105, 255);
        internal static float Scale => Mathf.Max(.85f, Screen.height / 1080f);
        internal static Font Font => ToolTipScreen.Instance?.defaultTooltipBodyStyle?.sdfFont?.sourceFontFile
            ?? Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");

        internal static void DrawWindow(Rect rect, float titleHeight)
        {
            var color = GUI.color;
            GUI.color = Panel;
            GUI.DrawTexture(rect, Texture2D.whiteTexture);
            GUI.color = Header;
            GUI.DrawTexture(new Rect(rect.x, rect.y, rect.width, titleHeight), Texture2D.whiteTexture);
            GUI.color = color;
        }

        private static Texture2D? normal, hover, pressed;
        private static Texture2D Background(Color32 color)
        {
            var texture = new Texture2D(1, 1) { hideFlags = HideFlags.HideAndDontSave };
            texture.SetPixel(0, 0, color);
            texture.Apply(false, true);
            return texture;
        }
        internal static GUIStyle Button(Font font)
        {
            if (normal == null) normal = Background(new Color32(87, 87, 102, 255));
            if (hover == null) hover = Background(new Color32(111, 110, 128, 255));
            if (pressed == null) pressed = Background(new Color32(140, 89, 118, 255));
            var style = new GUIStyle(GUI.skin.button) { font = font, fontSize = 12, border = new RectOffset() };
            style.normal.background = normal;
            style.hover.background = hover;
            style.active.background = style.onNormal.background = style.onHover.background = style.onActive.background = pressed;
            return style;
        }
    }
}
