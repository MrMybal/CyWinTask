# CyWinTask 0.8.0

[Français](VERSION.fr.md) · [README](README.md) · [Published release](https://github.com/MrMybal/CyWinTask/releases/tag/v0.8.0)

**Stage: in development.** The published application is the native Windows x64 build. The earlier WPF interface remains in the source repository for reference.

## What changed in 0.8.0

- Added an integrated Assistant with provider selection, chat and a local MCP server.
- Added `CyWinTaskCli.exe` for JSON commands and MCP connections from a terminal or external assistant.
- Added read-only system and process tools, paged device and process queries, and an advanced process report.
- Added controls for navigation, process filters and selection, graph layout, detachable windows, pause and zoom through the open application's tool registry.
- Added individual tool permissions in the Assistant tab, with presets for read-only access, interface control and all tools. Process termination requires explicit permission and confirmation.
- Updated the Windows x64 installer and portable ZIP to include the assistant, CLI and integration guide.

## Known limitations

- An AI provider must support tool calls to use the application's tools. Interface commands require an open graphical instance; the standalone CLI has no interface to control.
- Tool permissions in the graphical application apply to the current session. The standalone CLI has separate `--allow-details` and `--allow-stop` flags.
- Per-process memory shows the working set; adding the displayed rows does not give total physical memory use.
- Protected processes may hide their executable path, icon or modules. Application grouping is heuristic and can differ from Windows Task Manager.
- CPU/GPU values can differ from Windows Task Manager. Temperature and GPU engine availability depend on hardware, sensors and drivers.
- Application history, startup-program management and service management are not included yet. CyWinTask does not replace Windows Task Manager or its shortcuts.
- Building from source requires access to the private CyUI and CyMCPSDK repositories; the published packages include the compiled dependencies.

See the [Assistant, CLI and MCP guide](docs/CyMCPSDK.fr.md) for commands, connections and permissions. The [0.8.0 release](https://github.com/MrMybal/CyWinTask/releases/tag/v0.8.0) provides the installer, portable ZIP and SHA-256 checksums.
