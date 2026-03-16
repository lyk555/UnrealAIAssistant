// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ICodexRunner.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"

/**
 * Async runner for Codex CLI commands.
 * Uses `codex exec --json` so the editor can consume structured JSONL events.
 */
class UNREALCODEX_API FCodexCodeRunner : public ICodexRunner, public FRunnable
{
public:
	FCodexCodeRunner();
	virtual ~FCodexCodeRunner();

	// ICodexRunner interface
	virtual bool ExecuteAsync(
		const FCodexRequestConfig& Config,
		FOnCodexResponse OnComplete,
		FOnCodexProgress OnProgress = FOnCodexProgress()
	) override;

	virtual bool ExecuteSync(const FCodexRequestConfig& Config, FString& OutResponse) override;
	virtual void Cancel() override;
	virtual bool IsExecuting() const override { return bIsExecuting; }
	virtual bool IsAvailable() const override { return IsCodexAvailable(); }

	/** Check if the Codex CLI is available on this system. */
	static bool IsCodexAvailable();

	/** Resolve the Codex executable path. */
	static FString GetCodexPath();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	/** Legacy helper retained for existing tests around clipboard image payload generation. */
	FString BuildStreamJsonPayload(const FString& TextPrompt, const TArray<FString>& ImagePaths);

	/** Parse Codex JSONL output to extract the response text. */
	FString ParseStreamJsonOutput(const FString& RawOutput);

private:
	FString BuildCommandLine(const FCodexRequestConfig& Config);
	FString BuildPromptText(const FCodexRequestConfig& Config) const;
	void ExecuteProcess();
	void CleanupHandles();
	void ParseAndEmitJsonlLine(const FString& JsonLine);
	bool CreateProcessPipes();
	bool LaunchProcess(const FString& FullCommand, const FString& WorkingDir);
	FString ReadProcessOutput();
	void ReportError(const FString& ErrorMessage);
	void ReportCompletion(const FString& Output, bool bSuccess);

	/** Buffer for accumulating incomplete JSONL lines across read chunks. */
	FString JsonlLineBuffer;

	/** Accumulated text from Codex messages for the final response. */
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

	/** Temp file path used by `--output-last-message`. */
	FString OutputLastMessageFilePath;

	/** Temp CODEX_HOME used to provide a per-request config.toml for MCP bridge wiring. */
	FString TempCodexHomePath;

	/** Request start time used to derive duration for result events. */
	double RequestStartTimeSeconds = 0.0;
};
