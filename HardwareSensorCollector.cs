using LibreHardwareMonitor.Hardware;

namespace CyWinTask;

// Hardware reads run off the one-second process and PDH collection path.
// The last completed sample is copied into the bridge snapshot without waiting for sensors.
public sealed class HardwareSensorCollector : IDisposable
{
    private readonly CancellationTokenSource stop = new();
    private readonly Task worker;
    private readonly object gate = new();
    private List<PerfDevice> latest = [];
    public string? Error { get; private set; }

    public HardwareSensorCollector()
    {
        worker = Task.Run(Poll);
    }

    public List<PerfDevice> Snapshot()
    {
        lock (gate) return latest;
    }

    private static string Unit(SensorType type) => type switch
    {
        SensorType.Temperature => "°C",
        SensorType.Fan => "RPM",
        SensorType.Voltage => "V",
        SensorType.Power => "W",
        SensorType.Clock => "MHz",
        SensorType.Load => "%",
        _ => ""
    };

    private static void ReadHardware(IHardware hardware, List<PerfDevice> devices)
    {
        try { hardware.Update(); }
        catch { return; }
        var metrics = new List<PerfMetric>();
        foreach (var sensor in hardware.Sensors)
        {
            if (sensor.SensorType is not (SensorType.Temperature or SensorType.Fan
                or SensorType.Voltage or SensorType.Power or SensorType.Clock or SensorType.Load)
                || sensor.Value is not float value || !float.IsFinite(value)) continue;
            metrics.Add(new(sensor.Identifier.ToString(), sensor.Name, value,
                Unit(sensor.SensorType), sensor.SensorType == SensorType.Load ? 100 : null));
        }
        if (metrics.Count != 0)
            devices.Add(new("sensor:" + hardware.Identifier, hardware.Name,
                hardware.HardwareType.ToString(), "sensor", metrics));
        foreach (var child in hardware.SubHardware) ReadHardware(child, devices);
    }

    private async Task Poll()
    {
        try
        {
            var computer = new Computer
            {
                IsCpuEnabled = true,
                IsGpuEnabled = true,
                IsMotherboardEnabled = true,
                IsMemoryEnabled = true,
                IsStorageEnabled = true
            };
            try
            {
                computer.Open();
                while (!stop.IsCancellationRequested)
                {
                    var devices = new List<PerfDevice>();
                    foreach (var hardware in computer.Hardware)
                        ReadHardware(hardware, devices);
                    lock (gate) latest = devices;
                    await Task.Delay(TimeSpan.FromSeconds(2), stop.Token);
                }
            }
            finally { computer.Close(); }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { Error = ex.GetType().Name; }
    }

    public void Dispose()
    {
        stop.Cancel();
        try { worker.Wait(TimeSpan.FromSeconds(3)); }
        catch (AggregateException) { }
        stop.Dispose();
    }
}
