// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ICodexRunner.h"

// Forward declarations
class FCodexSessionManager;
class FCodexCodeRunner;

/**
 * Options for sending a prompt to Codex
 * Reduces parameter count in SendPrompt method
 */
struct UNREALCODEX_API FCodexPromptOptions
{
	/** Include UE5.5 engine context in system prompt */
	bool bIncludeEngineContext = true;

	/** Include project-specific context in system prompt */
	bool bIncludeProjectContext = true;

	/** Optional callback for streaming output progress */
	FOnCodexProgress OnProgress;

	/** Optional callback for structured NDJSON stream events */
	FOnCodexStreamEvent OnStreamEvent;

	/** Optional paths to attached clipboard images (PNG) */
	TArray<FString> AttachedImagePaths;

	/** Default constructor with sensible defaults */
	FCodexPromptOptions() = default;

	/** Convenience constructor for common case */
	FCodexPromptOptions(bool bEngineContext, bool bProjectContext)
		: bIncludeEngineContext(bEngineContext)
		, bIncludeProjectContext(bProjectContext)
	{}
};

/**
 * Subsystem for managing Codex Code interactions
 * Orchestrates runner, session management, and prompt building
 */
class UNREALCODEX_API FCodexCodeSubsystem
{
public:
	static FCodexCodeSubsystem& Get();

	/** Destructor - must be defined in cpp where full types are available */
	~FCodexCodeSubsystem();

	/** Send a prompt to Codex with optional context (new API with options struct) */
	void SendPrompt(
		const FString& Prompt,
		FOnCodexResponse OnComplete,
		const FCodexPromptOptions& Options = FCodexPromptOptions()
	);

	/** Send a prompt to Codex with optional context (legacy API for backward compatibility) */
	void SendPrompt(
		const FString& Prompt,
		FOnCodexResponse OnComplete,
		bool bIncludeUE55Context,
		FOnCodexProgress OnProgress,
		bool bIncludeProjectContext = true
	);

	/** Get the default UE5.5 system prompt */
	FString GetUE55SystemPrompt() const;

	/** Get the project context prompt */
	FString GetProjectContextPrompt() const;

	/** Set custom system prompt additions */
	void SetCustomSystemPrompt(const FString& InCustomPrompt);

	/** Get conversation history (delegates to session manager) */
	const TArray<TPair<FString, FString>>& GetHistory() const;

	/** Clear conversation history */
	void ClearHistory();

	/** Cancel current request */
	void CancelCurrentRequest();

	/** Save current session to disk */
	bool SaveSession();

	/** Load previous session from disk */
	bool LoadSession();

	/** Check if a previous session exists */
	bool HasSavedSession() const;

	/** Get session file path */
	FString GetSessionFilePath() const;

	/** Get the runner interface (for testing/mocking) */
	ICodexRunner* GetRunner() const;

private:
	FCodexCodeSubsystem();

	/** Build prompt with conversation history context */
	FString BuildPromptWithHistory(const FString& NewPrompt) const;

	TUniquePtr<FCodexCodeRunner> Runner;
	TUniquePtr<FCodexSessionManager> SessionManager;
	FString CustomSystemPrompt;
};
