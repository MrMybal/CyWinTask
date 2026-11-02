using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.IO;
using CyWinTask;

internal static class GraphChecks
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
        Console.WriteLine("PASS " + message);
    }
    private static IEnumerable<T> Descendants<T>(DependencyObject parent) where T : DependencyObject
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is T typed) yield return typed;
            foreach (var nested in Descendants<T>(child)) yield return nested;
        }
    }
    private static PerfSnapshot FakeSnapshot(int step) => new(DateTime.UtcNow.AddSeconds(step), new()
    {
        new("cpu", "Processeur", "Processeur de démonstration", "cpu", new()
        {
            new("cpu", "Utilisation totale", 42d + step, "%", 100),
            new("temperature", "Température CPU", 54d + step, "°C")
        }),
        new("memory", "Mémoire", "12,3 / 32 Go", "memory", new()
        {
            new("used", "Mémoire physique utilisée", 12.3 + step / 10d, "Go", 32),
            new("available", "Mémoire disponible", 19.7 - step / 10d, "Go", 32)
        }),
        new("gpu-a", "GPU 0", "Carte graphique fictive A", "gpu", new()
        {
            new("usage", "Utilisation", 76d - step, "%", 100),
            new("temperature", "Température GPU", 68d + step, "°C")
        }),
        new("gpu-b", "GPU 1", "Carte graphique fictive B", "gpu", new()
        {
            new("usage", "Utilisation", 3d + step, "%", 100),
            new("temperature", "Température GPU", null, "°C")
        })
    }, 0);
    public static int Run()
    {
        Check(CpuTemperatureSource.IsCpuSensor("/amdcpu/0") && !CpuTemperatureSource.IsCpuSensor("/lpc/0"), "Seuls les capteurs CPU sont retenus");
        var fakeGpuSensors = new (string Parent, string Name, double Value)[]
        {
            ("/gpu-nvidia/0", "GPU Core", 63),
            ("/gpu-nvidia/0", "GPU Hot Spot", 79),
            ("/gpu-amd/0", "GPU Core", 52)
        };
        Check(CpuTemperatureSource.SelectGpuTemperature(fakeGpuSensors, "/gpu-nvidia/") == 63
            && CpuTemperatureSource.SelectGpuTemperature(fakeGpuSensors, "/gpu-amd/") == 52,
            "Capteur GPU principal sélectionné par fabricant");
        var duplicateAdapters = new[]
        {
            new GpuAdapter(0, "gpu-first", "NVIDIA Fictional GPU", 8ul * 1073741824, 0),
            new GpuAdapter(1, "gpu-second", "NVIDIA Fictional GPU", 8ul * 1073741824, 0)
        };
        var mapping = NvidiaTemperatureSource.MatchAdapters(duplicateAdapters, ["NVIDIA Fictional GPU"]);
        Check(mapping.Count == 1 && mapping.ContainsKey("gpu-first"), "Doublon DXGI laissé sans attribution incertaine");
        var app = new App(); app.InitializeComponent();
        var page = new PerformanceView();
        var host = new Window { Content = page, Width = 1050, Height = 800, Title = "Vérification graphique",
            Background = (Brush)app.FindResource("CyBg1") };
        try
        {
            host.Show();
            page.Apply(FakeSnapshot(0));
            page.ShowGraphMode(true);
            host.UpdateLayout();
            var dashboard = (GraphDashboard)page.FindName("Dashboard");
            Check(page.IsGraphMode && dashboard.Visibility == Visibility.Visible, "Vue dédiée visible");
            Check(Descendants<HistoryChart>(dashboard).Count() == 4, "CPU, RAM et deux GPU affichés");
            Check(Descendants<TextBlock>(dashboard).Any(t => t.Text == "Température : 54 °C")
                && Descendants<TextBlock>(dashboard).Any(t => t.Text == "Température : indisponible"), "Températures présentes et absentes distinguées");
            if (Environment.GetEnvironmentVariable("CYWINTASK_UI_PREVIEW") is { Length: > 0 } previewPath)
            {
                host.UpdateLayout();
                var image = new RenderTargetBitmap((int)host.ActualWidth, (int)host.ActualHeight, 96, 96, PixelFormats.Pbgra32);
                image.Render(host);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(image));
                using var output = File.Create(previewPath);
                encoder.Save(output);
            }
            page.OpenDashboard();
            Check(page.HasDetachedWindow && app.Windows.Count == 2, "Fenêtre séparée ouverte");
            host.WindowState = WindowState.Minimized;
            var detached = app.Windows.Cast<Window>().Single(w => w != host);
            Check(detached.WindowState == WindowState.Normal, "Fenêtre indépendante même si la fenêtre principale est réduite");
            host.WindowState = WindowState.Normal;
            detached.UpdateLayout();
            var detachedBoard = Descendants<GraphDashboard>(detached).Single();
            Check(Descendants<HistoryChart>(detachedBoard).Count() == 4, "Courbes dans la fenêtre séparée");
            var cpuHistory = page.ResourcesList.Single(r => r.Device.Id == "cpu").Histories["cpu"];
            var detachedCpu = Descendants<HistoryChart>(detachedBoard).Single(c => c.History.Metric.Id == "cpu");
            Check(ReferenceEquals(cpuHistory, detachedCpu.History), "Historique partagé sans nouvelle collecte");
            page.Apply(FakeSnapshot(1));
            Check(detachedCpu.History.Points.Count == 2 && detachedCpu.History.Metric.Value == 43, "Mise à jour de la fenêtre séparée");
            Check(Descendants<TextBlock>(detachedBoard).Any(t => t.Text == "Température : 55 °C"), "Température mise à jour dans la fenêtre séparée");
            page.OpenDashboard("cpu");
            Check(app.Windows.Count == 3 && app.Windows.Cast<Window>().Any(w => w != host && w != detached && Descendants<HistoryChart>(w).Count() == 1), "Graphique individuel détaché");
            page.ShowGraphMode(false);
            Check(!page.IsGraphMode && dashboard.Visibility == Visibility.Collapsed, "Retour à la vue détaillée");
            if (Environment.GetEnvironmentVariable("CYWINTASK_UI_PREVIEW") is { Length: > 0 } detailedPreview)
            {
                host.UpdateLayout();
                var image = new RenderTargetBitmap((int)host.ActualWidth, (int)host.ActualHeight, 96, 96, PixelFormats.Pbgra32);
                image.Render(host);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(image));
                using var output = File.Create(Path.ChangeExtension(detailedPreview, ".detailed.png"));
                encoder.Save(output);
            }
            page.CloseDetachedWindows();
            Check(!page.HasDetachedWindow && app.Windows.Count == 1, "Fermeture des fenêtres séparées");
            return 0;
        }
        finally { host.Close(); app.Shutdown(); }
    }
}
