using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Threading;

namespace CyWinTask;

public sealed class ProcessRow : INotifyPropertyChanged
{
    public ProcessSample Sample { get; private set; }
    public int Id => Sample.Id;
    public string Name => Sample.Name;
    public double Cpu => Sample.Cpu;
    public double MemoryMb => Math.Round(Sample.WorkingSet / 1048576d, 1);
    private double totalMemoryBytes;
    public double MemoryPercent => totalMemoryBytes > 0 ? Math.Clamp(Sample.WorkingSet / totalMemoryBytes * 100, 0, 100) : 0;
    public string MemoryLabel => $"{MemoryMb:N1} Mo · {MemoryPercent:N1} %";
    public void SetMemoryCapacity(double bytes) { if (totalMemoryBytes == bytes) return; totalMemoryBytes = bytes; Changed(nameof(MemoryPercent)); Changed(nameof(MemoryLabel)); }
    public int Threads => Sample.Threads;
    public int Handles => Sample.Handles;
    public System.Windows.Media.ImageSource? Icon { get; private set; }
    public string Category { get; private set; } = "Processus en arrière-plan";
    public void SetMetadata(ProcessMetadata info) { if (Icon != info.Icon) { Icon = info.Icon; Changed(nameof(Icon)); } if (Category != info.Category) { Category = info.Category; Changed(nameof(Category)); } }
    private static readonly System.Windows.Media.Brush MemberFill = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(32,46,55));
    private static readonly System.Windows.Media.Brush RootFill = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(48,70,81));
    public System.Windows.Media.Brush BranchBackground => InBranch ? IsBranchRoot ? RootFill : MemberFill : System.Windows.Media.Brushes.Transparent;
    public Thickness BranchBorder { get; private set; }
    public bool InBranch { get; private set; }
    public bool IsBranchRoot { get; private set; }
    public void SetBranchFrame(bool member, bool first, bool last)
    {
        var border = member ? new Thickness(3, first ? 1 : 0, 1, last ? 1 : 0) : new Thickness(0);
        if (BranchBorder != border) { BranchBorder = border; Changed(nameof(BranchBorder)); }
        if (InBranch != member) { InBranch = member; Changed(nameof(InBranch)); Changed(nameof(BranchBackground)); }
        if (IsBranchRoot != first) { IsBranchRoot = first; Changed(nameof(IsBranchRoot)); Changed(nameof(BranchBackground)); }
    }    public int TreeOrder { get; private set; }
    public bool TreeVisible { get; private set; } = true;
    public string DisplayCategory { get; private set; } = "Processus en arrière-plan";
    public Thickness TreeIndent { get; private set; }
    public Visibility ExpandVisibility { get; private set; } = Visibility.Collapsed;
    public string ExpandGlyph { get; private set; } = "▶";
    public string TreeLabel { get; private set; } = "";
    public void SetTree(int order, bool visible, int depth, int descendants, bool expanded, string category, bool enabled)
    {
        if (!enabled || !visible) SetBranchFrame(false,false,false);
        if (TreeOrder != order) { TreeOrder = order; Changed(nameof(TreeOrder)); }
        if (TreeVisible != visible) { TreeVisible = visible; Changed(nameof(TreeVisible)); }
        if (DisplayCategory != category) { DisplayCategory = category; Changed(nameof(DisplayCategory)); }
        var indent = new Thickness(Math.Min(depth, 12) * 15,0,0,0);
        if (TreeIndent != indent) { TreeIndent = indent; Changed(nameof(TreeIndent)); }
        var visibility = enabled ? descendants > 0 ? Visibility.Visible : Visibility.Hidden : Visibility.Collapsed;
        if (ExpandVisibility != visibility) { ExpandVisibility = visibility; Changed(nameof(ExpandVisibility)); }
        string glyph = expanded ? "▼" : "▶";
        if (ExpandGlyph != glyph) { ExpandGlyph = glyph; Changed(nameof(ExpandGlyph)); }
        string label = enabled && descendants > 0 ? $" ({descendants})" : "";
        if (TreeLabel != label) { TreeLabel = label; Changed(nameof(TreeLabel)); }
    }    public ProcessRow(ProcessSample sample) => Sample = sample;
    public void Update(ProcessSample next)
    {
        var old = Sample; Sample = next;
        if (old.Name != next.Name) Changed(nameof(Name));
        if (old.Cpu != next.Cpu) Changed(nameof(Cpu));
        if (Math.Round(old.WorkingSet / 1048576d, 1) != MemoryMb) { Changed(nameof(MemoryMb)); Changed(nameof(MemoryPercent)); Changed(nameof(MemoryLabel)); }
        if (old.Threads != next.Threads) Changed(nameof(Threads));
        if (old.Handles != next.Handles) Changed(nameof(Handles));
    }
    public event PropertyChangedEventHandler? PropertyChanged;
    private void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));
}

