// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Query Blueprint information (read-only operations)
 *
 * Operations:
 *   - list: List all Blueprints in project (with optional filters)
 *   - inspect: Get detailed Blueprint info (variables, functions, parent class)
 *   - get_graph: Get graph information (optionally with full node topology)
 *   - capabilities: List supported Blueprint query/modify capabilities
 */
class FMCPTool_BlueprintQuery : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("blueprint_query");
		Info.Description = TEXT(
			"Query Blueprint information (read-only).\n\n"
			"Operations:\n"
			"- 'list': Find Blueprints in project with optional filters\n"
			"- 'inspect': Get detailed Blueprint info (variables, functions, parent class)\n"
			"- 'get_graph': Get graph structure (summary or full node topology)\n"
			"- 'capabilities': List supported Blueprint node types, workflow, and limitations\n\n"
			"Use 'list' first to discover Blueprints, then 'inspect' or 'get_graph' for details.\n\n"
			"Example paths:\n"
			"- '/Game/Blueprints/BP_Character'\n"
			"- '/Game/UI/WBP_MainMenu'\n"
			"- '/Game/Characters/ABP_Hero' (Animation Blueprint)\n\n"
			"Returns: Blueprint metadata, variables, functions, and/or graph structure."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("Operation: 'list', 'inspect', 'get_graph', or 'capabilities'"), true),
			FMCPToolParameter(TEXT("path_filter"), TEXT("string"),
				TEXT("Path prefix filter (e.g., '/Game/Blueprints/')"), false, TEXT("/Game/")),
			FMCPToolParameter(TEXT("type_filter"), TEXT("string"),
				TEXT("Blueprint type filter: 'Actor', 'Object', 'Widget', 'AnimBlueprint', etc."), false),
			FMCPToolParameter(TEXT("name_filter"), TEXT("string"),
				TEXT("Name substring filter"), false),
			FMCPToolParameter(TEXT("limit"), TEXT("number"),
				TEXT("Maximum results to return (1-1000, default: 25)"), false, TEXT("25")),
			FMCPToolParameter(TEXT("blueprint_path"), TEXT("string"),
				TEXT("Full Blueprint asset path (required for inspect/get_graph)"), false),
			FMCPToolParameter(TEXT("include_variables"), TEXT("boolean"),
				TEXT("Include variable list in inspect result (default: false)"), false, TEXT("false")),
			FMCPToolParameter(TEXT("include_functions"), TEXT("boolean"),
				TEXT("Include function list in inspect result (default: false)"), false, TEXT("false")),
			FMCPToolParameter(TEXT("include_graphs"), TEXT("boolean"),
				TEXT("Include graph info in inspect result"), false, TEXT("false")),
			FMCPToolParameter(TEXT("graph_name"), TEXT("string"),
				TEXT("Specific graph name for get_graph (defaults to all relevant graphs)"), false),
			FMCPToolParameter(TEXT("is_function_graph"), TEXT("boolean"),
				TEXT("Search function graphs instead of event graphs when graph_name is provided"), false, TEXT("false")),
			FMCPToolParameter(TEXT("include_nodes"), TEXT("boolean"),
				TEXT("Include detailed node and pin topology in get_graph (default: true)"), false, TEXT("true"))
		};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	/** List Blueprints matching filters */
	FMCPToolResult ExecuteList(const TSharedRef<FJsonObject>& Params);

	/** Get detailed Blueprint info */
	FMCPToolResult ExecuteInspect(const TSharedRef<FJsonObject>& Params);

	/** Get graph information */
	FMCPToolResult ExecuteGetGraph(const TSharedRef<FJsonObject>& Params);

	/** List supported Blueprint operations and node capabilities */
	FMCPToolResult ExecuteCapabilities(const TSharedRef<FJsonObject>& Params);
};
