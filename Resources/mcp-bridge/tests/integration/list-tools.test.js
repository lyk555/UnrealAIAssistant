/**
 * Integration tests for the ListTools handler logic.
 *
 * These tests replicate the current Codex-facing behavior from index.js:
 * expose the raw Unreal tool names, keep unreal_status first, and add
 * get_ue_context as the final helper tool.
 */

import { describe, it, expect, vi, beforeEach } from "vitest";
import {
  fetchUnrealTools,
  checkUnrealConnection,
  convertToMCPSchema,
  convertAnnotations,
} from "../../lib.js";
import {
  installFetchMock,
  installFetchReject,
} from "../helpers/mock-fetch.js";
import {
  UNREAL_STATUS_RESPONSE,
  UNREAL_TOOLS_RESPONSE,
} from "../helpers/fixtures.js";

vi.mock("fs", () => ({
  readFileSync: vi.fn(() => "# Mock Context"),
  existsSync: vi.fn(() => true),
}));

import { listCategories } from "../../context-loader.js";

const BASE_URL = "http://localhost:3000";
const TIMEOUT_MS = 5000;
const DEFAULT_TTL_MS = 30000;

let toolCache = { tools: [], timestamp: 0 };

beforeEach(() => {
  vi.unstubAllGlobals();
  toolCache = { tools: [], timestamp: 0 };
});

async function simulateListTools({ toolCacheTtlMs = DEFAULT_TTL_MS } = {}) {
  const status = await checkUnrealConnection(BASE_URL, TIMEOUT_MS);

  if (!status.connected) {
    return {
      tools: [
        {
          name: "unreal_status",
          description:
            "Check if Unreal Editor is running with the plugin. Currently: NOT CONNECTED. Please start Unreal Editor with the plugin enabled.",
          inputSchema: { type: "object", properties: {} },
        },
      ],
    };
  }

  let unrealTools;
  const cacheAge = Date.now() - toolCache.timestamp;
  if (toolCache.tools.length > 0 && cacheAge < toolCacheTtlMs) {
    unrealTools = toolCache.tools;
  } else {
    unrealTools = await fetchUnrealTools(BASE_URL, TIMEOUT_MS);
    toolCache = { tools: unrealTools, timestamp: Date.now() };
  }

  const mcpTools = [];

  mcpTools.push({
    name: "unreal_status",
    description: `Check Unreal Editor connection status. Currently: CONNECTED to ${status.projectName || "Unknown Project"} (${status.engineVersion || "Unknown"})`,
    inputSchema: { type: "object", properties: {} },
    annotations: {
      readOnlyHint: true,
      destructiveHint: false,
      idempotentHint: true,
      openWorldHint: false,
    },
  });

  for (const tool of unrealTools) {
    mcpTools.push({
      name: tool.name,
      description: tool.description,
      inputSchema: convertToMCPSchema(tool.parameters, true),
      annotations: convertAnnotations(tool.annotations),
    });
  }

  mcpTools.push({
    name: "get_ue_context",
    description: `Get Unreal Engine 5.5 API context/documentation. Categories: ${listCategories().join(", ")}.`,
    inputSchema: {
      type: "object",
      properties: {
        category: { type: "string" },
        query: { type: "string" },
      },
    },
    annotations: {
      readOnlyHint: true,
      destructiveHint: false,
      idempotentHint: true,
      openWorldHint: false,
    },
  });

  return { tools: mcpTools };
}

describe("ListTools - disconnected", () => {
  it("returns only unreal_status when Unreal is not connected", async () => {
    installFetchReject(new Error("ECONNREFUSED"));
    const result = await simulateListTools();
    expect(result.tools).toHaveLength(1);
    expect(result.tools[0].name).toBe("unreal_status");
    expect(result.tools[0].description).toContain("NOT CONNECTED");
  });
});

