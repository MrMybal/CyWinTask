using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using CyWinTask;

internal static class UiChecks
{
    [STAThread]
    public static int Run()
    {
        var app = new App();
        app.InitializeComponent();
        var window = new MainWindow { Width = 1280, Height = 800 };
        // Pause before Loaded so the preview never samples the host computer.
        ((Button)window.FindName("PauseButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        window.Show();
        ((TextBlock)window.FindName("Machine")).Text = "PC-DÉMONSTRATION";
        ((TextBlock)window.FindName("Cores")).Text = "16 processeurs logiques";
        ((TextBlock)window.FindName("CpuTotal")).Text = "38 %";
        ((TextBlock)window.FindName("MemoryTotal")).Text = "42 %";
        ((TextBlock)window.FindName("ProcessTotal")).Text = "6";
        ((TextBlock)window.FindName("Status")).Text = "1 s · Démonstration";
        var grid = (DataGrid)window.FindName("ProcessGrid");
        var view = (ICollectionView)grid.ItemsSource;
        var rows = (ObservableCollection<ProcessRow>)view.SourceCollection;
        string[] names = ["Atelier.exe", "Navigateur.exe", "Editeur.exe", "ServiceAudio.exe", "Synchronisation.exe", "Terminal.exe"];
        for (int i = 0; i < names.Length; i++)
        {
            var row = new ProcessRow(new ProcessSample(1000 + i, i + 1, names[i], 0,
                i == 0 ? 29.3 : 7.5 - i, (long)((i == 0 ? 3400 : 240 + 70 * i) * 1048576d), 12 + i * 4, 230 + i * 110));
            row.SetMemoryCapacity(16d * 1073741824d);
            rows.Add(row);
        }
        grid.SelectedItem = rows[0];
        window.UpdateLayout();
        if (window.Background is not SolidColorBrush background || background.Color != Color.FromRgb(13, 18, 24))
            throw new Exception("Palette navy absente");
        if (grid.SelectedItem != rows[0])
            throw new Exception("Sélection perdue");
        if (((Button)window.FindName("InspectButton")).IsEnabled != true)
            throw new Exception("Action Inspecter inaccessible");
        if (Environment.GetEnvironmentVariable("CYWINTASK_UI_PREVIEW") is { Length: > 0 } previewPath)
        {
            var image = new RenderTargetBitmap(1280, 800, 96, 96, PixelFormats.Pbgra32);
            image.Render(window);
            var encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create(image));
            using var output = File.Create(previewPath);
            encoder.Save(output);
        }
        var filter = (ComboBox)window.FindName("CategoryFilter");
        filter.IsDropDownOpen = true;
        window.UpdateLayout();
        if (filter.Template.FindName("PART_Popup", filter) is not Popup { IsOpen: true })
            throw new Exception("Filtre impossible à ouvrir");
        filter.SelectedIndex = 1;
        if (filter.SelectedIndex != 1)
            throw new Exception("Filtre impossible à sélectionner");
        filter.IsDropDownOpen = false;
        window.Close();
        app.Shutdown();
        Console.WriteLine("PASS Palette, sélection et actions sur des processus fictifs");
        return 0;
    }
}
