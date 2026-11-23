# CyWinTask 0.11.0

[Français](VERSION.fr.md) · [README](README.md) · [Release](https://github.com/MrMybal/CyWinTask/releases/tag/v0.11.0)

**Stage: in development.** This version includes the native Windows x64 application, its collector and the standalone CLI.

## What changed in 0.11.0

- Replaced the crowded top bar with a CyUI sidebar, live resource meters and a compact icon mode for narrow windows.
- Reworked Processes with a lighter table, clearer parent/child guides, load-tinted cells and a side inspector for identity, trust, properties and actions.
- Reworked Performance with compact chart cards, value legends, ranked processes, key sensors and card-based details.
- Added clearer layouts for Temperatures, System info and Assistant, plus a persistent terminal-style Console.
- Changed the default local MCP profile to **Local controlled**. Address-only clients retain per-tool permissions; **Free** remains available as an explicit choice.
- Fixed icon-cache timing, console shutdown, minimized-window UI replies and transient collector errors.

## Previously in 0.10.1

- Added clickable sorting to every GPU process column, with numeric comparisons for trust, CPU/GPU load, process memory and GPU memory across KiB, MiB and GiB values.
- Kept unavailable readings at the end of numeric sorts.

## Other changes since 0.9.0

- Integrated CyHWInfo for hardware inventory, GPU telemetry, GPU process counters and available advanced sensors, with graceful fallback when PawnIO cannot be accessed.
- Added a tree-based sensor view, GPU process subtab, per-process trust and usage bars, quick inspection and context actions.
- Added CPU, RAM, GPU and VRAM percentages to the Processes page and tightened the graph cards and sensor layout.
- Added current GPU temperature and total usage to GPU details; the GPU process shortcut is now beside the details heading.
- Rebuilt the Windows x64 installer and portable ZIP from the updated native application.

## Previously in 0.9.0

- Updated the Assistant and MCP integration to CyMCPSDK 0.7.2.
- Added four local MCP access profiles. **Free** is the default: clients connect to the loopback address without a token and can call every registered operation, including changes, without another confirmation step. Argument schemas and process identity checks still apply.
- Kept **Local controlled** for address-only access with per-tool permissions, **Token only** for token access to every operation, and **Protected** for token access with per-tool permissions.
- Saved explicit profile choices and migrated configurations without a selected profile to Free.
- Updated the CLI to start an HTTP MCP server in Free by default and connect to an open Free server using its address alone. The graphical application can start its local server with `--mcp-local PORT`.
- Rebuilt the Windows x64 installer and portable ZIP with these changes.

## Known limitations

- In Free, any process on the same computer can call the exposed operations, including process termination. Select Local controlled or Protected when those permissions are needed.
- The chat and stdio CLI retain their separate controlled permissions. A provider must support tool calls to use the Assistant tools.
- Interface commands require an open graphical instance; the standalone CLI has no interface to control.
- Protected processes may hide their executable path, icon or modules. CPU/GPU values and temperatures depend on Windows counters, hardware and drivers.
- Application history, startup-program management and service management are not included yet. CyWinTask does not replace Windows Task Manager or its shortcuts.
- Building from source requires access to the private CyUI, CyMCPSDK and CyHWInfo repositories; the published packages include their compiled components.

See the [hardware guide](docs/Hardware.fr.md) and the [Assistant, CLI and MCP guide](docs/CyMCPSDK.fr.md). The [0.11.0 release](https://github.com/MrMybal/CyWinTask/releases/tag/v0.11.0) provides the installer, portable ZIP and SHA-256 checksums.
