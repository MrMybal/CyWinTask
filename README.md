# CyWinTask

**English** · [Français](README.fr.md)

A native Windows x64 task manager built with C# / WPF (.NET 8).

<a id="démarrage"></a>

## Getting started

Run `dist\CyWinTask.exe`. The .NET Desktop 8 x64 runtime is required. To build from source,
install the .NET 8 SDK on Windows x64 and use the commands below to generate `dist`.
CyWinTask runs independently of Windows Task Manager and does not change system shortcuts.

<a id="processus"></a>

## Processes

- CPU, resident memory, threads and handles; click any of the six columns to sort (click again
  to reverse, with a visible arrow). Numeric values sort numerically across all processes by
  default, or within each group when grouping is enabled. Search by name or PID (`Ctrl+F`).
- Ctrl/Shift multiselection; termination requires confirmation and reports failures. Identity
  is checked using PID and creation time. CyWinTask itself and critical processes cannot be terminated.
- Icons extracted from accessible executables, loaded progressively in the background and
  cached. A neutral symbol appears when an icon is absent or inaccessible.
- **View** menu: all processes, Applications, background or Windows, combined with search.
  Optional **Group by type** is off at startup to allow global sorting.
- Applications, Background processes and Windows processes groups. Applications have a visible
  top-level window; Windows processes lack that window and have an accessible path under the
  Windows folder, plus System. This practical classification does not exactly reproduce Task
  Manager's internal rules; a protected process may remain in Background if its path is inaccessible.
- Inspector: path, parent PID, launch time and counters as measured when opened.

## Performance

Select **Performance**, then a resource in the list:

- CPU: total load and an option to show each logical processor.
- Memory: used and available physical memory.
- Physical disks: active time, reads and writes.
- Network interfaces: received and sent data, including virtual interfaces.
- GPU: adapters identified through DXGI and matched to counters by LUID; 3D, copy, encode,
  decode, dedicated and shared memory. Other engines are available through a checkbox.

A real 60-second history, without artificially filling the past. Collection gaps remain gaps.
Rates need at least two samples, so initial counters may be unavailable. Percentage scales
are fixed at 100%; throughput scales adjust automatically and are shown on each graph.
GPU usage is the busiest physical engine, after summing process contributions on that engine.

GPU counter availability depends on the driver. An unexposed engine is marked unavailable.
Temperatures, driver versions, detailed disk specifications, application history, startup
and services are not included yet. GPU detection runs when Performance is first opened;
restart CyWinTask after a GPU hardware change.

<a id="coût-et-architecture"></a>

## Overhead and architecture

- Batched process collection through NtQuerySystemInformation, with a reused native buffer;
  no WMI or per-process handle opened on every refresh.
- Native PDH collection only in Performance; queries and buffers are retained. No PowerShell
  process or external command runs in the application's collection loop.
- One background collection at a time, with one second between cycles. Pausing and minimizing
  suspend collection.
- Virtualized table rows, targeted notifications and a 150 ms deferred filter. All six columns
  render together to remain visible with groups.
- Metadata extraction is limited to 32 new processes per cycle. Paths and icons are retained
  for each process's lifetime.
- Graphs drawn directly with WPF, without animations or individual controls per point.
  History is limited to 60 seconds and 120 points per series.

`diagnostics.json` and `performance-diagnostics.json` contain local test-bench measurements.
They do not prove superiority over Windows Task Manager. WPF memory use and the initial icon
extraction cost still need monitoring. Displayed update time excludes some deferred rendering.

<a id="compilation-et-tests"></a>

## Build and tests

```powershell
dotnet build CyWinTask.csproj -c Release
dotnet run --project Tests/CyWinTask.Tests.csproj -c Release
dotnet publish CyWinTask.csproj -c Release --no-restore -o dist
```

The integration test is tailored to a development machine with a GPU exposing WDDM counters.
It checks real counters, grouping, icons, search, selection, navigation and termination of a
process created solely for the test. Logic assertions cover GPU engine aggregation and the
history limit. Previews are saved as `preview.png` and `preview-performance.png`.

<a id="limites-techniques"></a>

## Technical limitations

The native process API uses the Windows x64 format, with checked buffer bounds. This format
may evolve. Access remains subject to Windows permissions; there is no automatic elevation.
Per-process memory is its total working set; summing it does not give used physical memory.
CPU-time measurements may differ from Task Manager. The Processes counter needs further
validation above 64 logical processors.

Microsoft references:

- https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation
- https://learn.microsoft.com/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw
- https://learn.microsoft.com/windows/win32/api/dxgi/ns-dxgi-dxgi_adapter_desc1

Focused sorting test: `dotnet run --project Tests/CyWinTask.Tests.csproj -c Release -- --sorting`.
Checks both directions on all six headers, arrows, dynamic sorting and selections.

<a id="graphiques-et-repères-visuels-version-03"></a>

## Graphs and visual indicators (version 0.3)

- Memory: an additional committed virtual memory graph and limit measured through
  GetPerformanceInfo. It represents allocations guaranteed by Windows, not actual pagefile contents.
