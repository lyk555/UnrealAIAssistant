// Copyright Natali Caggiano. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
enum class ECodexProvider : uint8
{
	Codex,
	Claude
};
inline FString GetCodexProviderDisplayName(ECodexProvider Provider)
{
	switch (Provider)
	{
	case ECodexProvider::Claude:
		return TEXT("Claude Code");
	case ECodexProvider::Codex:
	default:
		return TEXT("CodeX");
	}
}
inline FString GetCodexProviderCliName(ECodexProvider Provider)
{
	switch (Provider)
	{
	case ECodexProvider::Claude:
		return TEXT("claude");
	case ECodexProvider::Codex:
	default:
		return TEXT("codex");
	}
}
struct UNREALAIASSISTANT_API FCodexModelInfo
{
	FString Id;
	FString DisplayName;
	ECodexProvider Provider = ECodexProvider::Codex;
	bool bSupportsImages = false;
	bool bSupportsTools = true;
	FCodexModelInfo() = default;
	FCodexModelInfo(
		const TCHAR* InId,
		const TCHAR* InDisplayName,
		ECodexProvider InProvider,
		bool bInSupportsImages,
		bool bInSupportsTools = true)
		: Id(InId)
		, DisplayName(InDisplayName)
		, Provider(InProvider)
		, bSupportsImages(bInSupportsImages)
		, bSupportsTools(bInSupportsTools)
	{
	}
};
DECLARE_DELEGATE_TwoParams(FOnCodexResponse, const FString& /*Response*/, bool /*bSuccess*/);
DECLARE_DELEGATE_OneParam(FOnCodexProgress, const FString& /*PartialOutput*/);
/**
 * Types of structured events derived from CLI output.
 * Maps provider-specific event streams into editor-friendly categories.
 */
enum class ECodexStreamEventType : uint8
{
	SessionInit,
	TextContent,
	ToolUse,
	ToolResult,
	Result,
	AssistantMessage,
	Unknown
};
/**
 * Structured event parsed from a provider CLI stream.
 */
struct UNREALAIASSISTANT_API FCodexStreamEvent
{
	ECodexStreamEventType Type = ECodexStreamEventType::Unknown;
	ECodexProvider Provider = ECodexProvider::Codex;
	FString Text;
	FString ToolName;
	FString ToolInput;
	FString ToolCallId;
	FString ToolResultContent;
	FString SessionId;
	bool bIsError = false;
	int32 DurationMs = 0;
	int32 NumTurns = 0;
	float TotalCostUsd = 0.0f;
	FString ResultText;
	FString RawJson;
};
DECLARE_DELEGATE_OneParam(FOnCodexStreamEvent, const FCodexStreamEvent& /*Event*/);
/**
 * Configuration for provider CLI execution.
 */
struct UNREALAIASSISTANT_API FCodexRequestConfig
{
	FString Prompt;
	ECodexProvider Provider = ECodexProvider::Codex;
	FString ModelId;
	FString SystemPrompt;
	FString WorkingDirectory;
	bool bUseJsonOutput = false;
	bool bSkipPermissions = true;
	float TimeoutSeconds = 300.0f;
	TArray<FString> AllowedTools;
	TArray<FString> AttachedImagePaths;
	FOnCodexStreamEvent OnStreamEvent;
};
/**
 * Abstract interface for provider-specific CLI runners.
 */
class UNREALAIASSISTANT_API IAssistantRunner
{
public:
	virtual ~IAssistantRunner() = default;
	virtual bool ExecuteAsync(
		const FCodexRequestConfig& Config,
		FOnCodexResponse OnComplete,
		FOnCodexProgress OnProgress = FOnCodexProgress()
	) = 0;
	virtual bool ExecuteSync(const FCodexRequestConfig& Config, FString& OutResponse) = 0;
	virtual void Cancel() = 0;
	virtual bool IsExecuting() const = 0;
	virtual bool IsAvailable() const = 0;
};
