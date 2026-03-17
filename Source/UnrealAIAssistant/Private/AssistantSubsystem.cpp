// Copyright Natali Caggiano. All Rights Reserved.

#include "AssistantSubsystem.h"
#include "ClaudeCodeRunner.h"
#include "AssistantCodeRunner.h"
#include "AssistantSessionManager.h"
#include "ProjectContext.h"
#include "ScriptExecutionManager.h"
#include "UnrealAIAssistantConstants.h"
#include "UnrealAIAssistantModule.h"

namespace
{
const TArray<FCodexModelInfo>& GetCodexModels()
{
	static const TArray<FCodexModelInfo> Models = {
		FCodexModelInfo(TEXT(""), TEXT("Provider Default"), ECodexProvider::Codex, true),
		FCodexModelInfo(TEXT("gpt-5"), TEXT("GPT-5"), ECodexProvider::Codex, true),
		FCodexModelInfo(TEXT("o3"), TEXT("o3"), ECodexProvider::Codex, true)
	};

	return Models;
}

const TArray<FCodexModelInfo>& GetClaudeModels()
{
	static const TArray<FCodexModelInfo> Models = {
		FCodexModelInfo(TEXT(""), TEXT("Provider Default"), ECodexProvider::Claude, true),
		FCodexModelInfo(TEXT("claude-sonnet-4-20250514"), TEXT("Claude Sonnet 4"), ECodexProvider::Claude, true),
		FCodexModelInfo(TEXT("claude-opus-4-1-20250805"), TEXT("Claude Opus 4.1"), ECodexProvider::Claude, true),
		FCodexModelInfo(TEXT("claude-3-7-sonnet-20250219"), TEXT("Claude Sonnet 3.7"), ECodexProvider::Claude, true)
	};

	return Models;
}

TUniquePtr<IAssistantRunner> MakeRunner(ECodexProvider Provider)
{
	switch (Provider)
	{
	case ECodexProvider::Claude:
		return MakeUnique<FClaudeCodeRunner>();
	case ECodexProvider::Codex:
	default:
		return MakeUnique<FAssistantCodeRunner>();
	}
}
}

static const FString CachedUE55SystemPrompt = TEXT(R"(You are an expert Unreal Engine 5.5 developer assistant integrated directly into the UE Editor.

CONTEXT:
- You are helping with an Unreal Engine 5.5 project
- The user is working in the Unreal Editor and expects UE5.5-specific guidance
- Focus on current UE5.5 APIs, patterns, and best practices

KEY UE5.5 FEATURES TO BE AWARE OF:
- Enhanced Nanite and Lumen for next-gen rendering
- World Partition for open world streaming
- Mass Entity (experimental) for large-scale simulations
- Enhanced Input System (preferred over legacy input)
- Common UI for cross-platform interfaces
- Gameplay Ability System (GAS) for complex ability systems
- MetaSounds for procedural audio
- Chaos physics engine (default)
- Control Rig for animation
- Niagara for VFX

CODING STANDARDS:
- Use UPROPERTY, UFUNCTION, UCLASS macros properly
- Follow Unreal naming conventions (F for structs, U for UObject, A for Actor, E for enums)
- Prefer BlueprintCallable/BlueprintPure for BP-exposed functions
- Use TObjectPtr<> for object pointers in headers (UE5+)
- Use Forward declarations in headers, includes in cpp
- Properly use GENERATED_BODY() macro

WHEN PROVIDING CODE:
- Always specify the correct includes
- Use proper UE5.5 API calls (not deprecated ones)
- Include both .h and .cpp when showing class implementations
- Explain any engine-specific gotchas or limitations

TOOL USAGE GUIDELINES:
- You have dedicated MCP tools for common Unreal Editor operations. ALWAYS prefer these over execute_script:
  * spawn_actor, move_actor, delete_actors, get_level_actors, set_property - Actor manipulation
  * open_level (open/new/list_templates) - Level management: open maps, create new levels, list templates
  * blueprint_query, blueprint_modify - Blueprint inspection and editing
  * anim_blueprint_modify - Animation blueprint state machines
  * asset_search, asset_dependencies, asset_referencers - Asset discovery and dependency tracking
  * capture_viewport - Screenshot the editor viewport
  * run_console_command - Run editor console commands
  * enhanced_input - Input action and mapping context management
  * character, character_data - Character and movement configuration
  * material - Material and material instance operations
  * task_submit, task_status, task_result, task_list, task_cancel - Async task management
- Only use execute_script when NO dedicated tool can accomplish the task
- Use open_level to switch levels instead of console commands (the 'open' command is blocked for security)
- Use get_ue_context to look up UE5.5 API patterns before writing scripts

RESPONSE FORMAT:
- Be concise but thorough
- Provide code examples when helpful
- Mention relevant documentation or resources
- Warn about common pitfalls)");