- Disks: three overlaid curves. Green = activity (left axis, 0–100%), blue = reads, orange = writes
  (shared automatic right axis in MB/s). A legend shows current values.
- CPU: enable **Show logical processors**, then select **Compact (Windows)** for a grid of small
  graphs or **Detailed** for larger graphs. The total remains in the summary. Logical processors
  are ordered numerically.
- Processes: CPU and memory bars with readable values and preserved numeric sorting. Width
  is proportional to total CPU capacity or physical RAM. Memory remains the working set;
  shared pages may be counted in several processes.
- CPU colors: blue below 5%, yellow from 5%, orange from 20%, red from 50%.
  RAM colors: blue below 1%, yellow from 1%, orange from 4%, red from 10% of total RAM.
  These thresholds are visual guides, not diagnoses. Tooltips repeat them.

Graphs and bars use direct WPF drawing, with no animations or additional per-process collection.
Memory reference: https://learn.microsoft.com/windows/win32/api/psapi/ns-psapi-performance_information

<a id="processus-associés-version-04"></a>

## Related processes (version 0.4)

Enable **Related processes** to show an expandable tree. The parenthesized count includes
descendants; CPU, RAM, thread and handle values remain those of the individual row, without
adding children. Click the chevron to expand or collapse.

Relationships use the parent PID recorded by Windows and creation times. A vanished parent
or reused PID does not attach children to a new process. Applications with a visible window
start their own branch, so they do not all appear under Explorer. This model does not reproduce
Task Manager's internal application-grouping heuristics or arbitrarily group same-name processes.

Search retains the ancestors needed to locate a child and opens matching branches. Searching
for a parent also shows its descendants. Manual collapsing remains possible during search.
The type filter applies to the branch root, so Applications also includes those applications'
background processes. Grouping by type remains compatible.

Sorting applies between roots and between children sharing a parent. Disabling related mode
restores global sorting. Expanded state is retained by PID and creation time, removing vanished
identities. Termination targets only explicitly selected rows, never hidden descendants automatically.

Dedicated tests: `dotnet run --project Tests/CyWinTask.Tests.csproj -c Release -- --tree`.
Launch option: `dist\CyWinTask.exe --tree`.

<a id="menu-contextuel-et-sélection-version-05"></a>

## Context menu and selection (version 0.5)

Right-click a row to terminate the process or selected rows with confirmation, open advanced
information, show the file in Explorer, or copy the path or name/PID. Right-clicking a selected
row preserves multiselection; right-clicking another row selects only that row. Termination
and file actions retain PID/creation-time validation. Children are never terminated implicitly.

The advanced window loads data in the background on demand: path, version and publisher declared
in the file, launch time, session, priority, affinity, CPU time, resident/private/virtual memory,
threads, handles and up to 200 modules. It is a snapshot; protected processes may restrict
available information. The declared publisher is not signature verification.

Selection uses a purple background and gold outline, distinct from the blue branch frame.
Focused test: `dotnet run --project Tests/CyWinTask.Tests.csproj -c Release -- --context`.

<a id="fichiers-partagés-sur-github"></a>

## Files shared on GitHub

The repository contains sources, tests and the final logo at `Assets/cywintask-logo-blue.png`.
Build outputs (`bin`, `obj`, `dist`), personal IDE settings, local tool histories, diagnostics
and `preview*.png` screenshots remain local through `.gitignore`.

Tests may record process names, PIDs, paths and machine specifications in diagnostics or
screenshots. Never publish those real files: use only fictional data for shared demonstrations
and screenshots. Do not publish secrets or private certificates; Git exclusions do not replace
reviewing new files.

Before committing, check `git status --short` and `git diff --cached`. To inspect untracked
files that would be included, use `git ls-files --others --exclude-standard`.
`.gitignore` does not remove files already tracked.

<a id="binaires-windows-x64"></a>

## Windows x64 binaries

Self-contained distributions include .NET Desktop; no separate runtime installation is needed.

- **Portable**: extract the entire ZIP and run `CyWinTask.exe`. Keep all files in the extracted folder.
- **Installer**: per-user installation in `%LOCALAPPDATA%\Programs\CyWinTask`, with a Start menu
  shortcut and optional desktop shortcut. Uninstall through Windows Settings.

To build both distributions, use the .NET 8 SDK or newer and [Inno Setup 6](https://jrsoftware.org/isdl.php):

```powershell
powershell -NoProfile -File packaging/Build-Release.ps1 -Version 0.5.3 -InnoCompiler "C:\path\to\ISCC.exe"
```

Deliverables and SHA-256 hashes are written to `artifacts/releases/0.5.3/`. Git ignores this folder;
the binaries are intended as GitHub Release attachments. The script uses a fresh publish folder
on each run to avoid including files from an older build. Binaries are not code-signed.

Before publishing, check the distribution folder and ZIP with
`python scripts/check-distribution-privacy.py <folder-or-zip>`. Also check the actual installed files.
The audit's tests use only fictional data: `python Tests/test_distribution_privacy.py`.