public partial class MainWindow : Window
{
    private readonly ObservableCollection<ProcessRow> rows = new();
    private readonly Dictionary<(int, long), ProcessRow> indexed = new();
    private readonly CancellationTokenSource stopping = new();
    private readonly DispatcherTimer searchDelay = new() { Interval = TimeSpan.FromMilliseconds(150) };
    private ICollectionView view = null!;
    private bool paused, actionPending, performanceVisible;
    private string query = ""; private string categoryFilter = "";
    private int detailVersion;
    private bool treeEnabled;
    private readonly HashSet<(int,long)> expandedProcesses = new();
    private readonly HashSet<(int,long)> searchCollapsed = new();
    private string activeSort = nameof(ProcessRow.Name);
    private ListSortDirection activeDirection = ListSortDirection.Ascending;
    public MainWindow()
    {
        InitializeComponent();
        Machine.Text = Environment.MachineName;
        Cores.Text = $"{Environment.ProcessorCount} processeurs logiques";
        view = CollectionViewSource.GetDefaultView(rows);
        view.Filter = item => item is ProcessRow row && (treeEnabled ? row.TreeVisible : Matches(row.Sample, query) && (categoryFilter.Length == 0 || row.Category == categoryFilter));
        view.SortDescriptions.Add(new(nameof(ProcessRow.Name), ListSortDirection.Ascending));
        if (view is ICollectionViewLiveShaping live) { if (live.CanChangeLiveSorting) live.IsLiveSorting = true; if (live.CanChangeLiveFiltering) { live.LiveFilteringProperties.Add(nameof(ProcessRow.Category)); live.LiveFilteringProperties.Add(nameof(ProcessRow.TreeVisible)); live.IsLiveFiltering = true; } if (live.CanChangeLiveGrouping) { live.LiveGroupingProperties.Add(nameof(ProcessRow.Category)); live.LiveGroupingProperties.Add(nameof(ProcessRow.DisplayCategory)); live.IsLiveGrouping = true; } }
        ProcessGrid.SizeChanged += (_, _) => ProcessGrid.Columns[0].Width = new System.Windows.Controls.DataGridLength(Math.Max(220, ProcessGrid.ActualWidth - 565)); ProcessGrid.ItemsSource = view; ProcessGrid.Columns[0].SortDirection = ListSortDirection.Ascending; PerformancePage.PauseRequested += (_, _) => Pause_Click(this, new RoutedEventArgs());
        searchDelay.Tick += (_, _) => { searchDelay.Stop(); query = Search.Text.Trim(); searchCollapsed.Clear(); if (treeEnabled) RebuildTree(); else view.Refresh(); UpdateEmpty(); };
        Loaded += async (_, _) => await RunAsync();
        Closed += (_, _) => { stopping.Cancel(); searchDelay.Stop(); detailVersion++; };
        PreviewKeyDown += (_, e) =>
        {
            if (e.Key == Key.F && Keyboard.Modifiers == ModifierKeys.Control) { ShowPerformance(false); Search.Focus(); Search.SelectAll(); e.Handled = true; }
            if (e.Key == Key.Escape) { Search.Clear(); DetailPanel.Visibility = Visibility.Collapsed; detailVersion++; }
        };
    }
    public static bool Matches(ProcessSample sample, string query) => sample.Name.Contains(query, StringComparison.OrdinalIgnoreCase) || sample.Id.ToString().Contains(query, StringComparison.Ordinal);
    private async Task RunAsync()
    {
        using var sampler = new ProcessSampler();
        PerformanceCollector? performance = null; var metadata = new ProcessMetadataCollector();
        try
        {
            while (!stopping.IsCancellationRequested)
            {
                if (!paused && WindowState != WindowState.Minimized)
                {
                    try
                    {
                        bool collectPerformance = performanceVisible;
                        var result = await Task.Run(() =>
                        {
                            var system = sampler.Collect();
                            if (collectPerformance)
                            {
                                performance ??= new PerformanceCollector();
                                return (system, perf: performance.Collect(system), meta: (Dictionary<int, ProcessMetadata>?)null);
                            }
                            return (system, perf: (PerfSnapshot?)null, meta: metadata.Collect(system.Processes));
                        }, stopping.Token);
                        if (!stopping.IsCancellationRequested && !paused)
                        {
                            if (result.perf != null && performanceVisible) PerformancePage.Apply(result.perf);
                            else if (!performanceVisible && result.meta != null) Apply(result.system, result.meta);
                        }
                    }
                    catch (OperationCanceledException) { break; }
                    catch (Exception ex) { Status.Text = "Collecte interrompue : " + ex.Message; PerformancePage.ShowError(Status.Text); }
                }
                await Task.Delay(1000, stopping.Token);
            }
        }
        catch (OperationCanceledException) { } finally { performance?.Dispose(); }
    }
    private void Apply(Snapshot snapshot, Dictionary<int, ProcessMetadata> metadata)
    {
        var watch = Stopwatch.StartNew();
        var present = new HashSet<(int, long)>();
        foreach (var sample in snapshot.Processes)
        {
            var key = (sample.Id, sample.Created); present.Add(key);
            if (indexed.TryGetValue(key, out var row)) row.Update(sample);
            else { row = new(sample); indexed.Add(key, row); rows.Add(row); }
            row.SetMemoryCapacity(snapshot.TotalGb * 1073741824d);
            if (metadata.TryGetValue(sample.Id, out var info)) row.SetMetadata(info);
        }
        foreach (var key in indexed.Keys.Where(k => !present.Contains(k)).ToArray())
        { rows.Remove(indexed[key]); indexed.Remove(key); }
        expandedProcesses.RemoveWhere(key => !indexed.ContainsKey(key));
        if (treeEnabled) RebuildTree();
        CpuTotal.Text = snapshot.Cpu is double cpu ? $"{cpu:N0} %" : "Mesure…";
        MemoryTotal.Text = $"{snapshot.MemoryPercent} %";
        MemoryTotal.ToolTip = $"{snapshot.UsedGb:N1} / {snapshot.TotalGb:N1} Go";
        ProcessTotal.Text = rows.Count.ToString();
        Status.Text = $"1 s · Collecte {snapshot.CollectionMs:N1} ms · Mise à jour {watch.Elapsed.TotalMilliseconds:N1} ms";
        UpdateEmpty();
    }
    private void CategoryFilter_Changed(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (view == null) return;
        categoryFilter = (CategoryFilter.SelectedItem as System.Windows.Controls.ComboBoxItem)?.Tag as string ?? "";
        RefreshProcessOptions();
    }
    private void GroupOptions_Changed(object sender, RoutedEventArgs e)
    {
        if (view == null) return;
        RefreshProcessOptions();
    }
    private void RefreshProcessOptions()
    {
        var selected = ProcessGrid.SelectedItems.Cast<ProcessRow>().ToArray();
        var sorting = view.SortDescriptions.Where(s => s.PropertyName != nameof(ProcessRow.Category)).ToArray();
        using (view.DeferRefresh())
        {
            view.GroupDescriptions.Clear();
            if (GroupProcesses.IsChecked == true)
            {
                var groups = new PropertyGroupDescription(treeEnabled ? nameof(ProcessRow.DisplayCategory) : nameof(ProcessRow.Category));
                foreach (string category in new[] { "Applications", "Processus en arrière-plan", "Processus Windows" })
                    if (categoryFilter.Length == 0 || categoryFilter == category) groups.GroupNames.Add(category);
                view.GroupDescriptions.Add(groups);
            }
            view.SortDescriptions.Clear();
            foreach (var sort in sorting) view.SortDescriptions.Add(sort);
        }
        if (treeEnabled) RebuildTree(); else view.Refresh();
        foreach (var row in selected)
            if (view.Contains(row) && !ProcessGrid.SelectedItems.Contains(row)) ProcessGrid.SelectedItems.Add(row);
        UpdateEmpty();
    }
    private void ProcessGrid_Sorting(object sender, System.Windows.Controls.DataGridSortingEventArgs e)
    {
        e.Handled = true;
        var property = e.Column.SortMemberPath;
        if (string.IsNullOrEmpty(property)) return;
        var direction = e.Column.SortDirection switch
        {
            ListSortDirection.Ascending => ListSortDirection.Descending,
            ListSortDirection.Descending => ListSortDirection.Ascending,
            _ => property == nameof(ProcessRow.Name) ? ListSortDirection.Ascending : ListSortDirection.Descending
        };
        activeSort = property; activeDirection = direction;
        if (treeEnabled)
        {
            foreach (var column in ProcessGrid.Columns) column.SortDirection = column == e.Column ? direction : null;
            RebuildTree(); return;
        }
        var selected = ProcessGrid.SelectedItems.Cast<ProcessRow>().ToArray();
        using (view.DeferRefresh())
        {
            view.SortDescriptions.Clear();
            view.SortDescriptions.Add(new(property, direction));
            if (property != nameof(ProcessRow.Name)) view.SortDescriptions.Add(new(nameof(ProcessRow.Name), ListSortDirection.Ascending));
            if (property != nameof(ProcessRow.Id)) view.SortDescriptions.Add(new(nameof(ProcessRow.Id), ListSortDirection.Ascending));
            if (view is ICollectionViewLiveShaping live && live.CanChangeLiveSorting)
            {
                live.LiveSortingProperties.Clear();
                foreach (var sort in view.SortDescriptions) live.LiveSortingProperties.Add(sort.PropertyName);
                live.IsLiveSorting = true;
            }
        }
        foreach (var column in ProcessGrid.Columns) column.SortDirection = column == e.Column ? direction : null;
        foreach (var row in selected)
            if (view.Contains(row) && !ProcessGrid.SelectedItems.Contains(row)) ProcessGrid.SelectedItems.Add(row);
    }
    private void TreeMode_Changed(object sender, RoutedEventArgs e)
    {
        if (view == null) return;
        treeEnabled = TreeProcesses.IsChecked == true;
        TreeHelp.Visibility = treeEnabled ? Visibility.Visible : Visibility.Collapsed;
        using (view.DeferRefresh())
        {
            view.SortDescriptions.Clear();
            if (treeEnabled) view.SortDescriptions.Add(new(nameof(ProcessRow.TreeOrder), ListSortDirection.Ascending));
            else
            {
                foreach (var row in rows) row.SetTree(0,true,0,0,false,row.Category,false);
                view.SortDescriptions.Add(new(activeSort,activeDirection));
                if (activeSort != nameof(ProcessRow.Id)) view.SortDescriptions.Add(new(nameof(ProcessRow.Id),ListSortDirection.Ascending));
            }
            if (view is ICollectionViewLiveShaping live && live.CanChangeLiveSorting)
            {
                live.LiveSortingProperties.Clear();
                foreach (var sort in view.SortDescriptions) live.LiveSortingProperties.Add(sort.PropertyName);
            }
        }
        RefreshProcessOptions();
    }
    private void ExpandProcess_Click(object sender, RoutedEventArgs e)
    {
        e.Handled = true;
        if (sender is not FrameworkElement { DataContext: ProcessRow row }) return;
        var key = (row.Id,row.Sample.Created);
        if (query.Length > 0) { if (!searchCollapsed.Add(key)) searchCollapsed.Remove(key); }
        else if (!expandedProcesses.Add(key)) expandedProcesses.Remove(key);
        RebuildTree();
    }
    private void RebuildTree()
    {
        var selected = ProcessGrid.SelectedItems.Cast<ProcessRow>().ToArray();
        var layout = ProcessTree.Build(rows,expandedProcesses,query,categoryFilter,activeSort,activeDirection,searchCollapsed);
        var visible = layout.Select(e => e.Row).ToHashSet();
        using (view.DeferRefresh())
        {
            foreach (var row in rows)
                if (!visible.Contains(row)) row.SetTree(int.MaxValue,false,0,0,false,row.Category,true);
            for (int i=0;i<layout.Count;i++)
            {
                var entry=layout[i];
                entry.Row.SetTree(i,true,entry.Depth,entry.Descendants,entry.Expanded,entry.RootCategory,true);
                bool member = entry.Depth > 0 || entry.Descendants > 0;
                entry.Row.SetBranchFrame(member, member && entry.Depth == 0, member && (i == layout.Count - 1 || layout[i + 1].Depth == 0));
            }
        }
        view.Refresh();
        foreach (var row in selected)
            if (view.Contains(row) && !ProcessGrid.SelectedItems.Contains(row)) ProcessGrid.SelectedItems.Add(row);
        UpdateEmpty();
    }    public void ShowPerformance(bool show)
    { performanceVisible = show; ProcessPage.Visibility = show ? Visibility.Collapsed : Visibility.Visible; PerformancePage.Visibility = show ? Visibility.Visible : Visibility.Collapsed; ProcessesTab.Background = (System.Windows.Media.Brush)new System.Windows.Media.BrushConverter().ConvertFromString(show ? "#34343B" : "#2B424A")!; PerformanceTab.Background = (System.Windows.Media.Brush)new System.Windows.Media.BrushConverter().ConvertFromString(show ? "#2B424A" : "#34343B")!; }
    private void Processes_Click(object sender, RoutedEventArgs e) => ShowPerformance(false);
    private void Performance_Click(object sender, RoutedEventArgs e) => ShowPerformance(true);
    private void UpdateEmpty() => EmptyLabel.Visibility = view.IsEmpty ? Visibility.Visible : Visibility.Collapsed;
    private void Search_Changed(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        if (SearchHint != null) SearchHint.Visibility = Search.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        searchDelay.Stop(); searchDelay.Start();
    }
    private void Pause_Click(object sender, RoutedEventArgs e)
    { paused = !paused; PerformancePage.SetPaused(paused); PauseButton.Content = paused ? "▶  Reprendre" : "Ⅱ  Pause"; if (paused) Status.Text = "Actualisation en pause"; }
    private void Selection_Changed(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (EndButton == null) return;
        EndButton.IsEnabled = !actionPending && ProcessGrid.SelectedItems.Count > 0;
        EndButton.Content = ProcessGrid.SelectedItems.Count > 1 ? $"Arrêter {ProcessGrid.SelectedItems.Count} tâches" : "Arrêter la tâche";
    }
    private void ProcessGrid_RightClick(object sender, MouseButtonEventArgs e)
    {
        var source=e.OriginalSource as DependencyObject;
        var row=source as System.Windows.Controls.DataGridRow;
        for (var current=source; row==null && current!=null; current=System.Windows.Media.VisualTreeHelper.GetParent(current)) row=current as System.Windows.Controls.DataGridRow;
        if(row?.Item is not ProcessRow process) { ProcessGrid.ContextMenu=new System.Windows.Controls.ContextMenu(); return; }
        if(!ProcessGrid.SelectedItems.Contains(process)) { ProcessGrid.SelectedItems.Clear(); ProcessGrid.SelectedItems.Add(process); }
        row.Focus();
        ProcessGrid.ContextMenu=CreateProcessMenu(process);
        e.Handled=true;
    }
    private void ProcessGrid_ContextOpening(object sender, System.Windows.Controls.ContextMenuEventArgs e)
    {
        // Keyboard context-menu key / Shift+F10 targets the current selected row.
        if(e.CursorLeft<0)
            ProcessGrid.ContextMenu=ProcessGrid.SelectedItem is ProcessRow row ? CreateProcessMenu(row) : null;
        if(ProcessGrid.ContextMenu==null || ProcessGrid.ContextMenu.Items.Count==0)e.Handled=true;
    }
    public System.Windows.Controls.ContextMenu CreateProcessMenu(ProcessRow target)
    {
        var sample=target.Sample;
        var selected=ProcessGrid.SelectedItems.Cast<ProcessRow>().Select(r=>r.Sample).ToArray();
        if(!selected.Any(s=>s.Id==sample.Id && s.Created==sample.Created))selected=new[]{sample};
        var menu=new System.Windows.Controls.ContextMenu { Background=new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(35,35,44)),Foreground=System.Windows.Media.Brushes.White,BorderBrush=System.Windows.Media.Brushes.MediumPurple,Padding=new Thickness(5) };
        void Item(string title,Action action,bool enabled=true)
        {
            var item=new System.Windows.Controls.MenuItem { Header=title,IsEnabled=enabled,Foreground=System.Windows.Media.Brushes.White,Padding=new Thickness(12,7,12,7) };
            item.Click+=(_,_)=>action(); menu.Items.Add(item);
        }
        Item($"{sample.Name} · PID {sample.Id}",()=>{},false);
        menu.Items.Add(new System.Windows.Controls.Separator());
        Item(selected.Length>1?$"Arrêter les {selected.Length} processus sélectionnés…":"Arrêter ce processus…",async()=>await StopSamplesAsync(selected),!actionPending);
        Item("Informations avancées…",()=>new ProcessDetailsWindow(sample){Owner=this}.Show());
        menu.Items.Add(new System.Windows.Controls.Separator());
        Item("Ouvrir l’emplacement du fichier",async()=>await FileActionAsync(sample,false));
        Item("Copier le chemin",async()=>await FileActionAsync(sample,true));
        Item("Copier le nom et le PID",()=> { try{Clipboard.SetText($"{sample.Name} · PID {sample.Id}");}catch(Exception ex){ShowActionError(ex);} });
        return menu;
    }
    private async Task FileActionAsync(ProcessSample sample,bool copy)
    {
        try
        {
            var path=await Task.Run(()=>ProcessActions.GetPath(sample));
            if(copy)Clipboard.SetText(path);
            else Process.Start(new ProcessStartInfo("explorer.exe") { UseShellExecute=false,Arguments="/select,\""+path+"\"" });
        }
        catch(Exception ex){ShowActionError(ex);}
    }
    private void ShowActionError(Exception ex) => MessageBox.Show(this,ex.Message,"Action indisponible",MessageBoxButton.OK,MessageBoxImage.Information);    private async void Inspect_Click(object sender, RoutedEventArgs e)
    {
        if (ProcessGrid.SelectedItem is not ProcessRow row) { Status.Text = "Sélectionnez un processus pour l’inspecter."; return; }
        int version = ++detailVersion;
        var sample = row.Sample;
        DetailPanel.Visibility = Visibility.Visible; Details.Text = "Lecture des détails…";
        string result;
        try { result = await Task.Run(() => ProcessActions.Describe(sample)); }
        catch (Exception ex) { result = $"{sample.Name} · PID {sample.Id}\nDétails indisponibles : {ex.Message}"; }
        if (version == detailVersion && !stopping.IsCancellationRequested) Details.Text = result;
    }
    private void CloseDetails_Click(object sender, RoutedEventArgs e) { detailVersion++; DetailPanel.Visibility = Visibility.Collapsed; }
    private async void End_Click(object sender, RoutedEventArgs e) => await StopSamplesAsync(ProcessGrid.SelectedItems.Cast<ProcessRow>().Select(r => r.Sample).ToArray());
    private async Task StopSamplesAsync(ProcessSample[] selected)
    {
        if (actionPending) return;
        if (selected.Length == 0) return;
        string names = string.Join("\n", selected.Take(8).Select(s => $"• {s.Name} ({s.Id})"));
        if (selected.Length > 8) names += $"\n… et {selected.Length - 8} autres";
        if (MessageBox.Show(this, $"Arrêter {selected.Length} processus ?\n\n{names}\n\nLes modifications non enregistrées peuvent être perdues.", "Arrêter les tâches", MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No) != MessageBoxResult.Yes) return;
        actionPending = true; EndButton.IsEnabled = false;
        var failures = await Task.Run(() =>
        {
            var errors = new List<string>();
            foreach (var sample in selected)
                try { ProcessActions.Terminate(sample); } catch (Exception ex) { errors.Add($"{sample.Name} ({sample.Id}) : {ex.Message}"); }
            return errors;
        });
        actionPending = false; EndButton.IsEnabled = ProcessGrid.SelectedItems.Count > 0;
        if (stopping.IsCancellationRequested) return;
        Status.Text = $"{selected.Length - failures.Count} arrêt(s) demandé(s) · {failures.Count} échec(s)";
        if (failures.Count > 0) MessageBox.Show(this, string.Join("\n", failures), "Résultat de l’arrêt", MessageBoxButton.OK, MessageBoxImage.Information);
    }
}
