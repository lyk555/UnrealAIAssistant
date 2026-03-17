// Copyright Natali Caggiano. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "IAssistantRunner.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
/**
 * Async runner for Claude Code CLI commands.
 */
class UNREALAIASSISTANT_API FClaudeCodeRunner : public IAssistantRunner, public FRunnable
{
public:
	FClaudeCodeRunner();
	virtual ~FClaudeCodeRunner();
	virtual bool ExecuteAsync(
		const FCodexRequestConfig& Config,
		FOnCodexResponse OnComplete,
		FOnCodexProgress OnProgress = FOnCodexProgress()
	) override;
	virtual bool ExecuteSync(const FCodexRequestConfig& Config, FString& OutResponse) override;
	virtual void Cancel() override;
	virtual bool IsExecuting() const override { return bIsExecuting; }
	virtual bool IsAvailable() const override { return IsClaudeAvailable(); }
	static bool IsClaudeAvailable();
	static FString GetClaudePath();
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	FString BuildStreamJsonPayload(const FString& TextPrompt, const TArray<FString>& ImagePaths);
	FString ParseStreamJsonOutput(const FString& RawOutput);
private:
	FString BuildCommandLine(const FCodexRequestConfig& Config);
	void ExecuteProcess();
	void CleanupHandles();
	void ParseAndEmitNdjsonLine(const FString& JsonLine);
	bool CreateProcessPipes();
	bool LaunchProcess(const FString& FullCommand, const FString& WorkingDir);
	FString ReadProcessOutput();
	void ReportError(const FString& ErrorMessage);
	void ReportCompletion(const FString& Output, bool bSuccess);
	FString NdjsonLineBuffer;
	FString AccumulatedResponseText;
	FCodexRequestConfig CurrentConfig;
	FOnCodexResponse OnCompleteDelegate;
	FOnCodexProgress OnProgressDelegate;
	FRunnableThread* Thread;
	FThreadSafeCounter StopTaskCounter;
	TAtomic<bool> bIsExecuting;
	FProcHandle ProcessHandle;
	void* ReadPipe;
	void* WritePipe;
	void* StdInReadPipe;
	void* StdInWritePipe;
	FString SystemPromptFilePath;
	FString PromptFilePath;
};
