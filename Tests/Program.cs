using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using CyWinTask;

internal static class Program
{
    static void Check(bool condition, string message) { if (!condition) throw new Exception(message); Console.WriteLine("PASS " + message); }
    static IEnumerable<T> FindVisual<T>(DependencyObject root) where T : DependencyObject
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(root); i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is T match) yield return match;
            foreach (var descendant in FindVisual<T>(child)) yield return descendant;
        }
    }
    static void Capture(Window window, string path)
    {
        window.UpdateLayout();
        var bitmap = new RenderTargetBitmap((int)window.ActualWidth, (int)window.ActualHeight, 96, 96, PixelFormats.Pbgra32); bitmap.Render(window);
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var stream = File.Create(path); encoder.Save(stream);
    }    [STAThread] static int Main(string[] args)
    {
        if (args.Contains("--context")) return ContextChecks.Run();
        if (args.Contains("--tree")) return TreeChecks.Run();
        if (args.Contains("--sorting")) return SortingChecks.Run();
        if (args.Contains("--child")) { Thread.Sleep(30000); return 0; }
        try
        {
            using var sampler = new ProcessSampler();
            var first = sampler.Collect();
            using var self = Process.GetCurrentProcess();
            var sample = first.Processes.Single(p => p.Id == Environment.ProcessId);
            Check(sample.Created == self.StartTime.ToUniversalTime().ToFileTimeUtc(), "Identité PID + date de création");
            Check(sample.WorkingSet > 0 && sample.Threads > 0 && sample.Handles > 0, "Compteurs système cohérents");
            Check(first.MemoryPercent <= 100 && first.TotalGb > 0, "Mémoire physique");
            Check(ProcessActions.Describe(sample).Contains("Exécutable"), "Inspection à la demande");
            Check(MainWindow.Matches(sample, sample.Name.ToUpperInvariant()) && MainWindow.Matches(sample, sample.Id.ToString()) && !MainWindow.Matches(sample, "NO_MATCH_987654321"), "Filtre nom et PID");
            bool protectedSelf = false;
            try { ProcessActions.Terminate(sample); } catch (InvalidOperationException) { protectedSelf = true; }
            Check(protectedSelf, "Protection de CyWinTask");
            bool staleRejected = false;
            try { ProcessActions.Describe(sample with { Created = 1 }); } catch (InvalidOperationException) { staleRejected = true; }
            Check(staleRejected, "Identité périmée refusée");
            using (var child = Process.Start(new ProcessStartInfo(Environment.ProcessPath!, "--child") { UseShellExecute = false, CreateNoWindow = true })!)
            {
                try
                {
                    Thread.Sleep(200);
                    var target = sampler.Collect().Processes.Single(p => p.Id == child.Id);
                    ProcessActions.Terminate(target);
                    Check(child.WaitForExit(5000), "Arrêt réel d’un processus de test appartenant au test");
                }
                finally { if (!child.HasExited) child.Kill(); }
            }
            var times = new List<double>();
            for (int i = 0; i < 20; i++)
            {
                Thread.Sleep(100);
                var result = sampler.Collect(); times.Add(result.CollectionMs);
                Check(result.Cpu is >= 0 and <= 100, "Échantillon CPU valide");
            }
            var aggregated = PerformanceCollector.AggregateEngines(new()
            {
                ["pid_1_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D"] = 20,
                ["pid_2_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D"] = 30,
                ["pid_3_luid_0x00000000_0x00000001_phys_0_eng_1_engtype_Copy"] = 40
            });
            Check(aggregated.Values.Max() == 50, "GPU : somme par moteur, maximum entre moteurs");
            var history = new MetricHistory(new("test", "Test", 0, "%", 100));
            var start = DateTime.UtcNow;
            for (int i = 0; i < 150; i++) history.Add(start.AddSeconds(i), new("test", "Test", i, "%", 100));
            Check(history.Points.Count == 61, "Historique borne a 60 secondes");
            using (var performance = new PerformanceCollector())
            {
                PerfSnapshot? perf = null;
                for (int i = 0; i < 3; i++) { Thread.Sleep(1000); perf = performance.Collect(sampler.Collect()); }
                Check(perf!.Devices.Single(d => d.Id == "cpu").Metrics[0].Value is >= 0 and <= 100, "Compteur CPU PDH");
                Check(perf.Devices.Any(d => d.Kind == "disk"), "Disques physiques detectes");
                Check(perf.Devices.Any(d => d.Kind == "network"), "Interfaces reseau detectees");
                Check(perf.Devices.Any(d => d.Kind == "gpu" && d.Metrics[0].Value.HasValue), "GPU detecte et moteurs mesurables sur ce poste");
                File.WriteAllText("performance-diagnostics.json", JsonSerializer.Serialize(perf, new JsonSerializerOptions { WriteIndented = true }));
                Console.WriteLine("Performance collection: " + perf.CollectionMs + " ms; devices: " + perf.Devices.Count);
            }
            var committed = CommitMemory.Read(); Check(committed.Value > 0 && committed.Maximum >= committed.Value, "Memoire virtuelle engagee et limite reelles");
            var barRow = new ProcessRow(sample with { WorkingSet = 1073741824 }); barRow.SetMemoryCapacity(10737418240); Check(barRow.MemoryPercent == 10, "Barre RAM rapportee a la memoire physique totale");
            Check(UsageCell.Heat(50,false) != UsageCell.Heat(1,false), "Couleurs de charge distinctes");
            var app = new App(); app.InitializeComponent();
            var window = new MainWindow();
            Exception? uiError = null;
            window.Loaded += async (_, _) =>
            {
                try
                {
                    await Task.Delay(2000);
                    var grid = (DataGrid)window.FindName("ProcessGrid");
                    var search = (TextBox)window.FindName("Search");
                    Check(grid.Columns.Count == 6 && grid.Columns.All(c => c.ActualWidth >= 70), "Toutes les colonnes restent visibles avec les groupes");
                    Check(grid.Items.Count > 10, "Interface alimentée par de vrais processus");
                    var a = grid.Items.Cast<ProcessRow>().Single(r => r.Id == Environment.ProcessId);
                    var b = grid.Items.Cast<ProcessRow>().First(r => r.Id == 4);
                    grid.SelectedItems.Add(a); grid.SelectedItems.Add(b);
                    await Task.Delay(1500);
                    Check(grid.SelectedItems.Contains(a) && grid.SelectedItems.Contains(b), "Multisélection conservée après actualisation");
                    search.Text = "NO_MATCH_987654321";
                    await Task.Delay(300);
                    Check(grid.Items.Count == 0, "État vide de la recherche");
                    search.Text = Environment.ProcessId.ToString();
                    await Task.Delay(300);
                    Check(grid.Items.Cast<ProcessRow>().Any(r => r.Id == Environment.ProcessId), "Recherche PID dans l’interface");
                    search.Clear();
                    await Task.Delay(300);
                    self.Refresh(); double startCpu = self.TotalProcessorTime.TotalMilliseconds;
                    var elapsed = Stopwatch.StartNew();
                    await Task.Delay(6000);
                    self.Refresh();
                    double cpu = (self.TotalProcessorTime.TotalMilliseconds - startCpu) / elapsed.Elapsed.TotalMilliseconds / Environment.ProcessorCount * 100;
                    var report = new { date = DateTimeOffset.Now, processCount = grid.Items.Count, collectorMedianMs = times.Order().ElementAt(times.Count / 2), collectorMaxMs = times.Max(), uiCpuPercentTotalCapacity = cpu, workingSetMb = self.WorkingSet64 / 1048576d, logicalProcessors = Environment.ProcessorCount, sampleSeconds = elapsed.Elapsed.TotalSeconds, note = "Mesure locale, sans comparaison au gestionnaire Windows. CPU normalisé sur tous les processeurs logiques. Inclut le banc de test." };
                    File.WriteAllText("diagnostics.json", JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
                    var bitmap = new RenderTargetBitmap((int)window.ActualWidth, (int)window.ActualHeight, 96, 96, PixelFormats.Pbgra32);
                    bitmap.Render(window);
                    var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
                    using (var stream = File.Create("preview.png")) encoder.Save(stream);
                    Check(FindVisual<UsageCell>(grid).Any(c => c.IsMemory && c.Label.Contains("%")), "Barres RAM et pourcentages dans le tableau");
                    Check(grid.Items.Cast<ProcessRow>().Any(r => r.Category == "Applications"), "Groupe Applications");
                    Check(grid.Items.Cast<ProcessRow>().Any(r => r.Category == "Processus Windows"), "Groupe Windows");
                    Check(grid.Items.Cast<ProcessRow>().Any(r => r.Icon != null), "Icones chargees");
                    window.ShowPerformance(true);
                    await Task.Delay(5500);
                    var performancePage = (PerformanceView)window.FindName("PerformancePage");
                    Check(performancePage.ResourcesList.Count >= 4, "Navigation Performance et ressources reelles");
                    var devices = (ListBox)performancePage.FindName("Devices");
                    var coresCheck = (CheckBox)performancePage.FindName("ShowCores");
                    var mode = (ComboBox)performancePage.FindName("CpuMode");
                    devices.SelectedItem = performancePage.ResourcesList.First(d => d.Device.Kind == "cpu");
                    coresCheck.IsChecked = true; mode.SelectedIndex = 0;
                    await Task.Delay(500); window.UpdateLayout();
                    var chartPanel = (System.Windows.Controls.Primitives.UniformGrid)performancePage.FindName("Charts");
                    var compactCharts = FindVisual<HistoryChart>(performancePage).ToArray();
                    Check(chartPanel.Columns >= 3 && compactCharts.Length == Environment.ProcessorCount && compactCharts.All(c => c.Height <= 80), "CPU compact : tous les processeurs logiques");
                    Capture(window,"preview-cpu-compact.png");
                    mode.SelectedIndex = 1; await Task.Delay(200); window.UpdateLayout();
                    Check(chartPanel.Columns == 2 && FindVisual<HistoryChart>(performancePage).Count() == Environment.ProcessorCount + 1, "CPU detaille avec charge totale");
                    Capture(window,"preview-cpu-detailed.png");
                    devices.SelectedItem = performancePage.ResourcesList.First(d => d.Device.Kind == "memory");
                    await Task.Delay(200); window.UpdateLayout();
                    Check(FindVisual<HistoryChart>(performancePage).Count() == 3 && FindVisual<HistoryChart>(performancePage).Any(c => c.History.Metric.Id == "commit" && c.History.Metric.Value > 0), "Trois graphiques memoire avec engagement");
                    Capture(window,"preview-memory.png");
                    devices.SelectedItem = performancePage.ResourcesList.First(d => d.Device.Kind == "disk");
                    await Task.Delay(200); window.UpdateLayout();
                    var diskChart = FindVisual<HistoryChart>(performancePage).Single();
                    Check(diskChart.Series.Count() == 3 && diskChart.Series.Select(s => s.Color).Distinct().Count() == 3 && diskChart.Scale("%") == 100, "Disque : trois courbes colorees et echelle 100 pourcent");
                    Check(diskChart.Scale("Mo/s") >= 1, "Echelle de debit commune lecture/ecriture");
                    Capture(window,"preview-disk.png");
                    devices.SelectedItem = performancePage.ResourcesList.First(d => d.Device.Kind == "gpu");
                    await Task.Delay(1500);
                    var gpuBitmap = new RenderTargetBitmap((int)window.ActualWidth, (int)window.ActualHeight, 96, 96, PixelFormats.Pbgra32);
                    gpuBitmap.Render(window);
                    var gpuEncoder = new PngBitmapEncoder(); gpuEncoder.Frames.Add(BitmapFrame.Create(gpuBitmap));
                    using (var stream = File.Create("preview-performance.png")) gpuEncoder.Save(stream);
                    window.ShowPerformance(false);
                    await Task.Delay(1200);
                    Check(grid.SelectedItems.Count >= 0 && grid.Items.Count > 0, "Retour a la vue Processus");
                    Console.WriteLine(JsonSerializer.Serialize(report));
                }
                catch (Exception ex) { uiError = ex; }
                finally { window.Close(); app.Shutdown(); }
            };
            app.Run(window);
            if (uiError != null) throw uiError;
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
