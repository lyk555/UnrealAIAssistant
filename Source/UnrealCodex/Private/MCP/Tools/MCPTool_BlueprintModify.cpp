// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_BlueprintModify.h"
#include "BlueprintUtils.h"
#include "MCP/MCPParamValidator.h"
#include "MCP/MCPBlueprintLoadContext.h"
#include "UnrealCodexModule.h"
#include "Engine/Blueprint.h"

// Operation name constants
namespace BlueprintModifyOps
{
	static const FString Create = TEXT("create");
	static const FString AddVariable = TEXT("add_variable");
	static const FString RemoveVariable = TEXT("remove_variable");
	static const FString AddFunction = TEXT("add_function");
	static const FString RemoveFunction = TEXT("remove_function");
	static const FString AddNode = TEXT("add_node");
	static const FString AddNodes = TEXT("add_nodes");
	static const FString DeleteNode = TEXT("delete_node");
	static const FString ConnectPins = TEXT("connect_pins");
	static const FString DisconnectPins = TEXT("disconnect_pins");
	static const FString SetPinValue = TEXT("set_pin_value");
}
namespace
{
FString ResolvePinName(UEdGraphNode* Node, const FString& PinNameHint, bool bOutput)
{
	if (!Node)
	{
		return PinNameHint;
	}

	UEdGraphPin* Pin = nullptr;
	if (PinNameHint.IsEmpty())
	{
		Pin = FBlueprintUtils::GetExecPin(Node, bOutput);
	}
	else
	{
		Pin = FBlueprintUtils::FindPinByName(Node, PinNameHint, bOutput ? EGPD_Output : EGPD_Input);
		if (!Pin)
		{
			Pin = FBlueprintUtils::FindPinByName(Node, PinNameHint, bOutput ? EGPD_Input : EGPD_Output);
		}
	}

	if (Pin)
	{
		return Pin->PinName.ToString();
	}

	return PinNameHint.IsEmpty() ? TEXT("(auto exec)") : PinNameHint;
}

TSharedPtr<FJsonObject> BuildConnectionRecord(
	UEdGraph* Graph,
	const FString& SourceNodeId,
	const FString& SourcePinHint,
	const FString& TargetNodeId,
	const FString& TargetPinHint,
	int32 ConnectionIndex)
{
	TSharedPtr<FJsonObject> ConnectionObj = MakeShared<FJsonObject>();
	ConnectionObj->SetNumberField(TEXT("index"), ConnectionIndex);
	ConnectionObj->SetStringField(TEXT("source_node_id"), SourceNodeId);
	ConnectionObj->SetStringField(TEXT("target_node_id"), TargetNodeId);

	UEdGraphNode* SourceNode = Graph ? FBlueprintUtils::FindNodeById(Graph, SourceNodeId) : nullptr;
	UEdGraphNode* TargetNode = Graph ? FBlueprintUtils::FindNodeById(Graph, TargetNodeId) : nullptr;
	ConnectionObj->SetStringField(TEXT("resolved_source_pin"), ResolvePinName(SourceNode, SourcePinHint, true));
	ConnectionObj->SetStringField(TEXT("resolved_target_pin"), ResolvePinName(TargetNode, TargetPinHint, false));

	if (SourceNode)
	{
		ConnectionObj->SetObjectField(TEXT("source_node"), FBlueprintUtils::SerializeNodeInfo(SourceNode));
	}
	if (TargetNode)
	{
		ConnectionObj->SetObjectField(TEXT("target_node"), FBlueprintUtils::SerializeNodeInfo(TargetNode));
	}

	return ConnectionObj;
}

TSharedPtr<FJsonObject> BuildConnectionErrorRecord(
	int32 ConnectionIndex,
	const FString& ErrorMessage,
	const FString& SourceNodeId,
	const FString& SourcePin,
	const FString& TargetNodeId,
	const FString& TargetPin)
{
	TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
	ErrorObj->SetNumberField(TEXT("index"), ConnectionIndex);
	ErrorObj->SetStringField(TEXT("error"), ErrorMessage);
	ErrorObj->SetStringField(TEXT("source_node_id"), SourceNodeId);
	ErrorObj->SetStringField(TEXT("source_pin"), SourcePin);
	ErrorObj->SetStringField(TEXT("target_node_id"), TargetNodeId);
	ErrorObj->SetStringField(TEXT("target_pin"), TargetPin);
	return ErrorObj;
}
}