FCodexCodeSubsystem& FCodexCodeSubsystem::Get()
{
	static FCodexCodeSubsystem Instance;
	return Instance;
}

FCodexCodeSubsystem::FCodexCodeSubsystem()
{
	SessionManager = MakeUnique<FAssistantSessionManager>();
	ActiveProvider = ChooseInitialProvider();
	ActiveModelId = GetDefaultModelId(ActiveProvider);
	RecreateRunner(true);
}

FCodexCodeSubsystem::~FCodexCodeSubsystem()
{
}

IAssistantRunner* FCodexCodeSubsystem::GetRunner() const
{
	return Runner.Get();
}

void FCodexCodeSubsystem::SendPrompt(
	const FString& Prompt,
	FOnCodexResponse OnComplete,
	const FCodexPromptOptions& Options)
{
	if (Options.bOverrideProvider && Options.Provider != ActiveProvider)
	{
		SetActiveProvider(Options.Provider);
	}

	if (!Options.ModelId.IsEmpty())
	{
		ActiveModelId = Options.ModelId;
	}

	RecreateRunner();
	if (!Runner.IsValid())
	{
		OnComplete.ExecuteIfBound(TEXT("No supported provider is configured."), false);
		return;
	}

	FCodexRequestConfig Config;
	Config.Prompt = BuildPromptWithHistory(Prompt);
	Config.Provider = ActiveProvider;
	Config.ModelId = ActiveModelId;
	Config.WorkingDirectory = FPaths::ProjectDir();
	Config.bSkipPermissions = true;
	Config.AllowedTools = { TEXT("Read"), TEXT("Write"), TEXT("Edit"), TEXT("Grep"), TEXT("Glob"), TEXT("Bash") };
	Config.AttachedImagePaths = Options.AttachedImagePaths;

	if (Options.bIncludeEngineContext)
	{
		Config.SystemPrompt = GetUE55SystemPrompt();
	}

	if (Options.bIncludeProjectContext)
	{
		Config.SystemPrompt += GetProjectContextPrompt();
	}

	if (!CustomSystemPrompt.IsEmpty())
	{
		Config.SystemPrompt += TEXT("\n\n") + CustomSystemPrompt;
	}

	Config.OnStreamEvent = Options.OnStreamEvent;

	FOnCodexResponse WrappedComplete;
	WrappedComplete.BindLambda([this, Prompt, OnComplete](const FString& Response, bool bSuccess)
	{
		if (bSuccess && SessionManager.IsValid())
		{
			SessionManager->AddExchange(Prompt, Response);
			SessionManager->SaveSession();
		}
		OnComplete.ExecuteIfBound(Response, bSuccess);
	});

	Runner->ExecuteAsync(Config, WrappedComplete, Options.OnProgress);
}

void FCodexCodeSubsystem::SendPrompt(
	const FString& Prompt,
	FOnCodexResponse OnComplete,
	bool bIncludeUE55Context,
	FOnCodexProgress OnProgress,
	bool bIncludeProjectContext)
{
	FCodexPromptOptions Options;
	Options.bIncludeEngineContext = bIncludeUE55Context;
	Options.bIncludeProjectContext = bIncludeProjectContext;
	Options.OnProgress = OnProgress;
	SendPrompt(Prompt, OnComplete, Options);
}

FString FCodexCodeSubsystem::GetUE55SystemPrompt() const
{
	return CachedUE55SystemPrompt;
}

FString FCodexCodeSubsystem::GetProjectContextPrompt() const
{
	FString Context = FProjectContextManager::Get().FormatContextForPrompt();
	FString ScriptHistory = FScriptExecutionManager::Get().FormatHistoryForContext(10);
	if (!ScriptHistory.IsEmpty())
	{
		Context += TEXT("\n\n") + ScriptHistory;
	}

	return Context;
}

void FCodexCodeSubsystem::SetCustomSystemPrompt(const FString& InCustomPrompt)
{
	CustomSystemPrompt = InCustomPrompt;
}

const TArray<TPair<FString, FString>>& FCodexCodeSubsystem::GetHistory() const
{
	static TArray<TPair<FString, FString>> EmptyHistory;
	if (SessionManager.IsValid())
	{
		return SessionManager->GetHistory();
	}
	return EmptyHistory;
}

void FCodexCodeSubsystem::ClearHistory()
{
	if (SessionManager.IsValid())
	{
		SessionManager->ClearHistory();
	}
}

void FCodexCodeSubsystem::CancelCurrentRequest()
{
	if (Runner.IsValid())
	{
		Runner->Cancel();
	}
}

