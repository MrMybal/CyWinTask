using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Automation.Provider;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using CyWinTask;

internal static class SortingChecks
{
    static void Check(bool condition, string message) { if (!condition) throw new Exception(message); Console.WriteLine("PASS " + message); }
    static IEnumerable<T> Find<T>(DependencyObject parent) where T : DependencyObject
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is T match) yield return match;
            foreach (var descendant in Find<T>(child)) yield return descendant;
        }
    }
    static void Verify(DataGrid grid, DataGridColumn column)
    {
        var property = typeof(ProcessRow).GetProperty(column.SortMemberPath)!;
        int sign = column.SortDirection == ListSortDirection.Ascending ? 1 : -1;
        var view = (ICollectionView)grid.ItemsSource;
        foreach (var itemsInGroup in view.Groups == null ? new[] { view.Cast<ProcessRow>().ToArray() } : view.Groups.Cast<CollectionViewGroup>().Select(g => g.Items.Cast<ProcessRow>().ToArray()))
        {
            var items = itemsInGroup;
            for (int i = 1; i < items.Length; i++)
            {
                var left = (IComparable)property.GetValue(items[i - 1])!;
                var right = property.GetValue(items[i]);
                CheckOrder(sign * left.CompareTo(right) <= 0, column.SortMemberPath);
            }
        }
    }
    static void CheckOrder(bool condition, string property) { if (!condition) throw new Exception("Incorrect sort: " + property); }
    public static int Run()
    {
        var app = new App(); app.InitializeComponent();
        var window = new MainWindow(); Exception? failure = null;
        window.Loaded += async (_, _) =>
        {
            try
            {
                var grid = (DataGrid)window.FindName("ProcessGrid");
                for (int i = 0; i < 40 && grid.Items.Count < 10; i++) await Task.Delay(250);
                Check(grid.Items.Count > 10, "Processus disponibles");
                ((Button)window.FindName("PauseButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                await Task.Delay(500); await window.Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ContextIdle);
                var selected = grid.Items.Cast<ProcessRow>().First(r => r.Id == Environment.ProcessId);
                grid.SelectedItems.Add(selected);
                foreach (var column in grid.Columns)
                {
                    for (int click = 0; click < 2; click++)
                    {
                        var header = Find<DataGridColumnHeader>(grid).Single(h => h.Column == column);
                        var peer = new DataGridColumnHeaderAutomationPeer(header);
                        typeof(DataGridColumnHeader).GetMethod("OnClick", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.Invoke(header, null);
                        await Task.Delay(100);
                        Verify(grid, column);
                        Check(Find<TextBlock>(header).Any(t => t.Text == (column.SortDirection == ListSortDirection.Ascending ? "▲" : "▼")), "Fleche " + column.Header + " " + column.SortDirection);
                        Check(grid.SelectedItems.Contains(selected), "Selection conservee");
                    }
                }
                var cpu = grid.Columns.Single(c => c.SortMemberPath == "Cpu");
                var cpuHeader = Find<DataGridColumnHeader>(grid).Single(h => h.Column == cpu);
                typeof(DataGridColumnHeader).GetMethod("OnClick", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.Invoke(cpuHeader, null);
                await Task.Delay(100);
                selected.Update(selected.Sample with { Cpu = 99.9 });
                await Task.Delay(100);
                Verify(grid, cpu);
                Check(grid.SelectedItems.Contains(selected), "Tri dynamique apres changement CPU");
                var search = (TextBox)window.FindName("Search"); search.Text = "exe";
                await Task.Delay(500); await window.Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ContextIdle); Verify(grid, cpu);
                search.Clear(); await Task.Delay(500); await window.Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ContextIdle); Verify(grid, cpu);
                Check(true, "Tri conserve apres recherche");
                ((Button)window.FindName("PauseButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                await Task.Delay(1500); Verify(grid, cpu);
                Check(grid.SelectedItems.Contains(selected), "Tri et selection apres actualisation reelle");
                ((Button)window.FindName("PauseButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                await window.Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ContextIdle);
                var grouping = (CheckBox)window.FindName("GroupProcesses");
                var category = (ComboBox)window.FindName("CategoryFilter");
                Check(grouping.IsChecked != true && ((ICollectionView)grid.ItemsSource).Groups == null, "Liste sans groupes par defaut");
                foreach (bool grouped in new[] { true, false })
                {
                    grouping.IsChecked = grouped;
                    await Task.Delay(100);
                    Verify(grid, cpu);
                    Check(grid.SelectedItems.Contains(selected), "Selection conservee au changement de regroupement");
                    foreach (int index in new[] { 1, 2, 3, 0 })
                    {
                        category.SelectedIndex = index;
                        await Task.Delay(150);
                        var expected = (string)((ComboBoxItem)category.SelectedItem).Tag;
                        Check(grid.Items.Count > 0 && grid.Items.Cast<ProcessRow>().All(r => expected == "" || r.Category == expected), "Filtre " + index + " groupes=" + grouped);
                        Verify(grid, cpu);
                    }
                    grid.SelectedItems.Add(selected);
                }
                category.SelectedIndex = 1;
                var filterSearch = (TextBox)window.FindName("Search"); filterSearch.Text = "NO_MATCH_987654321";
                await Task.Delay(500); await window.Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ContextIdle);
                Check(grid.Items.Count == 0, "Recherche combinee au filtre par type");
                filterSearch.Clear(); category.SelectedIndex = 0;
                await Task.Delay(500); await window.Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ContextIdle);
                var bitmap = new RenderTargetBitmap((int)window.ActualWidth, (int)window.ActualHeight, 96, 96, PixelFormats.Pbgra32); bitmap.Render(window);
                var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var stream = File.Create("preview-sorting.png"); encoder.Save(stream);
            }
            catch (Exception ex) { failure = ex; }
            finally { window.Close(); app.Shutdown(); }
        };
        app.Run(window);
        if (failure != null) { Console.Error.WriteLine(failure); return 1; }
        return 0;
    }
}
