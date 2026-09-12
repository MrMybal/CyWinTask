using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using CyWinTask;
internal static class ContextChecks
{
    static void Check(bool ok,string message){if(!ok)throw new Exception(message);Console.WriteLine("PASS "+message);}
    static void Capture(FrameworkElement target,string file)
    {target.UpdateLayout();var bitmap=new RenderTargetBitmap((int)target.ActualWidth,(int)target.ActualHeight,96,96,PixelFormats.Pbgra32);bitmap.Render(target);var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));using var stream=File.Create(file);encoder.Save(stream);}
    public static int Run()
    {
        var app=new App();app.InitializeComponent();var window=new MainWindow();Exception? failure=null;
        window.Loaded+=async(_,_)=>
        {
            try
            {
                var grid=(DataGrid)window.FindName("ProcessGrid");
                for(int i=0;i<40 && grid.Items.Count<10;i++)await Task.Delay(250);
                ((Button)window.FindName("PauseButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                var items=grid.Items.Cast<ProcessRow>().Take(3).ToArray();Check(items.Length==3,"Processus disponibles");
                grid.SelectedItems.Add(items[0]);grid.SelectedItems.Add(items[1]);grid.ScrollIntoView(items[1]);window.UpdateLayout();
                void RightClick(ProcessRow process)
                {
                    grid.ScrollIntoView(process);window.UpdateLayout();
                    var row=(DataGridRow)grid.ItemContainerGenerator.ContainerFromItem(process);
                    grid.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice,0,MouseButton.Right){RoutedEvent=UIElement.PreviewMouseRightButtonDownEvent,Source=row});
                }
                RightClick(items[1]);
                Check(grid.SelectedItems.Count==2,"Clic droit preserve la multiselection");
                var menu=grid.ContextMenu;
                Check(menu.Items.OfType<MenuItem>().Any(m=>m.Header.ToString()!.Contains("2 processus")),"Menu arret adapte aux deux cibles");
                RightClick(items[2]);
                Check(grid.SelectedItems.Count==1 && grid.SelectedItems.Contains(items[2]),"Clic droit hors selection cible seulement la ligne");
                menu=grid.ContextMenu;
                Check(menu.Items.OfType<MenuItem>().Count()==6,"Menu complet : arret, diagnostic et fichiers");
                menu.PlacementTarget=grid;menu.IsOpen=true;await Task.Delay(200);Capture(menu,"preview-context-menu.png");menu.IsOpen=false;
                Capture(window,"preview-selection.png");
                using var sampler=new ProcessSampler();var self=sampler.Collect().Processes.Single(p=>p.Id==Environment.ProcessId);
                var report=await Task.Run(()=>ProcessDetailsWindow.ReadReport(self));
                Check(report.Contains("Mémoire privée") && report.Contains("MODULES CHARGÉS") && report.Contains("Temps CPU total"),"Diagnostic avance du processus de test");
                var stale=await Task.Run(()=>ProcessDetailsWindow.ReadReport(self with { Created=1 }));
                Check(stale.Contains("PID réutilisé"),"Diagnostic refuse une identite perimee");
                var details=new ProcessDetailsWindow(self){Owner=window};details.Show();await Task.Delay(1200);Capture(details,"preview-advanced.png");details.Close();
            }
            catch(Exception ex){failure=ex;}finally{window.Close();app.Shutdown();}
        };
        app.Run(window);if(failure!=null){Console.Error.WriteLine(failure);return 1;}return 0;
    }
}