FMCPToolResult FMCPTool_BlueprintModify::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Get operation type
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	// Level 2: Variable/Function Operations
	if (Operation == BlueprintModifyOps::Create)
	{
		return ExecuteCreate(Params);
	}
	if (Operation == BlueprintModifyOps::AddVariable)
	{
		return ExecuteAddVariable(Params);
	}
	if (Operation == BlueprintModifyOps::RemoveVariable)
	{
		return ExecuteRemoveVariable(Params);
	}
	if (Operation == BlueprintModifyOps::AddFunction)
	{
		return ExecuteAddFunction(Params);
	}
	if (Operation == BlueprintModifyOps::RemoveFunction)
	{
		return ExecuteRemoveFunction(Params);
	}
	// Level 3: Node Operations
	if (Operation == BlueprintModifyOps::AddNode)
	{
		return ExecuteAddNode(Params);
	}
	if (Operation == BlueprintModifyOps::AddNodes)
	{
		return ExecuteAddNodes(Params);
	}
	if (Operation == BlueprintModifyOps::DeleteNode)
	{
		return ExecuteDeleteNode(Params);
	}
	// Level 4: Connection Operations
	if (Operation == BlueprintModifyOps::ConnectPins)
	{
		return ExecuteConnectPins(Params);
	}
	if (Operation == BlueprintModifyOps::DisconnectPins)
	{
		return ExecuteDisconnectPins(Params);
	}
	if (Operation == BlueprintModifyOps::SetPinValue)
	{
		return ExecuteSetPinValue(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid: create, add_variable, remove_variable, add_function, remove_function, add_node, add_nodes, delete_node, connect_pins, disconnect_pins, set_pin_value"),
		*Operation));
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteCreate(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	FString PackagePath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("package_path"), PackagePath, Error))
	{
		return Error.GetValue();
	}

	FString BlueprintName;
	if (!ExtractRequiredString(Params, TEXT("blueprint_name"), BlueprintName, Error))
	{
		return Error.GetValue();
	}

	FString ParentClassName;
	if (!ExtractRequiredString(Params, TEXT("parent_class"), ParentClassName, Error))
	{
		return Error.GetValue();
	}

	FString BlueprintTypeStr = ExtractOptionalString(Params, TEXT("blueprint_type"), TEXT("Normal"));

	// Validate package path
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(PackagePath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Validate Blueprint name
	if (!FMCPParamValidator::ValidateBlueprintVariableName(BlueprintName, ValidationError))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Invalid Blueprint name: %s"), *ValidationError));
	}

	// Find parent class
	FString ClassError;
	UClass* ParentClass = FBlueprintUtils::FindParentClass(ParentClassName, ClassError);
	if (!ParentClass)
	{
		return FMCPToolResult::Error(ClassError);
	}

	// Parse Blueprint type
	EBlueprintType BlueprintType = ParseBlueprintType(BlueprintTypeStr);

	// Create the Blueprint
	FString CreateError;
	UBlueprint* NewBlueprint = FBlueprintUtils::CreateBlueprint(
		PackagePath,
		BlueprintName,
		ParentClass,
		BlueprintType,
		CreateError
	);

	if (!NewBlueprint)
	{
		return FMCPToolResult::Error(CreateError);
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), NewBlueprint->GetName());
	ResultData->SetStringField(TEXT("blueprint_path"), NewBlueprint->GetPathName());
	ResultData->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	ResultData->SetStringField(TEXT("blueprint_type"), FBlueprintUtils::GetBlueprintTypeString(BlueprintType));
	ResultData->SetBoolField(TEXT("compiled"), true);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created Blueprint: %s"), *NewBlueprint->GetPathName()),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddVariable(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString VariableName;
	if (!ExtractRequiredString(Params, TEXT("variable_name"), VariableName, Error))
	{
		return Error.GetValue();
	}

	FString VariableType;
	if (!ExtractRequiredString(Params, TEXT("variable_type"), VariableType, Error))
	{
		return Error.GetValue();
	}

	// Validate variable name
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintVariableName(VariableName, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Parse variable type
	FEdGraphPinType PinType;
	FString TypeError;
	if (!FBlueprintUtils::ParsePinType(VariableType, PinType, TypeError))
	{
		return FMCPToolResult::Error(TypeError);
	}

	// Add the variable
	FString AddError;
	if (!FBlueprintUtils::AddVariable(Context.Blueprint, VariableName, PinType, AddError))
	{
		return FMCPToolResult::Error(AddError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Variable added")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);
	ResultData->SetStringField(TEXT("variable_type"), VariableType);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added variable '%s' (%s) to Blueprint"), *VariableName, *VariableType),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteRemoveVariable(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString VariableName;
	if (!ExtractRequiredString(Params, TEXT("variable_name"), VariableName, Error))
	{
		return Error.GetValue();
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Remove the variable
	FString RemoveError;
	if (!FBlueprintUtils::RemoveVariable(Context.Blueprint, VariableName, RemoveError))
	{
		return FMCPToolResult::Error(RemoveError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Variable removed")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed variable '%s' from Blueprint"), *VariableName),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddFunction(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString FunctionName;
	if (!ExtractRequiredString(Params, TEXT("function_name"), FunctionName, Error))
	{
		return Error.GetValue();
	}

	// Validate function name
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintFunctionName(FunctionName, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Add the function
	FString AddError;
	if (!FBlueprintUtils::AddFunction(Context.Blueprint, FunctionName, AddError))
	{
		return FMCPToolResult::Error(AddError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Function added")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("function_name"), FunctionName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added function '%s' to Blueprint"), *FunctionName),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteRemoveFunction(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString FunctionName;
	if (!ExtractRequiredString(Params, TEXT("function_name"), FunctionName, Error))
	{
		return Error.GetValue();
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Remove the function
	FString RemoveError;
	if (!FBlueprintUtils::RemoveFunction(Context.Blueprint, FunctionName, RemoveError))
	{
		return FMCPToolResult::Error(RemoveError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Function removed")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("function_name"), FunctionName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed function '%s' from Blueprint"), *FunctionName),
		ResultData
	);
}

EBlueprintType FMCPTool_BlueprintModify::ParseBlueprintType(const FString& TypeString)
{
	FString LowerType = TypeString.ToLower();

	if (LowerType == TEXT("normal") || LowerType == TEXT("actor") || LowerType == TEXT("object"))
	{
		return BPTYPE_Normal;
	}
	if (LowerType == TEXT("functionlibrary") || LowerType == TEXT("function_library"))
	{
		return BPTYPE_FunctionLibrary;
	}
	if (LowerType == TEXT("interface"))
	{
		return BPTYPE_Interface;
	}
	if (LowerType == TEXT("macrolibrary") || LowerType == TEXT("macro_library") || LowerType == TEXT("macro"))
	{
		return BPTYPE_MacroLibrary;
	}

	// Default to normal
	return BPTYPE_Normal;
}

// ===== Level 3: Node Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddNode(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString NodeType;
	if (!ExtractRequiredString(Params, TEXT("node_type"), NodeType, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);
	int32 PosX = (int32)ExtractOptionalNumber(Params, TEXT("pos_x"), 0);
	int32 PosY = (int32)ExtractOptionalNumber(Params, TEXT("pos_y"), 0);

	// Get node params object
	TSharedPtr<FJsonObject> NodeParams;
	const TSharedPtr<FJsonObject>* NodeParamsPtr;
	if (Params->TryGetObjectField(TEXT("node_params"), NodeParamsPtr))
	{
		NodeParams = *NodeParamsPtr;
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Create the node
	FString NodeId;
	FString CreateError;
	UEdGraphNode* NewNode = FBlueprintUtils::CreateNode(Graph, NodeType, NodeParams, PosX, PosY, NodeId, CreateError);
	if (!NewNode)
	{
		return FMCPToolResult::Error(CreateError);
	}

	// Apply pin default values if provided
	if (NodeParams.IsValid())
	{
		const TSharedPtr<FJsonObject>* PinValuesPtr;
		if (NodeParams->TryGetObjectField(TEXT("pin_values"), PinValuesPtr))
		{
			for (const auto& PinValue : (*PinValuesPtr)->Values)
			{
				FString PinValueStr;
				if (PinValue.Value->TryGetString(PinValueStr))
				{
					FString PinError;
					FBlueprintUtils::SetPinDefaultValue(Graph, NodeId, PinValue.Key, PinValueStr, PinError);
				}
			}
		}
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Node created")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	if (UEdGraphNode* FinalNode = FBlueprintUtils::FindNodeById(Graph, NodeId))
	{
		ResultData->SetObjectField(TEXT("node"), FBlueprintUtils::SerializeNodeInfo(FinalNode));
	}
	ResultData->SetStringField(TEXT("node_id"), NodeId);
	ResultData->SetStringField(TEXT("blueprint_path"), Context.Blueprint->GetPathName());
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created node '%s' (type: %s)"), *NodeId, *NodeType),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddNodes(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Get nodes array
	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	if (!Params->TryGetArrayField(TEXT("nodes"), NodesArray))
	{
		return FMCPToolResult::Error(TEXT("'nodes' array is required"));
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Create all nodes using helper
	TArray<FString> CreatedNodeIds;
	TArray<TSharedPtr<FJsonValue>> CreatedNodes;
	FString CreateError;
	if (!CreateNodesFromSpec(Graph, *NodesArray, CreatedNodeIds, CreatedNodes, CreateError))
	{
		return FMCPToolResult::Error(CreateError);
	}

	// Process connections using helper
	TArray<TSharedPtr<FJsonValue>> AppliedConnections;
	TArray<TSharedPtr<FJsonValue>> ConnectionErrors;
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray;
	if (Params->TryGetArrayField(TEXT("connections"), ConnectionsArray))
	{
		ProcessNodeConnections(Graph, *ConnectionsArray, CreatedNodeIds, AppliedConnections, ConnectionErrors);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Nodes created")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
	ResultData->SetArrayField(TEXT("nodes"), CreatedNodes);
	ResultData->SetNumberField(TEXT("node_count"), CreatedNodeIds.Num());
	ResultData->SetArrayField(TEXT("applied_connections"), AppliedConnections);
	ResultData->SetArrayField(TEXT("connection_errors"), ConnectionErrors);
	ResultData->SetNumberField(TEXT("connection_count"), AppliedConnections.Num());
	ResultData->SetNumberField(TEXT("connection_error_count"), ConnectionErrors.Num());

	const FString Summary = ConnectionErrors.Num() > 0
		? FString::Printf(TEXT("Created %d nodes with %d connection issues"), CreatedNodeIds.Num(), ConnectionErrors.Num())
		: FString::Printf(TEXT("Created %d nodes and %d connections"), CreatedNodeIds.Num(), AppliedConnections.Num());

	return FMCPToolResult::Success(Summary, ResultData);
}

bool FMCPTool_BlueprintModify::CreateNodesFromSpec(
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>& NodesArray,
	TArray<FString>& OutCreatedNodeIds,
	TArray<TSharedPtr<FJsonValue>>& OutCreatedNodes,
	FString& OutError)
{
	for (int32 i = 0; i < NodesArray.Num(); i++)
	{
		const TSharedPtr<FJsonObject>* NodeSpec;
		if (!NodesArray[i]->TryGetObject(NodeSpec))
		{
			OutError = FString::Printf(TEXT("Node at index %d is not a valid object"), i);
			return false;
		}

		FString NodeType = (*NodeSpec)->GetStringField(TEXT("type"));
		if (NodeType.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Node at index %d missing 'type' field"), i);
			return false;
		}

		int32 PosX = (int32)(*NodeSpec)->GetNumberField(TEXT("pos_x"));
		int32 PosY = (int32)(*NodeSpec)->GetNumberField(TEXT("pos_y"));

		// Get params (could be inline or nested)
		TSharedPtr<FJsonObject> NodeParams = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* ParamsPtr;
		if ((*NodeSpec)->TryGetObjectField(TEXT("params"), ParamsPtr))
		{
			NodeParams = *ParamsPtr;
		}
		else
		{
			// Copy common fields to params
			if ((*NodeSpec)->HasField(TEXT("function")))
				NodeParams->SetStringField(TEXT("function"), (*NodeSpec)->GetStringField(TEXT("function")));
			if ((*NodeSpec)->HasField(TEXT("target_class")))
				NodeParams->SetStringField(TEXT("target_class"), (*NodeSpec)->GetStringField(TEXT("target_class")));
			if ((*NodeSpec)->HasField(TEXT("event")))
				NodeParams->SetStringField(TEXT("event"), (*NodeSpec)->GetStringField(TEXT("event")));
			if ((*NodeSpec)->HasField(TEXT("variable")))
				NodeParams->SetStringField(TEXT("variable"), (*NodeSpec)->GetStringField(TEXT("variable")));
			if ((*NodeSpec)->HasField(TEXT("num_outputs")))
				NodeParams->SetNumberField(TEXT("num_outputs"), (*NodeSpec)->GetNumberField(TEXT("num_outputs")));
		}

		// Create node
		FString NodeId;
		FString CreateError;
		UEdGraphNode* NewNode = FBlueprintUtils::CreateNode(Graph, NodeType, NodeParams, PosX, PosY, NodeId, CreateError);
		if (!NewNode)
		{
			OutError = FString::Printf(TEXT("Failed to create node %d: %s"), i, *CreateError);
			return false;
		}

		OutCreatedNodeIds.Add(NodeId);

		// Apply pin default values if provided
		const TSharedPtr<FJsonObject>* PinValuesPtr;
		if ((*NodeSpec)->TryGetObjectField(TEXT("pin_values"), PinValuesPtr))
		{
			for (const auto& PinValue : (*PinValuesPtr)->Values)
			{
				FString PinValueStr;
				if (PinValue.Value->TryGetString(PinValueStr))
				{
					FString PinError;
					FBlueprintUtils::SetPinDefaultValue(Graph, NodeId, PinValue.Key, PinValueStr, PinError);
				}
			}
		}

		// Add to result
		TSharedPtr<FJsonObject> NodeInfo = FBlueprintUtils::SerializeNodeInfo(NewNode);
		NodeInfo->SetNumberField(TEXT("index"), i);
		OutCreatedNodes.Add(MakeShared<FJsonValueObject>(NodeInfo));
	}

	return true;
}

void FMCPTool_BlueprintModify::ProcessNodeConnections(
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>& ConnectionsArray,
	const TArray<FString>& CreatedNodeIds,
	TArray<TSharedPtr<FJsonValue>>& OutAppliedConnections,
	TArray<TSharedPtr<FJsonValue>>& OutConnectionErrors)
{
	for (int32 i = 0; i < ConnectionsArray.Num(); i++)
	{
		const TSharedPtr<FJsonObject>* ConnSpec;
		if (!ConnectionsArray[i]->TryGetObject(ConnSpec))
		{
			OutConnectionErrors.Add(MakeShared<FJsonValueObject>(BuildConnectionErrorRecord(
				i,
				TEXT("Connection entry is not a valid object"),
				TEXT(""),
				TEXT(""),
				TEXT(""),
				TEXT(""))));
			continue;
		}

		FString SourceNodeId;
		if ((*ConnSpec)->HasTypedField<EJson::Number>(TEXT("from_node")))
		{
			int32 FromIndex = (int32)(*ConnSpec)->GetNumberField(TEXT("from_node"));
			if (FromIndex >= 0 && FromIndex < CreatedNodeIds.Num())
			{
				SourceNodeId = CreatedNodeIds[FromIndex];
			}
		}
		else if ((*ConnSpec)->HasTypedField<EJson::String>(TEXT("from_node")))
		{
			SourceNodeId = (*ConnSpec)->GetStringField(TEXT("from_node"));
		}

		FString TargetNodeId;
		if ((*ConnSpec)->HasTypedField<EJson::Number>(TEXT("to_node")))
		{
			int32 ToIndex = (int32)(*ConnSpec)->GetNumberField(TEXT("to_node"));
			if (ToIndex >= 0 && ToIndex < CreatedNodeIds.Num())
			{
				TargetNodeId = CreatedNodeIds[ToIndex];
			}
		}
		else if ((*ConnSpec)->HasTypedField<EJson::String>(TEXT("to_node")))
		{
			TargetNodeId = (*ConnSpec)->GetStringField(TEXT("to_node"));
		}

		FString SourcePin;
		(*ConnSpec)->TryGetStringField(TEXT("from_pin"), SourcePin);
		FString TargetPin;
		(*ConnSpec)->TryGetStringField(TEXT("to_pin"), TargetPin);

		if (SourceNodeId.IsEmpty() || TargetNodeId.IsEmpty())
		{
			OutConnectionErrors.Add(MakeShared<FJsonValueObject>(BuildConnectionErrorRecord(
				i,
				TEXT("Connection must resolve both source and target nodes"),
				SourceNodeId,
				SourcePin,
				TargetNodeId,
				TargetPin)));
			continue;
		}

		FString ConnectError;
		if (!FBlueprintUtils::ConnectPins(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, ConnectError))
		{
			OutConnectionErrors.Add(MakeShared<FJsonValueObject>(BuildConnectionErrorRecord(
				i,
				ConnectError,
				SourceNodeId,
				SourcePin,
				TargetNodeId,
				TargetPin)));
			continue;
		}

		OutAppliedConnections.Add(MakeShared<FJsonValueObject>(BuildConnectionRecord(
			Graph,
			SourceNodeId,
			SourcePin,
			TargetNodeId,
			TargetPin,
			i)));
	}
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteDeleteNode(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString NodeId;
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Delete the node
	FString DeleteError;
	if (!FBlueprintUtils::DeleteNode(Graph, NodeId, DeleteError))
	{
		return FMCPToolResult::Error(DeleteError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Node deleted")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("node_id"), NodeId);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Deleted node '%s'"), *NodeId),
		ResultData
	);
}

// ===== Level 4: Connection Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteConnectPins(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString SourceNodeId;
	if (!ExtractRequiredString(Params, TEXT("source_node_id"), SourceNodeId, Error))
	{
		return Error.GetValue();
	}

	FString TargetNodeId;
	if (!ExtractRequiredString(Params, TEXT("target_node_id"), TargetNodeId, Error))
	{
		return Error.GetValue();
	}

	FString SourcePin = ExtractOptionalString(Params, TEXT("source_pin"), TEXT(""));
	FString TargetPin = ExtractOptionalString(Params, TEXT("target_pin"), TEXT(""));
	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Connect the pins
	FString ConnectError;
	if (!FBlueprintUtils::ConnectPins(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, ConnectError))
	{
		return FMCPToolResult::Error(ConnectError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Pins connected")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	TSharedPtr<FJsonObject> ConnectionRecord = BuildConnectionRecord(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, 0);
	ResultData->SetStringField(TEXT("source_node_id"), SourceNodeId);
	ResultData->SetStringField(TEXT("source_pin"), ConnectionRecord->GetStringField(TEXT("resolved_source_pin")));
	ResultData->SetStringField(TEXT("target_node_id"), TargetNodeId);
	ResultData->SetStringField(TEXT("target_pin"), ConnectionRecord->GetStringField(TEXT("resolved_target_pin")));
	ResultData->SetObjectField(TEXT("connection"), ConnectionRecord);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Connected '%s.%s' -> '%s.%s'"),
			*SourceNodeId,
			*ConnectionRecord->GetStringField(TEXT("resolved_source_pin")),
			*TargetNodeId,
			*ConnectionRecord->GetStringField(TEXT("resolved_target_pin"))),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteDisconnectPins(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString SourceNodeId;
	if (!ExtractRequiredString(Params, TEXT("source_node_id"), SourceNodeId, Error))
	{
		return Error.GetValue();
	}

	FString SourcePin;
	if (!ExtractRequiredString(Params, TEXT("source_pin"), SourcePin, Error))
	{
		return Error.GetValue();
	}

	FString TargetNodeId;
	if (!ExtractRequiredString(Params, TEXT("target_node_id"), TargetNodeId, Error))
	{
		return Error.GetValue();
	}

	FString TargetPin;
	if (!ExtractRequiredString(Params, TEXT("target_pin"), TargetPin, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Disconnect the pins
	FString DisconnectError;
	if (!FBlueprintUtils::DisconnectPins(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, DisconnectError))
	{
		return FMCPToolResult::Error(DisconnectError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Pins disconnected")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("source_node_id"), SourceNodeId);
	ResultData->SetStringField(TEXT("source_pin"), SourcePin);
	ResultData->SetStringField(TEXT("target_node_id"), TargetNodeId);
	ResultData->SetStringField(TEXT("target_pin"), TargetPin);
	if (UEdGraphNode* SourceNode = FBlueprintUtils::FindNodeById(Graph, SourceNodeId))
	{
		ResultData->SetObjectField(TEXT("source_node"), FBlueprintUtils::SerializeNodeInfo(SourceNode));
	}
	if (UEdGraphNode* TargetNode = FBlueprintUtils::FindNodeById(Graph, TargetNodeId))
	{
		ResultData->SetObjectField(TEXT("target_node"), FBlueprintUtils::SerializeNodeInfo(TargetNode));
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Disconnected '%s.%s' from '%s.%s'"), *SourceNodeId, *SourcePin, *TargetNodeId, *TargetPin),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteSetPinValue(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString NodeId;
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Error))
	{
		return Error.GetValue();
	}

	FString PinName;
	if (!ExtractRequiredString(Params, TEXT("pin_name"), PinName, Error))
	{
		return Error.GetValue();
	}

	FString PinValue;
	if (!ExtractRequiredString(Params, TEXT("pin_value"), PinValue, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Set the pin value
	FString SetError;
	if (!FBlueprintUtils::SetPinDefaultValue(Graph, NodeId, PinName, PinValue, SetError))
	{
		return FMCPToolResult::Error(SetError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Pin value set")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("node_id"), NodeId);
	ResultData->SetStringField(TEXT("pin_name"), PinName);
	ResultData->SetStringField(TEXT("pin_value"), PinValue);
	if (UEdGraphNode* Node = FBlueprintUtils::FindNodeById(Graph, NodeId))
	{
		ResultData->SetObjectField(TEXT("node"), FBlueprintUtils::SerializeNodeInfo(Node));
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set '%s.%s' = '%s'"), *NodeId, *PinName, *PinValue),
		ResultData
	);
}








