using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;

namespace CyWinTask;

internal static class BridgeProgram
{
    private static readonly ConcurrentQueue<string> commands = new();
    private static readonly BinaryWriter output = new(Console.OpenStandardOutput(), Encoding.UTF8, leaveOpen: true);
    private static bool performanceVisible;
    private static bool paused;
    private static int protectedUiPid;
    private static Snapshot? lastSnapshot;

    private static void Text(BinaryWriter writer, string value)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        writer.Write(bytes.Length);
        writer.Write(bytes);
    }
    private static void Frame(Action<BinaryWriter> write)
    {
        using var buffer = new MemoryStream();
        using (var writer = new BinaryWriter(buffer, Encoding.UTF8, leaveOpen: true)) write(writer);
        if (buffer.Length > 16 * 1024 * 1024) throw new InvalidOperationException("Trame trop volumineuse.");
        output.Write((int)buffer.Length);
        output.Write(buffer.GetBuffer(), 0, (int)buffer.Length);
        output.Flush();
    }
    private static void Reply(int requestId, bool success, string message) => Frame(writer =>
    {
        writer.Write((byte)2);
        writer.Write(requestId);
        writer.Write(success);
        Text(writer, message);
    });
    private static void Error(string message) => Frame(writer =>
    {
        writer.Write((byte)3);
        Text(writer, message);
    });
    private static void SnapshotFrame(Snapshot snapshot, (byte Category, string Path)[] metadata,
        Dictionary<int, ProcessGpuUsage>? gpuUsage, PerfSnapshot? performance)
    {
        Frame(writer =>
        {
            writer.Write((byte)1);
            writer.Write(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
            writer.Write(snapshot.Cpu ?? double.NaN);
            writer.Write(snapshot.UsedGb);
            writer.Write(snapshot.TotalGb);
            writer.Write(snapshot.CollectionMs);
            writer.Write(snapshot.Processes.Count);
            for (int i = 0; i < snapshot.Processes.Count; i++)
            {
                var process = snapshot.Processes[i];
                writer.Write(process.Id);
                writer.Write(process.Created);
                writer.Write(process.ParentId);
                writer.Write(process.Cpu);
                writer.Write(gpuUsage?.GetValueOrDefault(process.Id)?.Own ?? double.NaN);
                writer.Write(gpuUsage?.GetValueOrDefault(process.Id)?.Group ?? double.NaN);
                writer.Write(process.WorkingSet);
                writer.Write(process.Threads);
                writer.Write(process.Handles);
                writer.Write(metadata[i].Category);
                Text(writer, process.Name);
                Text(writer, metadata[i].Path);
            }
            writer.Write(performance?.Devices.Count ?? 0);
            if (performance == null) return;
            foreach (var device in performance.Devices)
            {
                Text(writer, device.Id);
                Text(writer, device.Title);
                Text(writer, device.Description);
                Text(writer, device.Kind);
                writer.Write(device.Metrics.Count);
                foreach (var metric in device.Metrics)
                {
                    Text(writer, metric.Id);
                    Text(writer, metric.Label);
                    Text(writer, metric.Unit);
                    writer.Write(metric.Value ?? double.NaN);
                    writer.Write(metric.Maximum ?? double.NaN);
                }
            }
        });
    }
    private static void Commands()
    {
        while (Console.ReadLine() is string line) commands.Enqueue(line);
        commands.Enqueue("Q");
    }
    private static void Handle(string line)
    {
        var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length == 0) return;
        if (parts[0] == "M" && parts.Length == 2) { performanceVisible = parts[1] == "1"; return; }
        if (parts[0] == "P" && parts.Length == 2) { paused = parts[1] == "1"; return; }
        if (parts[0] == "Q") { Environment.Exit(0); return; }
        if (parts.Length != 4 || !int.TryParse(parts[1], out int requestId)
            || !int.TryParse(parts[2], out int id) || !long.TryParse(parts[3], out long created)) return;
        var sample = lastSnapshot?.Processes.FirstOrDefault(p => p.Id == id && p.Created == created)
            ?? new ProcessSample(id, created, "", 0, 0, 0, 0, 0);
        try
        {
            if (parts[0] == "E")
            {
                if (id == protectedUiPid) throw new InvalidOperationException("CyWinTask ne peut pas s’arrêter lui-même.");
                ProcessActions.Terminate(sample);
                Reply(requestId, true, "Arrêt demandé.");
            }
            else if (parts[0] == "D") Reply(requestId, true, AdvancedReport.Read(sample));
        }
        catch (Exception ex) { Reply(requestId, false, ex.Message); }
    }
    public static int Main(string[] args)
    {
        if (args.Length > 0) int.TryParse(args[0], out protectedUiPid);
        Console.InputEncoding = Encoding.UTF8;
        _ = Task.Run(Commands);
        using var sampler = new ProcessSampler();
        var metadata = new BridgeMetadata();
        PerformanceCollector? performance = null;
        using var processGpu = new ProcessGpuCollector();
        long next = 0;
        try
        {
            while (true)
            {
                while (commands.TryDequeue(out string? line)) Handle(line);
                if (!paused && Environment.TickCount64 >= next)
                {
                    next = Environment.TickCount64 + 1000;
                    try
                    {
                        var snapshot = sampler.Collect();
                        lastSnapshot = snapshot;
                        var info = metadata.Collect(snapshot.Processes);
                        var gpuUsage = processGpu.Collect(snapshot.Processes);
                        PerfSnapshot? perf = null;
                        if (performanceVisible)
                        {
                            try
                            {
                                performance ??= new PerformanceCollector();
                                perf = performance.Collect(snapshot);
                            }
                            catch (Exception ex) { Error(ex.Message); }
                        }
                        SnapshotFrame(snapshot, info, gpuUsage, perf);
                    }
                    catch (Exception ex) { Error(ex.Message); }
                }
                Thread.Sleep(40);
            }
        }
        catch (IOException) { return 0; }
        finally { performance?.Dispose(); }
    }
}

