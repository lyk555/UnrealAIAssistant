# UnrealAIAssistant

![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.5-5c5c5c?logo=unrealengine)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B)
![Platform](https://img.shields.io/badge/Platform-Win64%20%7C%20Linux%20%7C%20Mac-0078D6)
![AI](https://img.shields.io/badge/AI-CodeX%20%7C%20Claude%20Code-d0845f)
![MCP](https://img.shields.io/badge/MCP-20%2B%20Tools-7a2cff)
![License](https://img.shields.io/badge/License-MIT-97CA00)

UnrealAIAssistant is a UE 5.5 editor plugin repository built around the `UnrealAIAssistant` plugin. It integrates AI coding backends directly into the Unreal Editor, currently supporting OpenAI CodeX and Claude Code, while exposing Unreal-specific tools for assets, Blueprints, characters, materials, viewport capture, scripting, and task-based automation.

This repository includes:

- The `UnrealAIAssistant` Unreal Engine editor plugin
- A local Node.js MCP bridge in `Resources/mcp-bridge`
- Editor automation tests for the MCP tool surface

## Highlights

- Dockable assistant panel inside Unreal Editor
- Backend switching between supported AI coding CLIs
- Quick Ask action for short prompts without leaving the editor
- Local MCP server hosted by the plugin on port `3000`
- MCP bridge that works with the supported backends and other MCP-compatible clients
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
- At least one supported backend CLI installed and available on `PATH` (`codex` or `claude`)
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
3. Make sure at least one supported backend CLI is installed and available on `PATH` (`codex` or `claude`).
4. Authenticate the backend you plan to use, for example `codex login` for CodeX or the normal `claude` sign-in flow for Claude Code.
5. Install MCP bridge dependencies if needed:

```bash
cd Resources/mcp-bridge
npm install
```

6. Rebuild the project or let Unreal generate and compile the plugin modules.

## How It Works

When the editor starts, the plugin:

- Registers an `assistant` tab in the editor
- Adds menu and toolbar entries for opening the panel
- Detects which supported backends are available locally
- Lets you switch between the available supported backends from the panel toolbar
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
3. Launch the editor and confirm the `assistant` toolbar entry is visible and the backend selector lists the installed CLI backends.

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

## License

MIT. See [LICENSE](LICENSE).