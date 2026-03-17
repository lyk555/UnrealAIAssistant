// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_BlueprintQuery.h"
#include "BlueprintUtils.h"
#include "MCP/MCPParamValidator.h"
#include "UnrealAIAssistantModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"

FMCPToolResult FMCPTool_BlueprintQuery::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	if (Operation == TEXT("list"))
	{
		return ExecuteList(Params);
	}
	else if (Operation == TEXT("inspect"))
	{
		return ExecuteInspect(Params);
	}
	else if (Operation == TEXT("get_graph"))
	{
		return ExecuteGetGraph(Params);
	}
	else if (Operation == TEXT("capabilities"))
	{
		return ExecuteCapabilities(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid operations: 'list', 'inspect', 'get_graph', 'capabilities'"), *Operation));
}

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteList(const TSharedRef<FJsonObject>& Params)
{
	FString PathFilter = ExtractOptionalString(Params, TEXT("path_filter"), TEXT("/Game/"));
	FString TypeFilter = ExtractOptionalString(Params, TEXT("type_filter"));
	FString NameFilter = ExtractOptionalString(Params, TEXT("name_filter"));
	int32 Limit = ExtractOptionalNumber<int32>(Params, TEXT("limit"), 25);
	Limit = FMath::Clamp(Limit, 1, 1000);

	FString ValidationError;
	if (!PathFilter.IsEmpty() && !FMCPParamValidator::ValidateBlueprintPath(PathFilter, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathFilter));
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 Count = 0;
	int32 TotalMatching = 0;

	for (const FAssetData& AssetData : AssetDataList)
	{
		FString ParentClassName;
		FAssetDataTagMapSharedView::FFindTagResult ParentClassTag = AssetData.TagsAndValues.FindTag(FName("ParentClass"));
		if (ParentClassTag.IsSet())
		{
			ParentClassName = ParentClassTag.GetValue();
		}

		if (!TypeFilter.IsEmpty() && !ParentClassName.Contains(TypeFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (!NameFilter.IsEmpty() && !AssetData.AssetName.ToString().Contains(NameFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TotalMatching++;
		if (Count >= Limit)
		{
			continue;
		}

		FString BlueprintType = TEXT("Normal");
		FAssetDataTagMapSharedView::FFindTagResult TypeTag = AssetData.TagsAndValues.FindTag(FName("BlueprintType"));
		if (TypeTag.IsSet())
		{
			BlueprintType = TypeTag.GetValue();
		}

		TSharedPtr<FJsonObject> BPJson = MakeShared<FJsonObject>();
		BPJson->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		BPJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		BPJson->SetStringField(TEXT("blueprint_type"), BlueprintType);

		if (!ParentClassName.IsEmpty())
		{
			FString CleanParentName = ParentClassName;
			int32 LastDotIndex;
			if (CleanParentName.FindLastChar(TEXT('.'), LastDotIndex))
			{
				CleanParentName = CleanParentName.Mid(LastDotIndex + 1);
			}
			if (CleanParentName.EndsWith(TEXT("_C")))
			{
				CleanParentName = CleanParentName.LeftChop(2);
			}
			BPJson->SetStringField(TEXT("parent_class"), CleanParentName);
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(BPJson));
		Count++;
	}

	TSharedPtr<FJsonObject> ResponseData = MakeShared<FJsonObject>();
	ResponseData->SetArrayField(TEXT("blueprints"), ResultsArray);
	ResponseData->SetNumberField(TEXT("count"), Count);
	ResponseData->SetNumberField(TEXT("total_matching"), TotalMatching);
	if (TotalMatching > Count)
	{
		ResponseData->SetBoolField(TEXT("truncated"), true);
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d Blueprints (showing %d)"), TotalMatching, Count),
		ResponseData
	);
}

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteInspect(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	bool bIncludeVariables = ExtractOptionalBool(Params, TEXT("include_variables"), false);
	bool bIncludeFunctions = ExtractOptionalBool(Params, TEXT("include_functions"), false);
	bool bIncludeGraphs = ExtractOptionalBool(Params, TEXT("include_graphs"), false);

	TSharedPtr<FJsonObject> BlueprintInfo = FBlueprintUtils::SerializeBlueprintInfo(
		Blueprint,
		bIncludeVariables,
		bIncludeFunctions,
		bIncludeGraphs
	);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Blueprint info for: %s"), *Blueprint->GetName()),
		BlueprintInfo
	);
}

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetGraph(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	const FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	const bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);
	const bool bIncludeNodes = ExtractOptionalBool(Params, TEXT("include_nodes"), true);

	TSharedPtr<FJsonObject> GraphInfo = FBlueprintUtils::GetGraphInfo(Blueprint, bIncludeNodes, GraphName, bFunctionGraph);
	GraphInfo->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	GraphInfo->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

	const TArray<TSharedPtr<FJsonValue>>* GraphsArray = nullptr;
	if (GraphInfo->TryGetArrayField(TEXT("graphs"), GraphsArray) && GraphsArray && GraphsArray->Num() == 0 && !GraphName.IsEmpty())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' was not found in Blueprint '%s'"), *GraphName, *Blueprint->GetName()));
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Graph info for: %s"), *Blueprint->GetName()),
		GraphInfo
	);
}

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteCapabilities(const TSharedRef<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> QueryOps;
	for (const TCHAR* Op : { TEXT("list"), TEXT("inspect"), TEXT("get_graph"), TEXT("capabilities") })
	{
		QueryOps.Add(MakeShared<FJsonValueString>(Op));
	}
	ResultData->SetArrayField(TEXT("query_operations"), QueryOps);

	TArray<TSharedPtr<FJsonValue>> ModifyOps;
	for (const TCHAR* Op : {
		TEXT("create"), TEXT("add_variable"), TEXT("remove_variable"), TEXT("add_function"), TEXT("remove_function"),
		TEXT("add_node"), TEXT("add_nodes"), TEXT("delete_node"), TEXT("connect_pins"), TEXT("disconnect_pins"), TEXT("set_pin_value") })
	{
		ModifyOps.Add(MakeShared<FJsonValueString>(Op));
	}
	ResultData->SetArrayField(TEXT("modify_operations"), ModifyOps);

	TArray<TSharedPtr<FJsonValue>> NodeTypes;
	for (const TCHAR* NodeType : {
		TEXT("CallFunction"), TEXT("Branch"), TEXT("Event"), TEXT("VariableGet"), TEXT("VariableSet"),
		TEXT("Sequence"), TEXT("PrintString"), TEXT("Add"), TEXT("Subtract"), TEXT("Multiply"), TEXT("Divide") })
	{
		NodeTypes.Add(MakeShared<FJsonValueString>(NodeType));
	}
	ResultData->SetArrayField(TEXT("supported_node_types"), NodeTypes);

	TArray<TSharedPtr<FJsonValue>> Workflow;
	for (const TCHAR* Step : {
		TEXT("Use blueprint_query inspect/get_graph before blueprint_modify on existing graphs"),
		TEXT("Existing nodes now expose stable node_id values derived from NodeGuid when no MCP id exists"),
		TEXT("Use get_graph with include_nodes=true to inspect BeginPlay chains, pins, and linked nodes"),
		TEXT("CallFunction support is strongest for resolvable static/class functions; instance-context wiring may still need explicit graph inspection first") })
	{
		Workflow.Add(MakeShared<FJsonValueString>(Step));
	}
	ResultData->SetArrayField(TEXT("recommended_workflow"), Workflow);

	TArray<TSharedPtr<FJsonValue>> Limitations;
	for (const TCHAR* Limitation : {
		TEXT("The tool can inspect existing EventGraph and FunctionGraph topology, but it does not infer gameplay intent automatically"),
		TEXT("Complex instance-method chains may still require explicit target pins and prior graph inspection"),
		TEXT("Node creation support is narrower than the full Blueprint editor and is intentionally constrained to common patterns") })
	{
		Limitations.Add(MakeShared<FJsonValueString>(Limitation));
	}
	ResultData->SetArrayField(TEXT("known_limitations"), Limitations);

	return FMCPToolResult::Success(TEXT("Blueprint tool capabilities"), ResultData);
}
