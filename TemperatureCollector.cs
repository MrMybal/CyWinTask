using System.IO;
using System.Management;
using System.Runtime.InteropServices;
using System.Text;

namespace CyWinTask;

// CPU package temperatures require a hardware sensor provider. Windows ACPI zones
// are deliberately not presented as CPU temperature.
public sealed class CpuTemperatureSource
{
    private DateTime retryAfter;
    private double? lastValue;
    private (string Parent, string Name, double Value)[] lastSensors = [];
    public double? Read(DateTime now)
    {
        if (now < retryAfter) return lastValue;
        try
        {
            using var search = new ManagementObjectSearcher(@"root\LibreHardwareMonitor",
                "SELECT Parent, Name, Value FROM Sensor WHERE SensorType = 'Temperature'");
            using var sensors = search.Get();
            lastSensors = sensors.Cast<ManagementObject>()
                .Select(s => new
                {
                    Parent = s["Parent"]?.ToString() ?? "",
                    Name = s["Name"]?.ToString() ?? "",
                    Value = s["Value"] is IConvertible raw
                        ? Convert.ToDouble(raw, System.Globalization.CultureInfo.InvariantCulture) : double.NaN
                })
                .Where(s => double.IsFinite(s.Value) && s.Value is >= -20 and <= 150)
                .OrderBy(s => SensorPriority(s.Name))
                .Select(s => (s.Parent, s.Name, s.Value))
                .ToArray();
            var candidates = lastSensors.Where(s => IsCpuSensor(s.Parent)).ToArray();
            lastValue = candidates.Length == 0 ? null : candidates[0].Value;
            retryAfter = now.AddSeconds(5);
        }
        catch (ManagementException) { lastSensors = []; lastValue = null; retryAfter = now.AddSeconds(30); }
        catch (UnauthorizedAccessException) { lastSensors = []; lastValue = null; retryAfter = now.AddSeconds(30); }
        catch (COMException) { lastSensors = []; lastValue = null; retryAfter = now.AddSeconds(30); }
        return lastValue;
    }
    public double? ReadGpu(GpuAdapter adapter, IReadOnlyList<GpuAdapter> adapters)
    {
        string vendor = adapter.Name.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase) ? "nvidia"
            : adapter.Name.Contains("AMD", StringComparison.OrdinalIgnoreCase) || adapter.Name.Contains("Radeon", StringComparison.OrdinalIgnoreCase) ? "amd"
            : adapter.Name.Contains("Intel", StringComparison.OrdinalIgnoreCase) ? "intel" : "";
        if (vendor.Length == 0) return null;
        int matchingAdapters = adapters.Count(a => vendor switch
        {
            "nvidia" => a.Name.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase),
            "amd" => a.Name.Contains("AMD", StringComparison.OrdinalIgnoreCase) || a.Name.Contains("Radeon", StringComparison.OrdinalIgnoreCase),
            _ => a.Name.Contains("Intel", StringComparison.OrdinalIgnoreCase)
        });
        if (matchingAdapters != 1) return null;
        return SelectGpuTemperature(lastSensors, "/gpu-" + vendor + "/");
    }
    public static double? SelectGpuTemperature(IEnumerable<(string Parent, string Name, double Value)> sensors, string prefix)
    {
        var matches = sensors.Where(s => s.Parent.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)
            && (s.Name.Equals("GPU Core", StringComparison.OrdinalIgnoreCase)
                || s.Name.Equals("GPU", StringComparison.OrdinalIgnoreCase)))
            .OrderBy(s => s.Name.Equals("GPU Core", StringComparison.OrdinalIgnoreCase) ? 0 : 1)
            .ToArray();
        return matches.Select(s => s.Parent).Distinct(StringComparer.OrdinalIgnoreCase).Count() == 1
            ? matches[0].Value : null;
    }
    public static bool IsCpuSensor(string parent) => parent.StartsWith("/amdcpu/", StringComparison.OrdinalIgnoreCase)
        || parent.StartsWith("/intelcpu/", StringComparison.OrdinalIgnoreCase)
        || parent.StartsWith("/cpu/", StringComparison.OrdinalIgnoreCase);
    public static int SensorPriority(string name)
    {
        if (name.Contains("Tctl/Tdie", StringComparison.OrdinalIgnoreCase)) return 0;
        if (name.Contains("CPU Package", StringComparison.OrdinalIgnoreCase)) return 1;
        if (name.Contains("Core (Tdie)", StringComparison.OrdinalIgnoreCase)) return 2;
        return 10;
    }
}

