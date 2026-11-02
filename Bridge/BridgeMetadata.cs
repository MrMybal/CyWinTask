using System.Runtime.InteropServices;

namespace CyWinTask;

internal sealed class BridgeMetadata
{
    private readonly Dictionary<(int Id, long Created), string> paths = new();
    private readonly string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows).TrimEnd('\\') + "\\";

    public (byte Category, string Path)[] Collect(List<ProcessSample> processes)
    {
        var visible = new HashSet<int>();
        EnumWindows((window, _) =>
        {
            if (IsWindowVisible(window) && GetWindow(window, 4) == IntPtr.Zero && GetWindowTextLength(window) > 0)
            {
                GetWindowThreadProcessId(window, out int id);
                visible.Add(id);
            }
            return true;
        }, IntPtr.Zero);
        var alive = processes.Select(p => (p.Id, p.Created)).ToHashSet();
        foreach (var key in paths.Keys.Where(k => !alive.Contains(k)).ToArray()) paths.Remove(key);
        int budget = 32;
        foreach (var process in processes.OrderByDescending(p => visible.Contains(p.Id)))
        {
            var key = (process.Id, process.Created);
            if (paths.ContainsKey(key) || budget-- <= 0) continue;
            try { paths[key] = ProcessActions.GetPath(process); }
            catch { paths[key] = ""; }
        }
        return processes.Select(process =>
        {
            paths.TryGetValue((process.Id, process.Created), out var path);
            byte category = visible.Contains(process.Id) ? (byte)0
                : process.Id <= 4 || path?.StartsWith(windows, StringComparison.OrdinalIgnoreCase) == true ? (byte)2 : (byte)1;
            return (category, path ?? "");
        }).ToArray();
    }

    private delegate bool WindowCallback(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(WindowCallback callback, IntPtr data);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextLength(IntPtr window);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out int id);
}
