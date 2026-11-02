using System.Diagnostics;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
namespace CyWinTask;
public sealed class ProcessDetailsWindow : Window
{
    public ProcessDetailsWindow(ProcessSample sample)
    {
        Title = $"Informations avancées — {sample.Name} ({sample.Id})";
        Width=850; Height=650; MinWidth=580; MinHeight=400;
        Background=UiTheme.Brush("Bg1"); Foreground=UiTheme.Brush("Text");
        WindowStartupLocation=WindowStartupLocation.CenterOwner;
        var dock=new DockPanel { Margin=new Thickness(22) };
        var header=new DockPanel { Margin=new Thickness(0,0,0,15) };
        var text=new TextBox { Text="Lecture des informations…",IsReadOnly=true,AcceptsReturn=true,TextWrapping=TextWrapping.Wrap,
            VerticalScrollBarVisibility=ScrollBarVisibility.Auto,FontFamily=new FontFamily("Consolas"),FontSize=13,
            Background=UiTheme.Brush("Bg2"),BorderBrush=UiTheme.Brush("LineStrong"),BorderThickness=new Thickness(1),
            Padding=new Thickness(14) };
        var close=new Button { Content="Fermer" };
        close.Click+=(_,_)=>Close();
        DockPanel.SetDock(close,Dock.Right); header.Children.Add(close);
        var copy=new Button { Content="Copier tout", ToolTip="Copier toutes les informations affichées" };
        copy.Click+=(_,_)=> { try { Clipboard.SetText(text.Text); } catch (Exception ex) { MessageBox.Show(this, ex.Message, "Copie indisponible", MessageBoxButton.OK, MessageBoxImage.Information); } };
        DockPanel.SetDock(copy,Dock.Right); header.Children.Add(copy);
        header.Children.Add(new TextBlock { Text=$"{sample.Name} · PID {sample.Id}",FontSize=23,FontWeight=FontWeights.SemiBold,VerticalAlignment=VerticalAlignment.Center });
        DockPanel.SetDock(header,Dock.Top); dock.Children.Add(header);
        var footer=new TextBlock { Text="Instantané à l’ouverture · certains accès peuvent être protégés",Margin=new Thickness(0,12,0,0),TextWrapping=TextWrapping.Wrap,Foreground=UiTheme.Brush("TextDim") };
        DockPanel.SetDock(footer,Dock.Bottom); dock.Children.Add(footer);
        dock.Children.Add(text); Content=dock;
        Loaded+=async(_,_)=> { var report=await Task.Run(()=>ReadReport(sample)); if(IsVisible)text.Text=report; };
    }
    public static string ReadReport(ProcessSample sample)
    {
        var b=new StringBuilder();
        b.AppendLine($"Processus : {sample.Name}\nPID : {sample.Id}\nPID parent : {sample.ParentId}\nRelevé : {DateTime.Now:G}\n");
        void Add(string label,Func<object?> read)
        { try{b.AppendLine(label+" : "+read());}catch(Exception ex){b.AppendLine(label+" : indisponible ("+ex.Message+")");} }
        string? path=null;
        Add("Exécutable",()=>path=ProcessActions.GetPath(sample));
        if(path!=null)
        {
            Add("Description",()=>FileVersionInfo.GetVersionInfo(path).FileDescription);
            Add("Éditeur déclaré",()=>FileVersionInfo.GetVersionInfo(path).CompanyName);
            Add("Version du fichier",()=>FileVersionInfo.GetVersionInfo(path).FileVersion);
        }
        b.AppendLine("\nCOMPTEURS DU PROCESSUS");
        try
        {
            using var process=Process.GetProcessById(sample.Id);
            _=process.Handle; // Retain one process handle before reading identity and properties.
            if(process.StartTime.ToUniversalTime().ToFileTimeUtc()!=sample.Created)throw new InvalidOperationException("Processus disparu ou PID réutilisé.");
            Add("Démarrage",()=>process.StartTime);
            Add("Session",()=>process.SessionId);
            Add("Priorité",()=>process.PriorityClass);
            Add("Affinité CPU (masque hexadécimal)",()=>"0x"+process.ProcessorAffinity.ToInt64().ToString("X"));
            Add("Temps CPU total",()=>process.TotalProcessorTime);
            Add("Temps CPU utilisateur",()=>process.UserProcessorTime);
            Add("Temps CPU noyau",()=>process.PrivilegedProcessorTime);
            Add("Mémoire résidente",()=>$"{process.WorkingSet64/1048576d:N1} Mo");
            Add("Mémoire privée",()=>$"{process.PrivateMemorySize64/1048576d:N1} Mo");
            Add("Espace virtuel",()=>$"{process.VirtualMemorySize64/1048576d:N1} Mo");
            Add("Pic de mémoire résidente",()=>$"{process.PeakWorkingSet64/1048576d:N1} Mo");
            Add("Threads",()=>process.Threads.Count);
            Add("Handles",()=>process.HandleCount);
            b.AppendLine("\nMODULES CHARGÉS (200 maximum)");
            try
            {
                var modules=process.Modules;
                foreach(ProcessModule module in modules.Cast<ProcessModule>().Take(200))
                    b.AppendLine($"{module.ModuleName} · {module.ModuleMemorySize/1024:N0} Ko\n  {module.FileName}");
                if(modules.Count>200)b.AppendLine($"… {modules.Count-200} modules supplémentaires non affichés.");
            }
            catch(Exception ex){b.AppendLine("Modules indisponibles : "+ex.Message);}
        }
        catch(Exception ex){b.AppendLine("Accès aux détails indisponible : "+ex.Message);}
        return b.ToString();
    }
}
