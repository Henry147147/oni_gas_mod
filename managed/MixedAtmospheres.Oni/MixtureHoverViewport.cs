using System;

namespace MixedAtmospheres.Oni
{
    internal readonly struct HoverBounds
    {
        internal readonly double Left, Bottom, Right, Top;
        internal HoverBounds(double left, double bottom, double right, double top)
        {
            if (!Finite(left) || !Finite(bottom) || !Finite(right) || !Finite(top) || right < left || top < bottom)
                throw new ArgumentException("Hover bounds must be finite and ordered.");
            Left = left; Bottom = bottom; Right = right; Top = top;
        }
        private static bool Finite(double value) => !double.IsNaN(value) && !double.IsInfinity(value);
    }

    internal readonly struct HoverTranslation
    {
        internal readonly double X, Y;
        internal readonly bool Fits;
        internal HoverTranslation(double x, double y, bool fits) { X = x; Y = y; Fits = fits; }
    }

    // Coordinates are local to the actual hover canvas. UI scale has already
    // been applied by its transforms; no assumed pixels-per-canvas-unit ratio.
    internal static class MixtureHoverViewport
    {
        internal static HoverTranslation Fit(HoverBounds content, HoverBounds viewport, double margin)
        {
            if (double.IsNaN(margin) || double.IsInfinity(margin) || margin < 0 ||
                2 * margin >= viewport.Right - viewport.Left || 2 * margin >= viewport.Top - viewport.Bottom)
                throw new ArgumentException("Hover viewport has no usable area.");
            var left = viewport.Left + margin; var right = viewport.Right - margin;
            var bottom = viewport.Bottom + margin; var top = viewport.Top - margin;
            var widthFits = content.Right - content.Left <= right - left;
            var heightFits = content.Top - content.Bottom <= top - bottom;
            var x = !widthFits || content.Left < left ? left - content.Left : content.Right > right ? right - content.Right : 0;
            var y = !heightFits || content.Top > top ? top - content.Top : content.Bottom < bottom ? bottom - content.Bottom : 0;
            return new HoverTranslation(x, y, widthFits && heightFits);
        }
    }
}