bool FCodexCodeSubsystem::SaveSession()
{
	return SessionManager.IsValid() ? SessionManager->SaveSession() : false;
}

bool FCodexCodeSubsystem::LoadSession()
{
	return SessionManager.IsValid() ? SessionManager->LoadSession() : false;
}

bool FCodexCodeSubsystem::HasSavedSession() const
{
	return SessionManager.IsValid() ? SessionManager->HasSavedSession() : false;
}

FString FCodexCodeSubsystem::GetSessionFilePath() const
{
	return SessionManager.IsValid() ? SessionManager->GetSessionFilePath() : FString();
}

void FCodexCodeSubsystem::SetActiveProvider(ECodexProvider InProvider)
{
	if (ActiveProvider == InProvider)
	{
		return;
	}

	if (Runner.IsValid() && Runner->IsExecuting())
	{
		Runner->Cancel();
	}

	ActiveProvider = InProvider;
	ActiveModelId = GetDefaultModelId(ActiveProvider);
	RecreateRunner(true);
}

FString FCodexCodeSubsystem::GetActiveProviderDisplayName() const
{
	return GetProviderDisplayName(ActiveProvider);
}

bool FCodexCodeSubsystem::IsProviderAvailable(ECodexProvider Provider) const
{
	switch (Provider)
	{
	case ECodexProvider::Claude:
		return FClaudeCodeRunner::IsClaudeAvailable();
	case ECodexProvider::Codex:
	default:
		return FAssistantCodeRunner::IsCodexAvailable();
	}
}

bool FCodexCodeSubsystem::IsActiveProviderAvailable() const
{
	return IsProviderAvailable(ActiveProvider);
}

bool FCodexCodeSubsystem::HasAnyAvailableProvider() const
{
	for (ECodexProvider Provider : GetSupportedProviders())
	{
		if (IsProviderAvailable(Provider))
		{
			return true;
		}
	}

	return false;
}

TArray<ECodexProvider> FCodexCodeSubsystem::GetSupportedProviders()
{
	TArray<ECodexProvider> Providers;
	Providers.Add(ECodexProvider::Codex);
	Providers.Add(ECodexProvider::Claude);
	return Providers;
}

FString FCodexCodeSubsystem::GetProviderDisplayName(ECodexProvider Provider)
{
	return GetCodexProviderDisplayName(Provider);
}

const TArray<FCodexModelInfo>& FCodexCodeSubsystem::GetModelsForProvider(ECodexProvider Provider) const
{
	switch (Provider)
	{
	case ECodexProvider::Claude:
		return GetClaudeModels();
	case ECodexProvider::Codex:
	default:
		return GetCodexModels();
	}
}

void FCodexCodeSubsystem::SetActiveModelId(const FString& InModelId)
{
	ActiveModelId = InModelId;
}

FString FCodexCodeSubsystem::GetDefaultModelId(ECodexProvider Provider) const
{
	const TArray<FCodexModelInfo>& Models = GetModelsForProvider(Provider);
	return Models.Num() > 0 ? Models[0].Id : FString();
}

FString FCodexCodeSubsystem::BuildPromptWithHistory(const FString& NewPrompt) const
{
	if (!SessionManager.IsValid())
	{
		return NewPrompt;
	}

	const TArray<TPair<FString, FString>>& History = SessionManager->GetHistory();
	if (History.Num() == 0)
	{
		return NewPrompt;
	}

	FString PromptWithHistory;
	int32 StartIndex = FMath::Max(0, History.Num() - UnrealAIAssistantConstants::Session::MaxHistoryInPrompt);

	for (int32 i = StartIndex; i < History.Num(); ++i)
	{
		const TPair<FString, FString>& Exchange = History[i];
		PromptWithHistory += FString::Printf(TEXT("Human: %s\n\nAssistant: %s\n\n"), *Exchange.Key, *Exchange.Value);
	}

	PromptWithHistory += FString::Printf(TEXT("Human: %s"), *NewPrompt);
	return PromptWithHistory;
}

void FCodexCodeSubsystem::RecreateRunner(bool bForce)
{
	if (!bForce && Runner.IsValid())
	{
		return;
	}

	Runner.Reset();
	Runner = MakeRunner(ActiveProvider);
}

ECodexProvider FCodexCodeSubsystem::ChooseInitialProvider()
{
	if (FAssistantCodeRunner::IsCodexAvailable())
	{
		return ECodexProvider::Codex;
	}

	if (FClaudeCodeRunner::IsClaudeAvailable())
	{
		return ECodexProvider::Claude;
	}

	return ECodexProvider::Codex;
}