internal static class AdvancedReport
{
    public static string Read(ProcessSample sample)
    {
        var text = new StringBuilder(ProcessActions.Describe(sample));
        text.AppendLine().AppendLine().AppendLine("COMPTEURS DU PROCESSUS");
        using var process = Process.GetProcessById(sample.Id);
        _ = process.Handle;
        if (process.StartTime.ToUniversalTime().ToFileTimeUtc() != sample.Created)
            throw new InvalidOperationException("Processus disparu ou PID réutilisé.");
        void Add(string label, Func<object?> read)
        {
            try { text.AppendLine(label + " : " + read()); }
            catch (Exception ex) { text.AppendLine(label + " : indisponible (" + ex.Message + ")"); }
        }
        Add("Session", () => process.SessionId);
        Add("Priorité", () => process.PriorityClass);
        Add("Affinité CPU", () => "0x" + process.ProcessorAffinity.ToInt64().ToString("X"));
        Add("Temps CPU total", () => process.TotalProcessorTime);
        Add("Mémoire résidente", () => $"{process.WorkingSet64 / 1048576d:N1} Mo");
        Add("Mémoire privée", () => $"{process.PrivateMemorySize64 / 1048576d:N1} Mo");
        Add("Espace virtuel", () => $"{process.VirtualMemorySize64 / 1048576d:N1} Mo");
        Add("Threads", () => process.Threads.Count);
        Add("Handles", () => process.HandleCount);
        text.AppendLine().AppendLine("MODULES CHARGÉS (200 maximum)");
        try
        {
            foreach (ProcessModule module in process.Modules.Cast<ProcessModule>().Take(200))
                text.AppendLine($"{module.ModuleName} · {module.ModuleMemorySize / 1024:N0} Ko\n  {module.FileName}");
        }
        catch (Exception ex) { text.AppendLine("Modules indisponibles : " + ex.Message); }
        return text.ToString();
    }
}
