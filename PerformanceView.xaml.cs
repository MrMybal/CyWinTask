using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace CyWinTask;
public sealed class MetricHistory
{
    public PerfMetric Metric { get; set; }
    public List<(DateTime Time, double? Value)> Points { get; } = new(64);
    public MetricHistory(PerfMetric metric) => Metric = metric;
    public void Add(DateTime time, PerfMetric metric)
    {
        Metric = metric;
        Points.RemoveAll(p => p.Time < time.AddSeconds(-60));
        if (Points.Count >= 120) Points.RemoveAt(0);
        Points.Add((time, metric.Value));
    }
}
public sealed class PerformanceResource : INotifyPropertyChanged
{
    public PerfDevice Device { get; private set; }
    public string Title => Device.Title;
    public string Subtitle => Device.Description;
    public string Summary => Device.Metrics[0].Value is double value ? $"{value:N1} {Device.Metrics[0].Unit}" : "Indisponible / mesure en cours";
    public Dictionary<string, MetricHistory> Histories { get; } = new();
    public PerformanceResource(PerfDevice device) => Device = device;
    public void Update(DateTime time, PerfDevice device)
    {
        Device = device;
        foreach (var metric in device.Metrics)
        {
            if (!Histories.TryGetValue(metric.Id, out var history)) Histories[metric.Id] = history = new(metric);
            history.Add(time, metric);
        }
        PropertyChanged?.Invoke(this, new(nameof(Summary)));
        PropertyChanged?.Invoke(this, new(nameof(Subtitle)));
    }
    public event PropertyChangedEventHandler? PropertyChanged;
}

