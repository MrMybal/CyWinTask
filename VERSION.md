# CyWinTask 0.9.0

[Français](VERSION.fr.md) · [README](README.md) · [Published release](https://github.com/MrMybal/CyWinTask/releases/tag/v0.9.0)

**Stage: in development.** This release contains the native Windows x64 application, its collector and the standalone CLI.

## What changed in 0.9.0

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
- Building from source requires access to the private CyUI and CyMCPSDK repositories; the published packages include their compiled components.

See the [Assistant, CLI and MCP guide](docs/CyMCPSDK.fr.md) for profiles, commands and connections. The [0.9.0 release](https://github.com/MrMybal/CyWinTask/releases/tag/v0.9.0) provides the installer, portable ZIP and SHA-256 checksums.
