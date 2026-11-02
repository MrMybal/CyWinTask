using System.Windows;
using System.Windows.Media;

namespace CyWinTask;

// Shared semantic colours for controls created in code.
internal static class UiTheme
{
    private static readonly Dictionary<string, Brush> Cached = new(StringComparer.Ordinal);
    public static Brush Brush(string role)
    {
        if (!Cached.TryGetValue(role, out var brush))
            Cached[role] = brush = (Brush)Application.Current.FindResource("Cy" + role);
        return brush;
    }
    public static Color Color(string role) => ((SolidColorBrush)Brush(role)).Color;
}