public partial class PerformanceView : UserControl
{
    public ObservableCollection<PerformanceResource> ResourcesList { get; } = new();
    public event EventHandler? PauseRequested;
    private readonly Dictionary<string, PerformanceResource> resources = new();
    private readonly List<(HistoryChart Chart, TextBlock Value)> charts = new();
    private string layoutKey = ""; private readonly List<(MetricHistory History, TextBlock Label)> legend = new();
    public PerformanceView() { InitializeComponent(); Devices.ItemsSource = ResourcesList; }
    public void SetPaused(bool paused) { Pause.Content = paused ? "▶  Reprendre" : "Ⅱ  Pause"; if (paused) CollectionStatus.Text = "Actualisation en pause"; }
    public void ShowError(string message) => CollectionStatus.Text = message;
    public void Apply(PerfSnapshot snapshot)
    {
        var present = snapshot.Devices.Select(d => d.Id).ToHashSet();
        foreach (var id in resources.Keys.Where(id => !present.Contains(id)).ToArray())
        { ResourcesList.Remove(resources[id]); resources.Remove(id); }
        foreach (var device in snapshot.Devices)
        {
            if (!resources.TryGetValue(device.Id, out var resource))
            { resources[device.Id] = resource = new(device); ResourcesList.Add(resource); }
            resource.Update(snapshot.Time, device);
        }
        if (Devices.SelectedItem == null && ResourcesList.Count > 0) Devices.SelectedIndex = 0;
        RefreshCharts();
        CollectionStatus.Text = $"{snapshot.Time.ToLocalTime():HH:mm:ss} · Compteurs Windows {snapshot.CollectionMs:N1} ms · Historique limité à 60 s · Collecte uniquement dans cet onglet";
    }
    private void Pause_Click(object sender, RoutedEventArgs e) => PauseRequested?.Invoke(this, EventArgs.Empty);
    private void Device_Changed(object sender, SelectionChangedEventArgs e) => RefreshCharts();
    private void Cores_Changed(object sender, RoutedEventArgs e) => RefreshCharts();
    private void CpuMode_Changed(object sender, SelectionChangedEventArgs e) { if (Charts != null) RefreshCharts(); }
    private void RefreshCharts()
    {
        if (Devices.SelectedItem is not PerformanceResource resource) return;
        var device = resource.Device;
        DeviceTitle.Text = device.Title;
        DeviceDescription.Text = device.Description + (device.Kind == "gpu" ? " · " + resource.Summary : "");
        ShowCores.Visibility = device.Kind is "cpu" or "gpu" ? Visibility.Visible : Visibility.Collapsed; ShowCores.Content = device.Kind == "gpu" ? "Afficher les autres moteurs GPU" : "Afficher les processeurs logiques";
        var mainEngines = new[] { "engine:3D", "engine:Copy", "engine:VideoEncode", "engine:VideoDecode", "dedicated", "shared" };
        var visible = device.Metrics.Where(m => (device.Kind != "cpu" || !m.Id.StartsWith("core:") || ShowCores.IsChecked == true) && (device.Kind != "gpu" || (m.Id != "usage" && (mainEngines.Contains(m.Id) || ShowCores.IsChecked == true)))).ToArray();
        bool compact = device.Kind == "cpu" && ShowCores.IsChecked == true && CpuMode.SelectedIndex == 0;
        CpuModePanel.Visibility = device.Kind == "cpu" ? Visibility.Visible : Visibility.Collapsed;
        if (compact && visible.Length > 1) visible = visible.Where(m => m.Id.StartsWith("core:")).ToArray();
        if (device.Kind == "cpu") DeviceDescription.Text = device.Description + " · " + resource.Summary;
        string key = compact + ":" + device.Id + ":" + string.Join(',', visible.Select(m => m.Id));
        if (key != layoutKey)
        {
            layoutKey = key; charts.Clear(); Charts.Children.Clear(); legend.Clear(); CombinedLegend.Children.Clear();
            Charts.Columns = compact ? Math.Clamp((int)Math.Ceiling(Math.Sqrt(visible.Length * 1.5)), 2, 8) : device.Kind == "gpu" || visible.Length > 3 ? 2 : 1;
            Brush accent = device.Kind switch { "memory" => Brushes.Plum, "disk" => Brushes.YellowGreen, "network" => Brushes.SandyBrown, _ => Brushes.LightSkyBlue };
            if (device.Kind == "disk")
            {
                var colors = new Brush[] { Brushes.YellowGreen, Brushes.DeepSkyBlue, Brushes.SandyBrown };
                var combined = new HistoryChart(resource.Histories[visible[0].Id]) { Accent = colors[0], Height = 330 };
                for (int i = 0; i < visible.Length; i++)
                {
                    var history = resource.Histories[visible[i].Id];
                    if (i > 0) combined.AdditionalSeries.Add((history, colors[i]));
                    var label = new TextBlock { Foreground = colors[i], Margin = new Thickness(0,0,24,8) }; CombinedLegend.Children.Add(label); legend.Add((history,label));
                }
                Charts.Children.Add(combined); charts.Add((combined,new TextBlock()));
            }
            else foreach (var metric in visible)
            {
                if (!resource.Histories.TryGetValue(metric.Id, out var history)) continue;
                var container = new StackPanel { Margin = new Thickness(0, 0, 12, 18) };
                var header = new DockPanel { Margin = new Thickness(0, 0, 0, 7) };
                var value = new TextBlock { HorizontalAlignment = HorizontalAlignment.Right, Foreground = accent, FontSize = compact ? 10 : 13 };
                DockPanel.SetDock(value, Dock.Right); header.Children.Add(value);
                header.Children.Add(new TextBlock { Text = compact ? metric.Label.Replace("Processeur logique ", "CPU ") : metric.Label, FontSize = compact ? 10 : 13, TextTrimming = TextTrimming.CharacterEllipsis, Margin = new Thickness(0, 0, 10, 0) });
                var chart = new HistoryChart(history) { Accent = accent, Height = compact ? Math.Clamp(340d / Math.Ceiling(visible.Length / (double)Charts.Columns) - 27, 32, 80) : device.Kind == "gpu" ? 100 : device.Kind == "memory" ? 125 : visible.Length > 3 ? 95 : 165 };
                container.Children.Add(header); container.Children.Add(chart);
                Charts.Children.Add(container); charts.Add((chart, value));
            }
        }
        foreach (var (chart, value) in charts)
        {
            var metric = chart.History.Metric;
            value.Text = metric.Value is double number ? $"{number:N1} {metric.Unit}" : "Indisponible";
            chart.InvalidateVisual();
        }
        foreach (var (history,label) in legend) label.Text = $"● {history.Metric.Label} : " + (history.Metric.Value is double v ? $"{v:N1} {history.Metric.Unit}" : "Indisponible");
        DeviceNote.Text = device.Kind switch
        {
            "gpu" => "Utilisation : moteur le plus actif. Les moteurs dépendent du pilote ; un compteur absent reste indisponible. Température et version du pilote : non collectées.",
            "network" => "Débits instantanés. Échelle automatique de chaque graphique, en Mbit/s. Les interfaces virtuelles sont également affichées.",
            "disk" => "Courbes superposées : activité (vert), lecture (bleu), écriture (orange). Gauche : 0–100 %. Droite : Mo/s, échelle automatique commune aux deux débits.",
            "cpu" => "Temps processeur mesuré par Windows. Les valeurs peuvent différer de la mesure de fréquence/utilisation du gestionnaire Windows.",
            _ => "Mémoire virtuelle engagée : allocations garanties par Windows, adossées à la RAM ou aux fichiers de pagination. Le plafond indique la limite actuelle. Cette mesure ne représente pas les données effectivement écrites dans le fichier de pagination."
        };
    }
}
