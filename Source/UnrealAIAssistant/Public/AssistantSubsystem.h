// Copyright Natali Caggiano. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "IAssistantRunner.h"
class FAssistantSessionManager;
/**
 * Options for sending a prompt to the currently selected provider.
 */
struct UNREALAIASSISTANT_API FCodexPromptOptions
{
	bool bIncludeEngineContext = true;
	bool bIncludeProjectContext = true;
	FOnCodexProgress OnProgress;
	FOnCodexStreamEvent OnStreamEvent;
	TArray<FString> AttachedImagePaths;
	bool bOverrideProvider = false;
	ECodexProvider Provider = ECodexProvider::Codex;
	FString ModelId;
	FCodexPromptOptions() = default;
	FCodexPromptOptions(bool bEngineContext, bool bProjectContext)
		: bIncludeEngineContext(bEngineContext)
		, bIncludeProjectContext(bProjectContext)
	{
	}
};
class UNREALAIASSISTANT_API FCodexCodeSubsystem
{
public:
	static FCodexCodeSubsystem& Get();
	~FCodexCodeSubsystem();
	void SendPrompt(
		const FString& Prompt,
		FOnCodexResponse OnComplete,
		const FCodexPromptOptions& Options = FCodexPromptOptions()
	);
	void SendPrompt(
		const FString& Prompt,
		FOnCodexResponse OnComplete,
		bool bIncludeUE55Context,
		FOnCodexProgress OnProgress,
		bool bIncludeProjectContext = true
	);
	FString GetUE55SystemPrompt() const;
	FString GetProjectContextPrompt() const;
	void SetCustomSystemPrompt(const FString& InCustomPrompt);
	const TArray<TPair<FString, FString>>& GetHistory() const;
	void ClearHistory();
	void CancelCurrentRequest();
	bool SaveSession();
	bool LoadSession();
	bool HasSavedSession() const;
	FString GetSessionFilePath() const;
	IAssistantRunner* GetRunner() const;
	void SetActiveProvider(ECodexProvider InProvider);
	ECodexProvider GetActiveProvider() const { return ActiveProvider; }
	FString GetActiveProviderDisplayName() const;
	bool IsProviderAvailable(ECodexProvider Provider) const;
	bool IsActiveProviderAvailable() const;
	bool HasAnyAvailableProvider() const;
	static TArray<ECodexProvider> GetSupportedProviders();
	static FString GetProviderDisplayName(ECodexProvider Provider);
	const TArray<FCodexModelInfo>& GetModelsForProvider(ECodexProvider Provider) const;
	void SetActiveModelId(const FString& InModelId);
	FString GetActiveModelId() const { return ActiveModelId; }
	FString GetDefaultModelId(ECodexProvider Provider) const;
private:
	FCodexCodeSubsystem();
	FString BuildPromptWithHistory(const FString& NewPrompt) const;
	void RecreateRunner(bool bForce = false);
	static ECodexProvider ChooseInitialProvider();
	TUniquePtr<IAssistantRunner> Runner;
	TUniquePtr<FAssistantSessionManager> SessionManager;
	FString CustomSystemPrompt;
	ECodexProvider ActiveProvider = ECodexProvider::Codex;
	FString ActiveModelId;
};
