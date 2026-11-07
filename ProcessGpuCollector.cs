using System.Runtime.InteropServices;
using System.Text.RegularExpressions;

namespace CyWinTask;

public sealed record ProcessGpuUsage(double Own, double Group);

// One retained PDH query, sampled only at the normal process refresh cadence.
public sealed class ProcessGpuCollector : IDisposable
{
    private static readonly Regex EngineName = new(
        @"^pid_(\d+)_(luid_0x[0-9a-f]+_0x[0-9a-f]+)_phys_(\d+)_eng_(\d+)_engtype_(.+?)(?:#\d+)?$",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant | RegexOptions.Compiled);
    private IntPtr query;
    private IntPtr counter;
    private IntPtr buffer;
    private uint capacity;
    private bool available;
    private bool primed;

    public ProcessGpuCollector()
    {
        if (PdhOpenQuery(null, IntPtr.Zero, out query) != 0) return;
        available = PdhAddEnglishCounter(query, @"\GPU Engine(*)\Utilization Percentage", IntPtr.Zero, out counter) == 0;
        if (available) primed = PdhCollectQueryData(query) == 0;
    }

    public Dictionary<int, ProcessGpuUsage>? Collect(IReadOnlyList<ProcessSample> processes)
    {
        if (!available || !primed || PdhCollectQueryData(query) != 0) return null;
        var values = ReadCounters();
        if (values == null || !values.Keys.Any(name => EngineName.IsMatch(name))) return null;
        return Aggregate(values, processes);
    }

    private Dictionary<string, double>? ReadCounters()
    {
        for (int attempt = 0; attempt < 3; attempt++)
        {
            uint bytes = capacity;
            uint status = PdhGetFormattedCounterArray(counter, 0x200 | 0x8000, ref bytes, out uint count, buffer);
            if (status == 0x800007D2)
            {
                if (bytes > 32 * 1024 * 1024) return null;
                buffer = Marshal.ReAllocHGlobal(buffer, (IntPtr)bytes);
                capacity = bytes;
                continue;
            }
            if (status != 0 || count > capacity / 24) return null;
            var values = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
            for (int i = 0; i < count; i++)
            {
                IntPtr item = IntPtr.Add(buffer, i * 24);
                int validity = Marshal.ReadInt32(item, 8);
                if (validity != 0 && validity != 1) continue;
                string? name = Marshal.PtrToStringUni(Marshal.ReadIntPtr(item));
                double value = BitConverter.Int64BitsToDouble(Marshal.ReadInt64(item, 16));
                if (name != null && double.IsFinite(value)) values[name] = Math.Max(0, value);
            }
            return values;
        }
        return null;
    }

    // The displayed percentage is the busiest physical GPU engine, including all
    // process contributions to that engine. Parent groups use their descendants.
    public static Dictionary<int, ProcessGpuUsage> Aggregate(
        IReadOnlyDictionary<string, double> counters, IReadOnlyList<ProcessSample> processes)
    {
        var byId = processes.ToDictionary(p => p.Id);
        var ownEngines = new Dictionary<int, Dictionary<string, double>>();
        foreach (var entry in counters)
        {
            var match = EngineName.Match(entry.Key);
            if (!match.Success || !int.TryParse(match.Groups[1].Value, out int pid) || !byId.ContainsKey(pid)
                || !double.IsFinite(entry.Value)) continue;
            string engine = string.Join('/', match.Groups[2].Value, match.Groups[3].Value,
                match.Groups[4].Value, match.Groups[5].Value);
            if (!ownEngines.TryGetValue(pid, out var engines)) ownEngines[pid] = engines = new(StringComparer.OrdinalIgnoreCase);
            engines[engine] = engines.GetValueOrDefault(engine) + Math.Max(0, entry.Value);
        }
        var children = processes.ToDictionary(p => p.Id, _ => new List<int>());
        foreach (var process in processes)
            if (process.ParentId != process.Id && byId.TryGetValue(process.ParentId, out var parent)
                && parent.Created <= process.Created)
                children[parent.Id].Add(process.Id);
        var result = new Dictionary<int, ProcessGpuUsage>(processes.Count);
        var state = new Dictionary<int, byte>(processes.Count);
        Dictionary<string, double> Visit(int pid)
        {
            if (state.GetValueOrDefault(pid) == 1) return new(StringComparer.OrdinalIgnoreCase);
            state[pid] = 1;
            var group = ownEngines.TryGetValue(pid, out var own)
                ? new Dictionary<string, double>(own, StringComparer.OrdinalIgnoreCase)
                : new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
            foreach (int child in children[pid])
                foreach (var engine in Visit(child)) group[engine.Key] = group.GetValueOrDefault(engine.Key) + engine.Value;
            double Peak(Dictionary<string, double>? values) => values?.Count > 0 ? Math.Clamp(values.Values.Max(), 0, 100) : 0;
            result[pid] = new(Peak(own), Peak(group));
            state[pid] = 2;
            return group;
        }
        foreach (var process in processes) if (state.GetValueOrDefault(process.Id) == 0) Visit(process.Id);
        return result;
    }

    public void Dispose()
    {
        if (buffer != IntPtr.Zero) Marshal.FreeHGlobal(buffer);
        if (query != IntPtr.Zero) PdhCloseQuery(query);
        buffer = IntPtr.Zero;
        query = IntPtr.Zero;
    }
    [DllImport("pdh.dll", CharSet = CharSet.Unicode, EntryPoint = "PdhOpenQueryW")] private static extern uint PdhOpenQuery(string? source, IntPtr user, out IntPtr query);
    [DllImport("pdh.dll", CharSet = CharSet.Unicode, EntryPoint = "PdhAddEnglishCounterW")] private static extern uint PdhAddEnglishCounter(IntPtr query, string path, IntPtr user, out IntPtr counter);
    [DllImport("pdh.dll")] private static extern uint PdhCollectQueryData(IntPtr query);
    [DllImport("pdh.dll", EntryPoint = "PdhGetFormattedCounterArrayW")] private static extern uint PdhGetFormattedCounterArray(IntPtr counter, uint format, ref uint size, out uint count, IntPtr items);
    [DllImport("pdh.dll")] private static extern uint PdhCloseQuery(IntPtr query);
}
