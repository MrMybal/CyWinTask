# CyWinTask

**English** · [Français](README.fr.md)

A Windows x64 task manager with a native **Dear ImGui**, **CyUI**, and Direct3D 11 interface. A separate .NET 8 collector supplies Windows measurements over a local stream. The earlier WPF interface remains in the repository for reference; current distributions launch the native interface.

## Screenshots

These screenshots were captured from CyWinTask's demo mode with fictional process and hardware data.

**Processes** — searchable table, usage bars, and expandable related-process groups.

![CyWinTask process table in demo mode](docs/screenshots/processes.png)

**Performance** — compact CPU, memory, GPU, disk, and network graphs.

![CyWinTask performance dashboard in demo mode](docs/screenshots/performance.png)

## Build and run

Install the .NET 8 SDK and Visual Studio 2022 with C++ tools. CyUI is currently a private repository. Contributors with access must fetch the pinned revision before building:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/Sync-CyUI.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File native/Build-Native.ps1
./dist-native/CyWinTask.exe
```

The exact CyUI commit is recorded in `native/CYUI_REVISION`. Its local checkout under `third_party/CyUI` stays outside this public repository. Keep the `bridge` directory next to `CyWinTask.exe`. `Build-Native.ps1 -Offline` uses locally cached NuGet packages. The collector's .NET runtime is included in the output. CyWinTask does not replace Windows Task Manager or its shortcuts.

Run `./dist-native/CyWinTask.exe --demo` to see fictional sample data. Add `--performance` to start on the Performance page. For visual review, `--theme navy|mid-grey|light-grey|amber-night|violet`, `--density compact|comfortable`, and `--ui-scale 1.25` change the current session. The normal appearance is navy with comfortable density.

Text and controls start at **115%**. Use the **A− / A+** buttons or `Ctrl++`, `Ctrl+-`, and `Ctrl+0` to adjust or reset the zoom. The setting is saved and also follows Windows display scaling.

## Processes

- Search by name or PID (`Ctrl+F`) and sort either way by name, PID, CPU, memory, threads, or handles.
- Filter Applications, Background, or Windows processes, with optional grouping by type. Classification uses visible windows and the executable path when available.
- Expand parent/child branches in **Associated processes** mode. Reused PIDs cannot attach old children to a new process. Values remain per process rather than sums of descendants.
- Select multiple rows with Ctrl or Shift. Group outlines, selection highlights, CPU and memory bars, and progressively loaded executable icons help identify processes.
- Right-click to stop selected processes with confirmation, inspect advanced information, open the executable's folder, or copy its path or name/PID. Actions validate PID and creation time. Critical processes and CyWinTask itself cannot be stopped; children are never stopped implicitly.

Advanced information includes the path, start time, session, priority, affinity, memory counters, and accessible modules. Protected processes may limit the available details.

## Performance

The Performance page shows CPU, physical and committed virtual memory, disks, network, and GPU. It keeps up to 60 seconds of real measurements without fabricating earlier samples. Missing counters appear as unavailable.

Each resource shows its current value. Graphs have a grid, value scales, and time markers; hovering reveals nearby measurements. Disk activity and throughput share a chart with separate percentage and MB/s scales.

- CPU: total utilization and logical processors, with **Compact (Windows)** and **Detailed** layouts.
- Memory: used and available physical memory plus committed virtual memory.
- Disks: activity, read, and write curves in three colors on one graph.
- GPU: utilization, engines, and memory exposed by Windows. CPU/GPU temperatures appear when a compatible sensor is available.
- **Graph view**: a compact dashboard of CPU, RAM, GPU, disks, and network. The layout menu selects automatic or two to four columns, larger cards, or CPU/RAM/GPU only. Open the dashboard or an individual card in a separate window; all windows share the same measurements and pause state.

CPU and GPU values may differ from Windows Task Manager. Temperature availability depends on sensors; CyWinTask does not install a driver. GPU data comes from WDDM and DXGI counters, and some drivers do not publish every engine.

## Architecture

The collector reuses a `NtQuerySystemInformation` buffer for processes and PDH queries for performance, sending a snapshot about once per second. Performance collection runs only while its page or a graph window is open. Icons are cached and loaded under a per-frame budget; the process table draws only visible rows. Native rendering slows when unfocused and pauses while minimized.

The frontend and collector build independently from `native/CMakeLists.txt` and `Bridge/CyWinTask.Bridge.csproj`. The native frontend links `CyUI::Core` and `CyUI::Plots`; Plots pulls in Canvas. Audio, Gallery, and Demo are disabled. The application supplies one Dear ImGui target shared with CyUI. CyUI 1.2.0 is pinned by `native/CYUI_REVISION`, and the distributed licenses and notices are in the output's `licenses` directory. The historical WPF frontend remains buildable with `dotnet build CyWinTask.csproj -c Release`; its tests run with `dotnet run --project Tests/CyWinTask.Tests.csproj -c Release`.

CyUI Plots draws the grids and curves. It currently has one Y axis, so CyWinTask maps disk throughput to the activity scale while showing real throughput on the right axis and in hover details. The process table retains its thin usage bar to distinguish loads below 1%, which CyUI's rounded progress bar draws at a minimum width.

## Portable package and installer

Build both Windows x64 distributions with Inno Setup 6:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/Build-Release.ps1 -Version 0.7.0 -InnoCompiler "C:\path\to\ISCC.exe"
```

Outputs are written to `artifacts/releases/<version>/`. Extract the whole portable ZIP before launching `CyWinTask.exe`. The installer places the app in the current user's profile, adds a Start menu shortcut, and can add a desktop shortcut.

## Current limits

Per-process memory is the working set; summing it does not equal physical memory use. Protected processes may hide their path, icon, or modules. Application grouping is heuristic and does not exactly match Windows Task Manager. Application history, startup programs, and service management are not included yet.

References: [NtQuerySystemInformation](https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation), [PDH counters](https://learn.microsoft.com/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw), [DXGI](https://learn.microsoft.com/windows/win32/api/dxgi/ns-dxgi-dxgi_adapter_desc1).