// NVML is loaded from NVIDIA's installed driver and matched to DXGI by its
// reported device name. Ambiguous duplicate names are never guessed.
public sealed class NvidiaTemperatureSource : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Init();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Shutdown();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Count(out uint count);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Handle(uint index, out IntPtr device);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Name(IntPtr device, [Out] byte[] name, uint length);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Temperature(IntPtr device, uint sensor, out uint value);
    private readonly Dictionary<string, IntPtr> devices = new();
    private IntPtr library;
    private Shutdown? shutdown;
    private Temperature? temperature;
    public string? AdapterId => devices.Keys.FirstOrDefault();

    public NvidiaTemperatureSource(IReadOnlyList<GpuAdapter> adapters)
    {
        if (!adapters.Any(a => a.Name.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase))) return;
        var paths = new[]
        {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "nvml.dll"),
            Path.Combine(Environment.GetEnvironmentVariable("ProgramW6432") ?? "", "NVIDIA Corporation", "NVSMI", "nvml.dll")
        };
        try
        {
            foreach (string path in paths)
                if (File.Exists(path) && NativeLibrary.TryLoad(path, out library)) break;
        }
        catch (BadImageFormatException) { return; }
        if (library == IntPtr.Zero) return;
        try
        {
            var init = Export<Init>("nvmlInit_v2");
            shutdown = Export<Shutdown>("nvmlShutdown");
            var count = Export<Count>("nvmlDeviceGetCount_v2");
            var handle = Export<Handle>("nvmlDeviceGetHandleByIndex_v2");
            var getName = Export<Name>("nvmlDeviceGetName");
            temperature = Export<Temperature>("nvmlDeviceGetTemperature");
            if (init() != 0) { shutdown = null; Dispose(); return; }
            if (count(out uint countValue) != 0 || countValue > 32) return;
            var names = new List<(string Name, IntPtr Handle)>();
            for (uint index = 0; index < countValue; index++)
            {
                if (handle(index, out var device) != 0) continue;
                var buffer = new byte[256];
                if (getName(device, buffer, (uint)buffer.Length) != 0) continue;
                int end = Array.IndexOf(buffer, (byte)0);
                if (end < 0) end = buffer.Length;
                names.Add((Encoding.UTF8.GetString(buffer, 0, end).Trim(), device));
            }
            foreach (var (adapterId, nvmlIndex) in MatchAdapters(adapters, names.Select(n => n.Name).ToArray()))
                devices[adapterId] = names[nvmlIndex].Handle;
        }
        catch (EntryPointNotFoundException) { Dispose(); }
        catch (BadImageFormatException) { Dispose(); }
    }
    public static Dictionary<string, int> MatchAdapters(IReadOnlyList<GpuAdapter> adapters, IReadOnlyList<string> deviceNames)
    {
        var result = new Dictionary<string, int>();
        foreach (var adapter in adapters.Where(a => a.Name.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase)))
        {
            var matches = deviceNames.Select((name, index) => (name, index))
                .Where(x => string.Equals(x.name.Trim(), adapter.Name.Trim(), StringComparison.OrdinalIgnoreCase))
                .ToArray();
            // If DXGI lists the same model twice but NVML exposes it once, use
            // only the first DXGI entry. The other entry remains unavailable.
            var first = adapters.Where(a => string.Equals(a.Name.Trim(), adapter.Name.Trim(), StringComparison.OrdinalIgnoreCase))
                .OrderBy(a => a.Index).First();
            if (matches.Length == 1 && first.Id == adapter.Id)
                result[adapter.Id] = matches[0].index;
        }
        return result;
    }
    private T Export<T>(string name) where T : Delegate
    {
        if (!NativeLibrary.TryGetExport(library, name, out var pointer)) throw new EntryPointNotFoundException(name);
        return Marshal.GetDelegateForFunctionPointer<T>(pointer);
    }
    public double? Read(string adapterId)
    {
        if (!devices.TryGetValue(adapterId, out var device) || temperature == null) return null;
        return temperature(device, 0, out uint degrees) == 0 && degrees <= 150 ? degrees : null;
    }
    public void Dispose()
    {
        if (library == IntPtr.Zero) return;
        shutdown?.Invoke();
        NativeLibrary.Free(library);
        library = IntPtr.Zero;
        shutdown = null;
        temperature = null;
        devices.Clear();
    }
}