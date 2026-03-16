// Copyright Natali Caggiano. All Rights Reserved.

/**
 * REFACTORED: BlueprintUtils - Introspection Functions Only
 *
 * This file now only contains Blueprint introspection/serialization functions.
 * The implementation has been split into focused modules:
 *
 * - BlueprintLoader.cpp: Loading, validation, compilation
 * - BlueprintEditor.cpp: Variable/function management
 * - BlueprintGraphEditor.cpp: Node/connection management
 */

#include "BlueprintUtils.h"
#include "UnrealCodexModule.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Event.h"
#include "EdGraph/EdGraph.h"

// ===== Blueprint Introspection =====

FString FBlueprintUtils::GetBlueprintTypeString(EBlueprintType Type)
{
	switch (Type)
	{
	case BPTYPE_Normal:
		return TEXT("Normal");
	case BPTYPE_Const:
		return TEXT("Const");
	case BPTYPE_MacroLibrary:
		return TEXT("MacroLibrary");
	case BPTYPE_Interface:
		return TEXT("Interface");
	case BPTYPE_LevelScript:
		return TEXT("LevelScript");
	case BPTYPE_FunctionLibrary:
		return TEXT("FunctionLibrary");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> FBlueprintUtils::SerializeBlueprintInfo(
	UBlueprint* Blueprint,
	bool bIncludeVariables,
	bool bIncludeFunctions,
	bool bIncludeGraphs)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!Blueprint)
	{
		return Result;
	}

	// Basic info
	Result->SetStringField(TEXT("name"), Blueprint->GetName());
	Result->SetStringField(TEXT("path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("blueprint_type"), GetBlueprintTypeString(Blueprint->BlueprintType));

	// Parent class
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetPathName());
		Result->SetStringField(TEXT("parent_class_name"), Blueprint->ParentClass->GetName());
	}

	// Generated class
	if (Blueprint->GeneratedClass)
	{
		Result->SetStringField(TEXT("generated_class"), Blueprint->GeneratedClass->GetPathName());
	}

	// Variables
	if (bIncludeVariables)
	{
		Result->SetArrayField(TEXT("variables"), GetBlueprintVariables(Blueprint));
	}

	// Functions
	if (bIncludeFunctions)
	{
		Result->SetArrayField(TEXT("functions"), GetBlueprintFunctions(Blueprint));
	}

	// Graphs
	if (bIncludeGraphs)
	{
		Result->SetObjectField(TEXT("graph_info"), GetGraphInfo(Blueprint));
	}

	return Result;
}

TArray<TSharedPtr<FJsonValue>> FBlueprintUtils::GetBlueprintVariables(UBlueprint* Blueprint)
{
	TArray<TSharedPtr<FJsonValue>> Variables;

	if (!Blueprint)
	{
		return Variables;
	}

	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), FBlueprintEditor::PinTypeToString(Var.VarType));
		VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
		VarObj->SetBoolField(TEXT("is_instance_editable"), (Var.PropertyFlags & CPF_Edit) != 0);
		VarObj->SetBoolField(TEXT("is_blueprint_read_only"), (Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);
		VarObj->SetBoolField(TEXT("is_exposed_on_spawn"), (Var.PropertyFlags & CPF_ExposeOnSpawn) != 0);

		// Default value if available
		if (!Var.DefaultValue.IsEmpty())
		{
			VarObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
		}

		Variables.Add(MakeShared<FJsonValueObject>(VarObj));
	}

	return Variables;
}

