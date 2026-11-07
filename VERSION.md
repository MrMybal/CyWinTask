# CyWinTask 0.7.0

[Français](VERSION.fr.md) · [README](README.md) · [Published release](https://github.com/MrMybal/CyWinTask/releases/tag/v0.7.0)

**Stage: in development.** The published application is the native Windows x64 build. The earlier WPF interface remains in the source repository for reference; it is not the interface started by the 0.7.0 installer or portable package.

## What changed in 0.7.0

- Introduced the native Dear ImGui interface built with CyUI and a separate .NET 8 Windows collector.
- Added a compact process table with search, column sorting, optional type grouping, related-process trees, multiselection and advanced inspection.
- Added CPU and memory usage bars, accessible process icons and confirmed stop actions from the context menu.
- Added a compact performance dashboard and detailed CPU, memory, disk, network and GPU graphs. The dashboard and individual graphs can open in separate windows.
- Added CPU/GPU temperatures when compatible sensors provide them.
- Published a Windows x64 installer and portable ZIP with the required collector runtime.

## Known limitations

- Per-process memory shows the working set; adding the displayed rows does not give total physical memory use.
- Protected processes may hide their executable path, icon or modules. Application grouping is heuristic and can differ from Windows Task Manager.
- CPU/GPU values can differ from Windows Task Manager. Temperature and GPU engine availability depend on hardware, sensors and drivers.
- Application history, startup-program management and service management are not included yet. CyWinTask does not replace Windows Task Manager or its shortcuts.
- Building from source requires access to the private CyUI repository; the published installer and portable package include the compiled interface.

The [0.7.0 release](https://github.com/MrMybal/CyWinTask/releases/tag/v0.7.0) provides the installer, portable ZIP and SHA-256 checksums. It does not publish a bug-by-bug list; use the [issue tracker](https://github.com/MrMybal/CyWinTask/issues) for reported and confirmed problems.
