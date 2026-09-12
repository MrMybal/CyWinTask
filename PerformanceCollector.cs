using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace CyWinTask;
public sealed record PerfMetric(string Id, string Label, double? Value, string Unit, double? Maximum = null);
public sealed record PerfDevice(string Id, string Title, string Description, string Kind, List<PerfMetric> Metrics);
public sealed record PerfSnapshot(DateTime Time, List<PerfDevice> Devices, double CollectionMs);

// One PDH query, retained native buffers, no subprocesses or WMI in the refresh loop.
public sealed class PerformanceCollector : IDisposable
{
    private readonly IntPtr query;
    private readonly Dictionary<string, Counter> counters = new();
    private readonly List<GpuAdapter> adapters;
    private readonly string processorName;
    public PerformanceCollector()
    {
        if (PdhOpenQuery(null, IntPtr.Zero, out query) != 0) throw new InvalidOperationException("Compteurs Windows indisponibles.");
        try
        {
            Add("cpu", @"\Processor Information(*)\% Processor Time");
            Add("idle", @"\PhysicalDisk(*)\% Idle Time");
            Add("read", @"\PhysicalDisk(*)\Disk Read Bytes/sec");
            Add("write", @"\PhysicalDisk(*)\Disk Write Bytes/sec");
            Add("receive", @"\Network Interface(*)\Bytes Received/sec");
            Add("send", @"\Network Interface(*)\Bytes Sent/sec");
            Add("engine", @"\GPU Engine(*)\Utilization Percentage");
            Add("dedicated", @"\GPU Adapter Memory(*)\Dedicated Usage");
            Add("shared", @"\GPU Adapter Memory(*)\Shared Usage");
            adapters = GpuAdapter.Enumerate();
            using var key = Registry.LocalMachine.OpenSubKey(@"HARDWARE\DESCRIPTION\System\CentralProcessor\0");
            processorName = key?.GetValue("ProcessorNameString")?.ToString()?.Trim() ?? "Processeur";
            PdhCollectQueryData(query);
        }
        catch { Dispose(); throw; }
    }
    private void Add(string id, string path)
    {
        uint status = PdhAddEnglishCounter(query, path, IntPtr.Zero, out var handle);
        counters[id] = new Counter(handle, status);
    }
    public PerfSnapshot Collect(Snapshot system)
    {
        var timer = Stopwatch.StartNew();
        uint status = PdhCollectQueryData(query);
        var data = counters.ToDictionary(c => c.Key, c => status == 0 ? c.Value.Read() : new Dictionary<string, double>());
        double? Get(string key, string instance) => data[key].TryGetValue(instance, out double v) ? v : null;
        var devices = new List<PerfDevice>();
        double? cpu = Get("cpu", "_Total") ?? system.Cpu;
        var cpuMetrics = new List<PerfMetric> { new("cpu", "Utilisation totale", cpu, "%", 100) };
        foreach (var entry in data["cpu"].Where(x => !x.Key.Contains("_Total")).OrderBy(x => int.TryParse(x.Key.Split(',')[0], out var g) ? g : 0).ThenBy(x => int.TryParse(x.Key.Split(',').Last(), out var c) ? c : 0))
            cpuMetrics.Add(new("core:" + entry.Key, "Processeur logique " + entry.Key, Math.Clamp(entry.Value, 0, 100), "%", 100));
        devices.Add(new("cpu", "Processeur", processorName + $" · {Environment.ProcessorCount} processeurs logiques", "cpu", cpuMetrics));
        devices.Add(new("memory", "Mémoire", $"{system.UsedGb:N1} / {system.TotalGb:N1} Go utilisés · {system.MemoryPercent} %", "memory",
            new() { new("used", "Mémoire physique utilisée", system.UsedGb, "Go", system.TotalGb), new("available", "Mémoire physique disponible", system.TotalGb - system.UsedGb, "Go", system.TotalGb), CommitMemory.Read() }));
        foreach (string disk in data["idle"].Keys.Union(data["read"].Keys).Union(data["write"].Keys).Where(x => x != "_Total").Order())
            devices.Add(new("disk:" + disk, "Disque " + disk, "Disque physique · " + disk, "disk", new()
            {
                new("active", "Temps d’activité", Get("idle", disk) is double idle ? Math.Clamp(100 - idle, 0, 100) : null, "%", 100),
                new("read", "Lecture", Get("read", disk) / 1048576, "Mo/s"), new("write", "Écriture", Get("write", disk) / 1048576, "Mo/s")
            }));
        foreach (string network in data["receive"].Keys.Union(data["send"].Keys).Order())
            devices.Add(new("network:" + network, "Réseau", network, "network", new()
            { new("receive", "Réception", Get("receive", network) * 8 / 1000000, "Mbit/s"), new("send", "Envoi", Get("send", network) * 8 / 1000000, "Mbit/s") }));
        var engines = AggregateEngines(data["engine"]);
        foreach (var adapter in adapters)
        {
            var matching = engines.Where(e => e.Key.StartsWith(adapter.Id + "/", StringComparison.OrdinalIgnoreCase)).ToArray();
            double? usage = matching.Length == 0 ? null : matching.Max(e => e.Value);
            double? Memory(string key)
            {
                var values = data[key].Where(x => x.Key.StartsWith(adapter.Id, StringComparison.OrdinalIgnoreCase)).ToArray();
                return values.Length == 0 ? null : values.Sum(x => x.Value) / 1073741824;
            }
            var metrics = new List<PerfMetric> { new("usage", "Utilisation · moteur le plus actif", usage, "%", 100) };
            var types = new[] { "3D", "Copy", "VideoEncode", "VideoDecode" }.Concat(matching.Select(e => e.Key.Split('/')[2])).Distinct(StringComparer.OrdinalIgnoreCase);
            foreach (string type in types)
            {
                var values = matching.Where(e => e.Key.EndsWith("/" + type, StringComparison.OrdinalIgnoreCase)).ToArray();
                metrics.Add(new("engine:" + type, type switch { "VideoEncode" => "Encodage vidéo", "VideoDecode" => "Décodage vidéo", _ => type }, values.Length == 0 ? null : values.Max(e => e.Value), "%", 100));
            }
            metrics.Add(new("dedicated", "Mémoire GPU dédiée", Memory("dedicated"), "Go", adapter.DedicatedBytes / 1073741824d));
            metrics.Add(new("shared", "Mémoire GPU partagée", Memory("shared"), "Go", adapter.SharedBytes / 1073741824d));
            devices.Add(new(adapter.Id, "GPU " + adapter.Index, adapter.Name, "gpu", metrics));
        }
        return new(DateTime.UtcNow, devices, timer.Elapsed.TotalMilliseconds);
    }
    // Sum process contributions for the SAME physical engine, then take the busiest engine.
    public static Dictionary<string, double> AggregateEngines(Dictionary<string, double> values)
    {
        var result = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in values)
        {
            var m = Regex.Match(entry.Key, @"(luid_0x[0-9a-f]+_0x[0-9a-f]+)_phys_(\d+)_eng_(\d+)_engtype_(.+)", RegexOptions.IgnoreCase);
            if (!m.Success) continue;
            string key = m.Groups[1].Value + "/" + m.Groups[2].Value + ":" + m.Groups[3].Value + "/" + m.Groups[4].Value;
            result[key] = Math.Clamp(result.GetValueOrDefault(key) + entry.Value, 0, 100);
        }
        return result;
    }
    public void Dispose() { foreach (var counter in counters.Values) counter.Dispose(); if (query != IntPtr.Zero) PdhCloseQuery(query); }
    private sealed class Counter(IntPtr handle, uint added) : IDisposable
    {
        private IntPtr buffer;
        private uint capacity;
        public Dictionary<string, double> Read()
        {
            var values = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
            if (added != 0) return values;
            for (int attempt = 0; attempt < 3; attempt++)
            {
                uint bytes = capacity;
                uint status = PdhGetFormattedCounterArray(handle, 0x200 | 0x8000, ref bytes, out uint count, buffer);
                if (status == 0x800007D2)
                {
                    if (bytes > 32 * 1024 * 1024) return values;
                    buffer = Marshal.ReAllocHGlobal(buffer, (IntPtr)bytes); capacity = bytes; continue;
                }
                if (status != 0 || count > capacity / 24) return values;
                for (int i = 0; i < count; i++)
                {
                    var item = IntPtr.Add(buffer, i * 24);
                    int validity = Marshal.ReadInt32(item, 8);
                    if (validity != 0 && validity != 1) continue;
                    string? name = Marshal.PtrToStringUni(Marshal.ReadIntPtr(item));
                    double value = BitConverter.Int64BitsToDouble(Marshal.ReadInt64(item, 16));
                    if (name != null && double.IsFinite(value)) values[name] = Math.Max(0, value);
                }
                break;
            }
            return values;
        }
        public void Dispose() { if (buffer != IntPtr.Zero) { Marshal.FreeHGlobal(buffer); buffer = IntPtr.Zero; } }
    }
    [DllImport("pdh.dll", CharSet = CharSet.Unicode, EntryPoint = "PdhOpenQueryW")] private static extern uint PdhOpenQuery(string? source, IntPtr user, out IntPtr query);
    [DllImport("pdh.dll", CharSet = CharSet.Unicode, EntryPoint = "PdhAddEnglishCounterW")] private static extern uint PdhAddEnglishCounter(IntPtr query, string path, IntPtr user, out IntPtr counter);
    [DllImport("pdh.dll")] private static extern uint PdhCollectQueryData(IntPtr query);
    [DllImport("pdh.dll", EntryPoint = "PdhGetFormattedCounterArrayW")] private static extern uint PdhGetFormattedCounterArray(IntPtr counter, uint format, ref uint size, out uint count, IntPtr items);
    [DllImport("pdh.dll")] private static extern uint PdhCloseQuery(IntPtr query);
}

