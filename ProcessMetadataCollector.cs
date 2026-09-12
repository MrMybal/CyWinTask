using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace CyWinTask;
public sealed record ProcessMetadata(string Category, ImageSource? Icon);
public sealed class ProcessMetadataCollector
{
    private readonly Dictionary<(int, long), (string? Path, ImageSource? Icon)> metadata = new();
    private readonly Dictionary<string, ImageSource?> icons = new(StringComparer.OrdinalIgnoreCase);
    private readonly string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows).TrimEnd('\\') + "\\";
    public Dictionary<int, ProcessMetadata> Collect(List<ProcessSample> processes)
    {
        var visible = new HashSet<int>();
        EnumWindows((window, _) => { if (IsWindowVisible(window) && GetWindow(window, 4) == IntPtr.Zero && GetWindowTextLength(window) > 0) { GetWindowThreadProcessId(window, out int id); visible.Add(id); } return true; }, IntPtr.Zero);
        var present = processes.Select(p => (p.Id, p.Created)).ToHashSet();
        foreach (var key in metadata.Keys.Where(k => !present.Contains(k)).ToArray()) metadata.Remove(key);
        int budget = 32;
        foreach (var process in processes.OrderByDescending(p => visible.Contains(p.Id)))
        {
            var key = (process.Id, process.Created);
            if (metadata.ContainsKey(key) || budget-- <= 0) continue;
            string? path = null; ImageSource? icon = null;
            try
            {
                path = ProcessActions.GetPath(process);
                if (!icons.TryGetValue(path, out icon))
                {
                    icon = Extract(path);
                    if (icons.Count >= 256) icons.Clear();
                    icons[path] = icon;
                }
            }
            catch (Exception) { /* Protected or exited processes retain a neutral fallback icon. */ }
            metadata[key] = (path, icon);
        }
        return processes.ToDictionary(p => p.Id, p =>
        {
            metadata.TryGetValue((p.Id, p.Created), out var info);
            string category = visible.Contains(p.Id) ? "Applications" : p.Id <= 4 || info.Path?.StartsWith(windows, StringComparison.OrdinalIgnoreCase) == true ? "Processus Windows" : "Processus en arrière-plan";
            return new ProcessMetadata(category, info.Icon);
        });
    }
    private static ImageSource? Extract(string path)
    {
        ExtractIconEx(path, 0, out var large, out var small, 1);
        try
        {
            var handle = small != IntPtr.Zero ? small : large;
            if (handle == IntPtr.Zero) return null;
            var image = Imaging.CreateBitmapSourceFromHIcon(handle, Int32Rect.Empty, BitmapSizeOptions.FromWidthAndHeight(20, 20));
            image.Freeze(); return image;
        }
        finally { if (small != IntPtr.Zero) DestroyIcon(small); if (large != IntPtr.Zero) DestroyIcon(large); }
    }
    private delegate bool EnumWindowProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowProc callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextLength(IntPtr window);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out int processId);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] private static extern uint ExtractIconEx(string path, int index, out IntPtr large, out IntPtr small, uint count);
    [DllImport("user32.dll")] private static extern bool DestroyIcon(IntPtr icon);
}