describe("ListTools - connected", () => {
  beforeEach(() => {
    installFetchMock([
      { pattern: "/mcp/status", body: UNREAL_STATUS_RESPONSE },
      { pattern: "/mcp/tools", body: UNREAL_TOOLS_RESPONSE },
    ]);
  });

  it("puts unreal_status first", async () => {
    const result = await simulateListTools();
    expect(result.tools[0].name).toBe("unreal_status");
  });

  it("includes project info in the status description", async () => {
    const result = await simulateListTools();
    expect(result.tools[0].description).toContain("CONNECTED");
    expect(result.tools[0].description).toContain("MyGame");
  });

  it("exposes raw Unreal tool names", async () => {
    const result = await simulateListTools();
    const spawnTool = result.tools.find((t) => t.name === "spawn_actor");
    expect(spawnTool).toBeDefined();
    expect(spawnTool.description).toBe("Spawn an actor in the current level");
  });

  it("puts get_ue_context last", async () => {
    const result = await simulateListTools();
    const last = result.tools[result.tools.length - 1];
    expect(last.name).toBe("get_ue_context");
  });

  it("returns raw tools + status + context", async () => {
    const result = await simulateListTools();
    expect(result.tools).toHaveLength(UNREAL_TOOLS_RESPONSE.tools.length + 2);
  });

  it("converts tool parameters to MCP inputSchema", async () => {
    const result = await simulateListTools();
    const spawnTool = result.tools.find((t) => t.name === "spawn_actor");
    expect(spawnTool.inputSchema.type).toBe("object");
    expect(spawnTool.inputSchema.properties.class_name.type).toBe("string");
    expect(spawnTool.inputSchema.required).toContain("class_name");
  });

  it("converts tool annotations", async () => {
    const result = await simulateListTools();
    const statusTool = result.tools.find((t) => t.name === "unreal_status");
    expect(statusTool.annotations.readOnlyHint).toBe(true);
    expect(statusTool.annotations.destructiveHint).toBe(false);
  });
});

describe("ListTools - empty tool list from Unreal", () => {
  it("returns status + context when Unreal has no tools", async () => {
    installFetchMock([
      { pattern: "/mcp/status", body: UNREAL_STATUS_RESPONSE },
      { pattern: "/mcp/tools", body: { tools: [] } },
    ]);
    const result = await simulateListTools();
    expect(result.tools).toHaveLength(2);
    expect(result.tools[0].name).toBe("unreal_status");
    expect(result.tools[1].name).toBe("get_ue_context");
  });
});

describe("ListTools - TTL cache", () => {
  it("uses cached tools on the second call within TTL", async () => {
    const spy = installFetchMock([
      { pattern: "/mcp/status", body: UNREAL_STATUS_RESPONSE },
      { pattern: "/mcp/tools", body: UNREAL_TOOLS_RESPONSE },
    ]);

    await simulateListTools();
    const toolsFetchCount1 = spy.mock.calls.filter((c) => c[0].includes("/mcp/tools")).length;
    expect(toolsFetchCount1).toBe(1);

    await simulateListTools();
    const toolsFetchCount2 = spy.mock.calls.filter((c) => c[0].includes("/mcp/tools")).length;
    expect(toolsFetchCount2).toBe(1);
  });

  it("re-fetches tools after the cache expires", async () => {
    const spy = installFetchMock([
      { pattern: "/mcp/status", body: UNREAL_STATUS_RESPONSE },
      { pattern: "/mcp/tools", body: UNREAL_TOOLS_RESPONSE },
    ]);

    await simulateListTools({ toolCacheTtlMs: 1 });
    await new Promise((resolve) => setTimeout(resolve, 10));
    await simulateListTools({ toolCacheTtlMs: 1 });

    const toolsFetchCount = spy.mock.calls.filter((c) => c[0].includes("/mcp/tools")).length;
    expect(toolsFetchCount).toBe(2);
  });

  it("still checks connection on every call even with cached tools", async () => {
    const spy = installFetchMock([
      { pattern: "/mcp/status", body: UNREAL_STATUS_RESPONSE },
      { pattern: "/mcp/tools", body: UNREAL_TOOLS_RESPONSE },
    ]);

    await simulateListTools();
    await simulateListTools();

    const statusFetchCount = spy.mock.calls.filter((c) => c[0].includes("/mcp/status")).length;
    expect(statusFetchCount).toBe(2);
  });
});

describe("ListTools - raw tool exposure", () => {
  beforeEach(() => {
    installFetchMock([
      { pattern: "/mcp/status", body: UNREAL_STATUS_RESPONSE },
      { pattern: "/mcp/tools", body: UNREAL_TOOLS_RESPONSE },
    ]);
  });

  it("includes modifying tools directly", async () => {
    const result = await simulateListTools();
    expect(result.tools.find((t) => t.name === "spawn_actor")).toBeDefined();
    expect(result.tools.find((t) => t.name === "blueprint_modify")).toBeDefined();
  });

  it("includes task and script tools directly", async () => {
    const result = await simulateListTools();
    expect(result.tools.find((t) => t.name === "task_submit")).toBeDefined();
    expect(result.tools.find((t) => t.name === "execute_script")).toBeDefined();
  });
});
