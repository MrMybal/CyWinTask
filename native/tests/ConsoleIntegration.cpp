#include "../src/ConsolePane.h"
#include "../src/SensorLog.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

int main()
{
    for (int shell : {1, 0}) {
        ConsolePane console;
        if (!console.startSession(shell)) return 1;
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (console.output().find(shell == 0 ? "PS " : ">") != std::string::npos) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        const std::string set = shell == 0 ? "$global:CyWinTaskCheck='ready'" : "set CYWINTASKCHECK=ready";
        const std::string get = shell == 0 ? "Write-Output ('CHECK:' + $global:CyWinTaskCheck)" : "echo CHECK:%CYWINTASKCHECK%";
        if (!console.executeCommand(set, shell)) return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        if (!console.executeCommand(get, shell)) return 1;
        bool found = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (console.output().find("CHECK:ready") != std::string::npos) { found = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!found) {
            std::fprintf(stderr, "Persistent console shell %d failed\n", shell);
            return 1;
        }
        console.stopSession();
        std::printf("PASS persistent console shell %d\n", shell);
    }
    wchar_t tempDir[MAX_PATH] {}, tempFile[MAX_PATH] {};
    if (!GetTempPathW(MAX_PATH, tempDir) || !GetTempFileNameW(tempDir, L"CWT", 0, tempFile)) return 1;
    const std::filesystem::path csv = tempFile;
    {
        std::ofstream file(csv, std::ios::binary | std::ios::trunc);
        file << "Date,Time,CPU Temperature [C],GPU Temperature [C]\n"
             << "2025-01-01,12:00,51.5,62.0\n";
    }
    SensorLog log;
    log.openFile(csv);
    log.refresh();
    const auto& values = log.readings();
    const bool valid = values.size() == 2 && values[0].current == 51.5 && values[1].current == 62.0;
    std::error_code ignored;
    std::filesystem::remove(csv, ignored);
    if (!valid) { std::fprintf(stderr, "CSV sensor parsing failed\n"); return 1; }
    std::printf("PASS fictional CSV sensors\n");
    return 0;
}