public sealed record GpuAdapter(int Index, string Id, string Name, ulong DedicatedBytes, ulong SharedBytes)
{
    public static List<GpuAdapter> Enumerate()
    {
        var adapters = new List<GpuAdapter>();
        var iid = new Guid("770aae78-f26f-4dba-a829-253c83d1b387");
        if (CreateDXGIFactory1(ref iid, out var factory) < 0) return adapters;
        try
        {
            var enumerate = Marshal.GetDelegateForFunctionPointer<EnumAdapters>(Marshal.ReadIntPtr(Marshal.ReadIntPtr(factory), 12 * IntPtr.Size));
            for (uint index = 0; index < 32; index++)
            {
                if (enumerate(factory, index, out var adapter) < 0) break;
                try
                {
                    var getDesc = Marshal.GetDelegateForFunctionPointer<GetDescription>(Marshal.ReadIntPtr(Marshal.ReadIntPtr(adapter), 10 * IntPtr.Size));
                    if (getDesc(adapter, out var d) >= 0 && (d.Flags & 2) == 0)
                        adapters.Add(new(adapters.Count, $"luid_0x{d.High:x8}_0x{d.Low:x8}", d.Description, d.Dedicated, d.Shared));
                }
                finally { Marshal.Release(adapter); }
            }
        }
        finally { Marshal.Release(factory); }
        return adapters;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct AdapterDescription
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Description;
        public uint Vendor, Device, SubSystem, Revision;
        public ulong Dedicated, DedicatedSystem, Shared;
        public uint Low, High, Flags;
    }
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int EnumAdapters(IntPtr self, uint index, out IntPtr adapter);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int GetDescription(IntPtr self, out AdapterDescription description);
    [DllImport("dxgi.dll")] private static extern int CreateDXGIFactory1(ref Guid iid, out IntPtr factory);
}
