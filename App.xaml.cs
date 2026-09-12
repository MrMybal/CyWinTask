using System.Windows;
namespace CyWinTask;
public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        if (MainWindow == null)
        {
            var window = new MainWindow();
            if (e.Args.Contains("--performance")) window.ShowPerformance(true);
            if (e.Args.Contains("--tree")) ((System.Windows.Controls.CheckBox)window.FindName("TreeProcesses")).IsChecked = true;
            window.Show();
        }
    }
}
