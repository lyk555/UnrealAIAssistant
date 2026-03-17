# UnrealAIAssistant

UnrealAIAssistant is a UE 5.5 editor plugin repository built around the `UnrealAIAssistant` plugin. It brings Codex CLI into the Unreal Editor, starts a local MCP server inside the editor, and exposes Unreal-specific tools for assets, Blueprints, characters, materials, viewport capture, scripting, and task-based automation.

This repository includes:

- The `UnrealAIAssistant` Unreal Engine editor plugin
- A local Node.js MCP bridge in `Resources/mcp-bridge`
- Editor automation tests for the MCP tool surface

## Highlights

- Dockable Codex panel inside Unreal Editor
- Quick Ask action for short prompts without leaving the editor
- Local MCP server hosted by the plugin on port `3000`
- MCP bridge that works with Codex CLI and other MCP-compatible clients
- Tool coverage for actors, levels, assets, Blueprints, animation Blueprints, materials, Enhanced Input, scripting, viewport capture, and async task management
- Built-in validation around tool parameters and script execution

## Repository Layout

```text
.
|- Config/
|- Resources/
|  \- mcp-bridge/        # Node.js MCP bridge
|- Source/
|  \- UnrealAIAssistant/       # UE editor module
|- UnrealAIAssistant.uplugin
|- LICENSE
\- README.md
```

## Requirements

- Unreal Engine `5.5.x`
- Codex CLI installed and authenticated
- Node.js `18+` for the MCP bridge
- A C++ Unreal project if you want to build from source

## Installation

Clone or copy this repository into your Unreal project as:

```text
<YourProject>/Plugins/UnrealAIAssistant
```

Then:

1. Open the project in Unreal Editor.
2. Enable the `UnrealAIAssistant` plugin if it is not already enabled.
3. Make sure Codex CLI is installed and available on `PATH`.
4. Run `codex login`.
5. Install MCP bridge dependencies if needed:

```bash
cd Resources/mcp-bridge
npm install
```

6. Rebuild the project or let Unreal generate and compile the plugin modules.

## How It Works

When the editor starts, the plugin:

- Registers a `Codex Assistant` tab in the editor
- Adds menu and toolbar entries for opening the panel
- Detects whether Codex CLI is available locally
- Starts an in-editor MCP server on `http://localhost:3000`

The included `.mcp.json` points MCP clients to the local bridge:

```json
{
  "mcpServers": {
    "UnrealAIAssistant": {
      "command": "node",
      "args": ["Resources/mcp-bridge/index.js"],
      "env": {
        "UNREAL_MCP_URL": "http://localhost:3000"
      }
    }
  }
}
```

## Tool Surface

The plugin currently registers MCP tools in these areas:

- Actor workflows: spawn, move, delete, list, set properties
- Level workflows: open level, inspect level actors
- Blueprint workflows: query and modify Blueprints
- Animation workflows: modify Animation Blueprints
- Asset workflows: search assets, inspect dependencies, inspect referencers, edit assets
- Material workflows: material inspection and editing helpers
- Character workflows: character setup and character data helpers
- Utility workflows: run console commands, fetch output log, execute scripts, clean script history, capture viewport
- Task workflows: submit, inspect, list, fetch results, and cancel long-running MCP tasks

## Development

### Unreal module

The main editor module lives in:

```text
Source/UnrealAIAssistant
```

Key areas:

- `Private/MCP/` for server, registry, validation, and task queue code
- `Private/MCP/Tools/` for individual tool implementations
- `Private/Tests/` for automation coverage
- `Resources/mcp-bridge/` for the Node.js bridge

### Build

Typical local workflow:

1. Generate project files if needed.
2. Build the Unreal project from Visual Studio or Rider.
3. Launch the editor and confirm the `Codex` toolbar button is visible.

### Tests

In the Unreal Editor console, run:

```text
Automation RunTests UnrealAIAssistant
```

For the bridge:

```bash
cd Resources/mcp-bridge
npm test
```

## Notes

- The plugin metadata targets Unreal Engine `5.5.0`.
- The default MCP server port is `3000`.
- `Binaries/`, `Intermediate/`, and bridge `node_modules/` are excluded from version control.

## Credits

This project is based on `UnrealAIAssistant` by Natali Caggiano, with this repository packaging and publishing the plugin for the `UnrealAIAssistant` distribution.

## License

MIT. See [LICENSE](LICENSE).