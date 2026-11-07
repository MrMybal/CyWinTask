using CyWinTask;

internal static class ProcessGpuChecks
{
    public static int Run()
    {
        var processes = new List<ProcessSample>
        {
            new(4200, 10, "Atelier.exe", 0, 0, 0, 0, 0),
            new(4201, 11, "Atelier Renderer.exe", 4200, 0, 0, 0, 0),
            new(4202, 12, "Atelier Media.exe", 4200, 0, 0, 0, 0),
            new(5600, 13, "Navigateur.exe", 0, 0, 0, 0, 0),
            new(6800, 9, "Ancien processus.exe", 4200, 0, 0, 0, 0)
        };
        var counters = new Dictionary<string, double>
        {
            ["pid_4200_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D"] = 4,
            ["pid_4201_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D"] = 28,
            ["pid_4201_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D#1"] = 7,
            ["pid_4202_luid_0x00000000_0x00000001_phys_0_eng_1_engtype_Copy"] = 60,
            ["pid_5600_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D"] = 12,
            ["pid_6800_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D"] = 7,
            ["pid_42000_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D"] = 99
        };
        var usage = ProcessGpuCollector.Aggregate(counters, processes);
        Check(usage[4200].Own == 4 && usage[4200].Group == 60, "Groupe : maximum entre moteurs, processus seul distinct");
        Check(usage[4201].Own == 35 && usage[4201].Group == 35, "Enfant : instances du même moteur réunies");
        Check(usage[5600].Group == 12 && usage[6800].Group == 7, "Aucun mélange entre applications et PID réutilisé");
        Check(ProcessGpuCollector.Aggregate(new Dictionary<string, double>(), processes)[4200].Own == 0,
            "Compteur disponible sans activité : zéro");
        var capped = ProcessGpuCollector.Aggregate(new Dictionary<string, double>
        {
            ["pid_4200_luid_0x00000000_0x00000001_phys_0_eng_0_engtype_3D"] = 125,
            ["pid_4200_luid_0x00000000_0x00000002_phys_0_eng_0_engtype_3D"] = 40
        }, processes);
        Check(capped[4200].Own == 100, "Valeur bornée à 100 % sans additionner les GPU");
        Console.WriteLine("Process GPU checks passed.");
        return 0;
    }
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
        Console.WriteLine("PASS " + message);
    }
}