TArray<TSharedPtr<FJsonValue>> FBlueprintUtils::GetBlueprintFunctions(UBlueprint* Blueprint)
{
	TArray<TSharedPtr<FJsonValue>> Functions;

	if (!Blueprint)
	{
		return Functions;
	}

	// Get function graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Graph->GetName());
		FuncObj->SetStringField(TEXT("type"), TEXT("Function"));

		// Try to get the function entry node for more info
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
			{
				// Get input parameters
				TArray<TSharedPtr<FJsonValue>> Inputs;
				for (UEdGraphPin* Pin : EntryNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && !Pin->PinType.PinCategory.IsNone())
					{
						// Skip exec pins
						if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
						{
							continue;
						}

						TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
						ParamObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
						ParamObj->SetStringField(TEXT("type"), FBlueprintEditor::PinTypeToString(Pin->PinType));
						Inputs.Add(MakeShared<FJsonValueObject>(ParamObj));
					}
				}
				FuncObj->SetArrayField(TEXT("inputs"), Inputs);
				break;
			}
		}

		// Get output parameters from function result node
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
			{
				TArray<TSharedPtr<FJsonValue>> Outputs;
				for (UEdGraphPin* Pin : ResultNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Input && !Pin->PinType.PinCategory.IsNone())
					{
						// Skip exec pins
						if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
						{
							continue;
						}

						TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
						ParamObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
						ParamObj->SetStringField(TEXT("type"), FBlueprintEditor::PinTypeToString(Pin->PinType));
						Outputs.Add(MakeShared<FJsonValueObject>(ParamObj));
					}
				}
				FuncObj->SetArrayField(TEXT("outputs"), Outputs);
				break;
			}
		}

		Functions.Add(MakeShared<FJsonValueObject>(FuncObj));
	}

	// Add event graphs info
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph)
		{
			continue;
		}

		// Count event nodes
		int32 EventCount = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Cast<UK2Node_Event>(Node))
			{
				EventCount++;
			}
		}

		if (EventCount > 0)
		{
			TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
			GraphObj->SetStringField(TEXT("name"), Graph->GetName());
			GraphObj->SetStringField(TEXT("type"), TEXT("EventGraph"));
			GraphObj->SetNumberField(TEXT("event_count"), EventCount);
			GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
			Functions.Add(MakeShared<FJsonValueObject>(GraphObj));
		}
	}

	return Functions;
}

TSharedPtr<FJsonObject> FBlueprintUtils::GetGraphInfo(UBlueprint* Blueprint)
{
	return GetGraphInfo(Blueprint, false);
}

TSharedPtr<FJsonObject> FBlueprintUtils::GetGraphInfo(
	UBlueprint* Blueprint,
	bool bIncludeNodeDetails,
	const FString& TargetGraphName,
	bool bFunctionGraph)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!Blueprint)
	{
		return Result;
	}

	int32 TotalNodes = 0;
	int32 TotalEvents = 0;
	TArray<TSharedPtr<FJsonValue>> GraphNames;
	TArray<TSharedPtr<FJsonValue>> Graphs;

	auto AddGraphInfo = [&](UEdGraph* Graph, const FString& GraphType)
	{
		if (!Graph)
		{
			return;
		}

		if (!TargetGraphName.IsEmpty() && !Graph->GetName().Equals(TargetGraphName, ESearchCase::IgnoreCase))
		{
			return;
		}

		int32 EventCount = 0;
		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			if (Cast<UK2Node_Event>(Node))
			{
				EventCount++;
			}

			if (bIncludeNodeDetails)
			{
				Nodes.Add(MakeShared<FJsonValueObject>(FBlueprintGraphEditor::SerializeNodeInfo(Node)));
			}
		}

		TotalNodes += Graph->Nodes.Num();
		TotalEvents += EventCount;
		GraphNames.Add(MakeShared<FJsonValueString>(Graph->GetName()));

		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("graph_type"), GraphType);
		GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		GraphObj->SetNumberField(TEXT("event_count"), EventCount);
		if (bIncludeNodeDetails)
		{
			GraphObj->SetArrayField(TEXT("nodes"), Nodes);
		}
		Graphs.Add(MakeShared<FJsonValueObject>(GraphObj));
	};

	if (TargetGraphName.IsEmpty())
	{
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			AddGraphInfo(Graph, TEXT("EventGraph"));
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			AddGraphInfo(Graph, TEXT("FunctionGraph"));
		}
	}
	else
	{
		auto& GraphCollection = bFunctionGraph ? Blueprint->FunctionGraphs : Blueprint->UbergraphPages;
		for (UEdGraph* Graph : GraphCollection)
		{
			AddGraphInfo(Graph, bFunctionGraph ? TEXT("FunctionGraph") : TEXT("EventGraph"));
		}
	}

	Result->SetNumberField(TEXT("total_nodes"), TotalNodes);
	Result->SetNumberField(TEXT("total_events"), TotalEvents);
	Result->SetNumberField(TEXT("function_count"), Blueprint->FunctionGraphs.Num());
	Result->SetArrayField(TEXT("graph_names"), GraphNames);
	Result->SetArrayField(TEXT("graphs"), Graphs);
	Result->SetBoolField(TEXT("includes_node_details"), bIncludeNodeDetails);
	if (!TargetGraphName.IsEmpty())
	{
		Result->SetStringField(TEXT("requested_graph_name"), TargetGraphName);
		Result->SetBoolField(TEXT("requested_function_graph"), bFunctionGraph);
	}

	return Result;
}

