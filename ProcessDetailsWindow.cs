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
        Width=850; Height=650; MinWidth=580; MinHeight=400; Background=new SolidColorBrush(Color.FromRgb(25,25,31));
        WindowStartupLocation=WindowStartupLocation.CenterOwner;
        var dock=new DockPanel { Margin=new Thickness(22) };
        var header=new TextBlock { Text=$"{sample.Name} · PID {sample.Id}",FontSize=24,Margin=new Thickness(0,0,0,12) };
        DockPanel.SetDock(header,Dock.Top); dock.Children.Add(header);
        var footer=new TextBlock { Text="Instantané à l’ouverture · Ctrl+A / Ctrl+C pour copier · certains accès peuvent être protégés",Margin=new Thickness(0,12,0,0),TextWrapping=TextWrapping.Wrap };
        DockPanel.SetDock(footer,Dock.Bottom); dock.Children.Add(footer);
        var text=new TextBox { Text="Lecture des informations…",IsReadOnly=true,AcceptsReturn=true,TextWrapping=TextWrapping.Wrap,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,FontFamily=new FontFamily("Consolas"),FontSize=13 };
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
