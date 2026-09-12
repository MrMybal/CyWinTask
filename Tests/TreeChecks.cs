using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using CyWinTask;
internal static class TreeChecks
{
    static void Check(bool ok,string message) { if(!ok) throw new Exception(message); Console.WriteLine("PASS "+message); }
    static ProcessRow Row(int id,int parent,long created,string name,double cpu=0,string category="Processus en arrière-plan")
    { var row=new ProcessRow(new(id,created,name,parent,cpu,1024,1,1)); row.SetMetadata(new(category,null)); return row; }
    static IEnumerable<T> Find<T>(DependencyObject root) where T:DependencyObject
    {
        for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);i++)
        { var child=VisualTreeHelper.GetChild(root,i); if(child is T t)yield return t; foreach(var item in Find<T>(child))yield return item; }
    }
    public static int Run()
    {
        try
        {
            var root=Row(1,0,10,"Application",1,"Applications"); var child=Row(2,1,11,"Worker",70); var grandchild=Row(3,2,12,"Helper",3); var sibling=Row(4,1,13,"WorkerB",30);
            var reused=Row(5,1,5,"Ancien enfant"); var missing=Row(6,99,20,"Orphelin");
            var list=new[]{root,child,grandchild,sibling,reused,missing}; var expanded=new HashSet<(int,long)>();
            var collapsed=ProcessTree.Build(list,expanded,"","","Cpu",ListSortDirection.Descending);
            Check(collapsed.Count==3 && collapsed.Single(e=>e.Row==root).Descendants==3,"Compteur des descendants et branches repliees");
            expanded.Add((1,10)); expanded.Add((2,11));
            var full=ProcessTree.Build(list,expanded,"","","Cpu",ListSortDirection.Descending);
            Check(full.Select(e=>e.Row.Id).Distinct().Count()==6,"Chaque processus apparait une seule fois");
            int index=full.FindIndex(e=>e.Row==root);
            Check(full[index+1].Row==child && full[index+2].Row==grandchild && full[index+3].Row==sibling,"Tri entre freres sans detacher les enfants");
            Check(full.Single(e=>e.Row==reused).Depth==0 && full.Single(e=>e.Row==missing).Depth==0,"PID parent reutilise et parent disparu traites en racines");
            var search=ProcessTree.Build(list,new(),"Helper","Applications","Name",ListSortDirection.Ascending);
            Check(search.Select(e=>e.Row.Id).SequenceEqual(new[]{1,2,3}),"Recherche enfant : ancetres conserves et branche ouverte");
            var searchFolded=ProcessTree.Build(list,new(),"Helper","Applications","Name",ListSortDirection.Ascending,new HashSet<(int,long)>{(1,10)});
            Check(searchFolded.Count==1,"Repliage manuel pendant une recherche");
            var category=ProcessTree.Build(list,expanded,"","Applications","Name",ListSortDirection.Ascending);
            Check(category.Count==4 && category.All(e=>e.RootCategory=="Applications"),"Filtre Applications conserve les enfants associes");
            var cycle=ProcessTree.Build(new[]{Row(10,11,1,"A"),Row(11,10,1,"B")},new(),"","","Name",ListSortDirection.Ascending);
            Check(cycle.Count==1 && cycle[0].Descendants==1,"Cycle invalide coupe sans perte ni boucle");
            var newIdentity=ProcessTree.Build(new[]{Row(1,0,20,"Remplacant"),Row(8,1,21,"Enfant")},expanded,"","","Name",ListSortDirection.Ascending);
            Check(newIdentity.Count==1,"Expansion ne suit pas un PID reutilise");
            var app=new App(); app.InitializeComponent(); var window=new MainWindow(); Exception? failure=null;
            window.Loaded+=async(_,_)=>
            {
                try
                {
                    var grid=(DataGrid)window.FindName("ProcessGrid");
                    for(int i=0;i<40&&grid.Items.Count<10;i++)await Task.Delay(250);
                    var tree=(CheckBox)window.FindName("TreeProcesses"); var group=(CheckBox)window.FindName("GroupProcesses");
                    int flatCount=grid.Items.Count;
                    tree.IsChecked=true; await Task.Delay(500); window.UpdateLayout();
                    Check(grid.Items.Count<flatCount,"Mode associe masque les descendants replies");
                    var branch=grid.Items.Cast<ProcessRow>().First(r=>r.ExpandVisibility==Visibility.Visible && r.Category=="Applications");
                    grid.ScrollIntoView(branch); window.UpdateLayout();
                    var button=Find<Button>(grid).First(b=>b.DataContext==branch && b.ToolTip?.ToString()?.StartsWith("Déplier")==true);
                    button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent)); await Task.Delay(300);
                    Check(branch.ExpandGlyph=="▼" && grid.Items.Cast<ProcessRow>().Any(r=>r.TreeIndent.Left>0),"Clic chevron affiche les enfants indentes");
                    grid.SelectedItems.Add(branch);
                    await Task.Delay(1500);
                    Check(branch.ExpandGlyph=="▼" && grid.SelectedItems.Contains(branch),"Expansion et selection conservees apres collecte");
                    foreach(var column in grid.Columns)
                    {
                        var header=Find<DataGridColumnHeader>(grid).Single(h=>h.Column==column);
                        typeof(DataGridColumnHeader).GetMethod("OnClick",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic)!.Invoke(header,null);
                        await Task.Delay(100);
                        var ordered=grid.Items.Cast<ProcessRow>().ToArray();
                        Check(ordered.Zip(ordered.Skip(1)).All(p=>p.First.TreeOrder<p.Second.TreeOrder),"Tri arborescent colonne "+column.Header);
                    }
                    group.IsChecked=true; await Task.Delay(300);
                    Check(grid.Items.Cast<ProcessRow>().Where(r=>r.TreeIndent.Left>0).All(r=>!string.IsNullOrEmpty(r.DisplayCategory)),"Regroupement et arborescence compatibles");
                    group.IsChecked=false;
                    var searchBox=(TextBox)window.FindName("Search"); searchBox.Text=branch.Name;
                    await Task.Delay(500); await window.Dispatcher.InvokeAsync(()=>{},System.Windows.Threading.DispatcherPriority.ContextIdle);
                    Check(grid.Items.Cast<ProcessRow>().Any(r=>r.Id==branch.Id),"Recherche application conserve sa branche");
                    window.UpdateLayout();
                    var bitmap=new RenderTargetBitmap((int)window.ActualWidth,(int)window.ActualHeight,96,96,PixelFormats.Pbgra32);bitmap.Render(window);
                    var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));using(var stream=File.Create("preview-tree.png"))encoder.Save(stream);
                    searchBox.Clear(); await Task.Delay(500);
                    tree.IsChecked=false; await Task.Delay(300);
                    Check(grid.Items.Cast<ProcessRow>().All(r=>r.TreeIndent.Left==0 && r.ExpandVisibility==Visibility.Collapsed),"Retour au mode plat et tri global");
                }
                catch(Exception ex){failure=ex;}finally{window.Close();app.Shutdown();}
            };
            app.Run(window); if(failure!=null)throw failure; return 0;
        }
        catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
    }
}
