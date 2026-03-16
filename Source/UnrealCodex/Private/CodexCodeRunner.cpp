// Copyright Natali Caggiano. All Rights Reserved.

#include "CodexCodeRunner.h"
#include "UnrealCodexConstants.h"
#include "UnrealCodexModule.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString NormalizeCliPath(const FString& Path)
{
	FString Result = FPaths::ConvertRelativePathToFull(Path);
	FPaths::NormalizeFilename(Result);
	return Result.Replace(TEXT("\\"), TEXT("/"));
}

FString QuoteCliArg(const FString& Value)
{
	return FString::Printf(TEXT("\"%s\""), *Value.Replace(TEXT("\""), TEXT("\\\"")));
}

FString EscapeTomlString(const FString& Value)
{
	return Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
}

bool IsCmdScriptPath(const FString& Path)
{
	return Path.EndsWith(TEXT(".cmd"), ESearchCase::IgnoreCase) ||
		Path.EndsWith(TEXT(".bat"), ESearchCase::IgnoreCase);
}

FString SerializeJsonObject(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return FString();
	}

	FString Serialized;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	Writer->Close();
	return Serialized;
}

FString ExtractTextFromContentArray(const TArray<TSharedPtr<FJsonValue>>& ContentArray)
{
	FString Text;

	for (const TSharedPtr<FJsonValue>& Value : ContentArray)
	{
		if (!Value.IsValid())
		{
			continue;
		}

		FString StringValue;
		if (Value->TryGetString(StringValue))
		{
			Text += StringValue;
			continue;
		}

		const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
		if (!Value->TryGetObject(ObjectValue) || !ObjectValue || !(*ObjectValue).IsValid())
		{
			continue;
		}

		FString Fragment;
		if ((*ObjectValue)->TryGetStringField(TEXT("text"), Fragment) ||
			(*ObjectValue)->TryGetStringField(TEXT("content"), Fragment) ||
			(*ObjectValue)->TryGetStringField(TEXT("output"), Fragment))
		{
			Text += Fragment;
		}
	}

	return Text;
}

FString ExtractItemText(const TSharedPtr<FJsonObject>& ItemObject)
{
	if (!ItemObject.IsValid())
	{
		return FString();
	}

	FString Text;
	if (ItemObject->TryGetStringField(TEXT("text"), Text) ||
		ItemObject->TryGetStringField(TEXT("content"), Text) ||
		ItemObject->TryGetStringField(TEXT("output"), Text) ||
		ItemObject->TryGetStringField(TEXT("message"), Text))
	{
		return Text;
	}

	const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
	if (ItemObject->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray)
	{
		Text = ExtractTextFromContentArray(*ContentArray);
		if (!Text.IsEmpty())
		{
			return Text;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
	if (ItemObject->TryGetArrayField(TEXT("messages"), Messages) && Messages)
	{
		return ExtractTextFromContentArray(*Messages);
	}

	return FString();
}

bool IsToolItemType(const FString& ItemType)
{
	return ItemType == TEXT("mcp_tool_call") ||
		ItemType == TEXT("tool_call") ||
		ItemType == TEXT("command_execution") ||
		ItemType == TEXT("patch_apply");
}

FString GetToolName(const TSharedPtr<FJsonObject>& ItemObject, const FString& ItemType)
{
	if (!ItemObject.IsValid())
	{
		return ItemType;
	}

	FString ToolName;
	if (ItemObject->TryGetStringField(TEXT("tool_name"), ToolName) ||
		ItemObject->TryGetStringField(TEXT("name"), ToolName) ||
		ItemObject->TryGetStringField(TEXT("command"), ToolName))
	{
		return ToolName;
	}

	return ItemType;
}

FString GetToolResultContent(const TSharedPtr<FJsonObject>& ItemObject)
{
	if (!ItemObject.IsValid())
	{
		return FString();
	}

	FString ResultContent;
	if (ItemObject->TryGetStringField(TEXT("output"), ResultContent) ||
		ItemObject->TryGetStringField(TEXT("stdout"), ResultContent) ||
		ItemObject->TryGetStringField(TEXT("stderr"), ResultContent) ||
		ItemObject->TryGetStringField(TEXT("summary"), ResultContent))
	{
		return ResultContent;
	}

	ResultContent = ExtractItemText(ItemObject);
	if (!ResultContent.IsEmpty())
	{
		return ResultContent;
	}

	return SerializeJsonObject(ItemObject);
}

FString GetPluginDirectory()
{
	const FString EnginePluginPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("UnrealCodex"));
	if (FPaths::DirectoryExists(EnginePluginPath))
	{
		return EnginePluginPath;
	}

	const FString MarketplacePluginPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Marketplace"), TEXT("UnrealCodex"));
	if (FPaths::DirectoryExists(MarketplacePluginPath))
	{
		return MarketplacePluginPath;
	}

	const FString ProjectPluginPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("UnrealCodex"));
	if (FPaths::DirectoryExists(ProjectPluginPath))
	{
		return ProjectPluginPath;
	}

	UE_LOG(LogUnrealCodex, Warning, TEXT("Could not find UnrealCodex plugin directory."));
	return FString();
}

FString GetDefaultCodexHomePath()
{
#if PLATFORM_WINDOWS
	const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfile.IsEmpty())
	{
		return FPaths::Combine(UserProfile, TEXT(".codex"));
	}
#else
	const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!Home.IsEmpty())
	{
		return FPaths::Combine(Home, TEXT(".codex"));
	}
#endif

	return FString();
}

void CopyCodexHomeFileIfPresent(const FString& SourceHomePath, const FString& TargetHomePath, const FString& FileName)
{
	const FString SourcePath = FPaths::Combine(SourceHomePath, FileName);
	if (!FPaths::FileExists(SourcePath))
	{
		return;
	}

	const FString TargetPath = FPaths::Combine(TargetHomePath, FileName);
	if (IFileManager::Get().Copy(*TargetPath, *SourcePath, true, true) != COPY_OK)
	{
		UE_LOG(LogUnrealCodex, Verbose, TEXT("Failed to copy Codex home file: %s"), *SourcePath);
	}
}

void SeedTempCodexHome(const FString& TargetHomePath)
{
	const FString SourceHomePath = GetDefaultCodexHomePath();
	if (SourceHomePath.IsEmpty() || !FPaths::DirectoryExists(SourceHomePath))
	{
		return;
	}

	CopyCodexHomeFileIfPresent(SourceHomePath, TargetHomePath, TEXT("auth.json"));
	CopyCodexHomeFileIfPresent(SourceHomePath, TargetHomePath, TEXT("cap_sid"));
	CopyCodexHomeFileIfPresent(SourceHomePath, TargetHomePath, TEXT(".codex-global-state.json"));
	CopyCodexHomeFileIfPresent(SourceHomePath, TargetHomePath, TEXT("config.toml"));
}

bool WriteMcpConfigToml(const FString& CodexHomePath, const FString& BridgePath)
{
	const FString ConfigPath = FPaths::Combine(CodexHomePath, TEXT("config.toml"));
	FString ExistingConfig;
	FFileHelper::LoadFileToString(ExistingConfig, *ConfigPath);
	ExistingConfig.TrimEndInline();
	if (!ExistingConfig.IsEmpty())
	{
		ExistingConfig += TEXT("\n\n");
	}

	const FString MergedConfigToml = ExistingConfig + FString::Printf(
		TEXT("[mcp_servers.unrealcodex]\n")
		TEXT("command = \"node\"\n")
		TEXT("args = [\"%s\"]\n\n")
		TEXT("[mcp_servers.unrealcodex.env]\n")
		TEXT("UNREAL_MCP_URL = \"http://localhost:%d\"\n"),
		*EscapeTomlString(BridgePath),
		UnrealCodexConstants::MCPServer::DefaultPort);

	if (!FFileHelper::SaveStringToFile(MergedConfigToml, *ConfigPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogUnrealCodex, Warning, TEXT("Failed to write Codex config.toml to: %s"), *ConfigPath);
		return false;
	}

	UE_LOG(LogUnrealCodex, Log, TEXT("Codex MCP config written to: %s"), *ConfigPath);
	return true;
}
}

FCodexCodeRunner::FCodexCodeRunner()
	: Thread(nullptr)
	, bIsExecuting(false)
	, ReadPipe(nullptr)
	, WritePipe(nullptr)
	, StdInReadPipe(nullptr)
	, StdInWritePipe(nullptr)
{
}

FCodexCodeRunner::~FCodexCodeRunner()
{
	StopTaskCounter.Set(1);

	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;
		Thread = nullptr;
	}

	CleanupHandles();
}

void FCodexCodeRunner::CleanupHandles()
{
	if (ReadPipe || WritePipe)
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		ReadPipe = nullptr;
		WritePipe = nullptr;
	}

	if (StdInReadPipe || StdInWritePipe)
	{
		FPlatformProcess::ClosePipe(StdInReadPipe, StdInWritePipe);
		StdInReadPipe = nullptr;
		StdInWritePipe = nullptr;
	}

	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::CloseProc(ProcessHandle);
	}
}

bool FCodexCodeRunner::IsCodexAvailable()
{
	return !GetCodexPath().IsEmpty();
}

FString FCodexCodeRunner::GetCodexPath()
{
	static FString CachedCodexPath;
	static bool bHasSearched = false;

	if (bHasSearched && !CachedCodexPath.IsEmpty())
	{
		return CachedCodexPath;
	}

	bHasSearched = true;

	TArray<FString> PossiblePaths;

#if PLATFORM_WINDOWS
	const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfile.IsEmpty())
	{
		PossiblePaths.Add(FPaths::Combine(UserProfile, TEXT(".local"), TEXT("bin"), TEXT("codex.exe")));
		PossiblePaths.Add(FPaths::Combine(UserProfile, TEXT("AppData"), TEXT("Roaming"), TEXT("npm"), TEXT("codex.cmd")));
	}

	const FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
	if (!AppData.IsEmpty())
	{
		PossiblePaths.Add(FPaths::Combine(AppData, TEXT("npm"), TEXT("codex.cmd")));
	}

	const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
	if (!LocalAppData.IsEmpty())
	{
		PossiblePaths.Add(FPaths::Combine(LocalAppData, TEXT("npm"), TEXT("codex.cmd")));
	}

	FString PathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	TArray<FString> PathDirs;
	PathEnv.ParseIntoArray(PathDirs, TEXT(";"), true);
	for (const FString& Dir : PathDirs)
	{
		PossiblePaths.Add(FPaths::Combine(Dir, TEXT("codex.cmd")));
		PossiblePaths.Add(FPaths::Combine(Dir, TEXT("codex.exe")));
	}
#else
	const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!Home.IsEmpty())
	{
		PossiblePaths.Add(FPaths::Combine(Home, TEXT(".local"), TEXT("bin"), TEXT("codex")));
		PossiblePaths.Add(FPaths::Combine(Home, TEXT(".npm-global"), TEXT("bin"), TEXT("codex")));
	}

	PossiblePaths.Add(TEXT("/usr/local/bin/codex"));
	PossiblePaths.Add(TEXT("/usr/bin/codex"));

	FString PathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	TArray<FString> PathDirs;
	PathEnv.ParseIntoArray(PathDirs, TEXT(":"), true);
	for (const FString& Dir : PathDirs)
	{
		PossiblePaths.Add(FPaths::Combine(Dir, TEXT("codex")));
	}
#endif

	for (const FString& Path : PossiblePaths)
	{
		if (IFileManager::Get().FileExists(*Path))
		{
			CachedCodexPath = Path;
			UE_LOG(LogUnrealCodex, Log, TEXT("Found Codex CLI at: %s"), *CachedCodexPath);
			return CachedCodexPath;
		}
	}

	FString WhereOutput;
	FString WhereErrors;
	int32 ReturnCode = 0;

#if PLATFORM_WINDOWS
	const TCHAR* WhichCmd = TEXT("where");
	const TCHAR* WhichArgs = TEXT("codex");
#else
	const TCHAR* WhichCmd = TEXT("/bin/sh");
	const TCHAR* WhichArgs = TEXT("-c 'which codex 2>/dev/null'");
#endif

	if (FPlatformProcess::ExecProcess(WhichCmd, WhichArgs, &ReturnCode, &WhereOutput, &WhereErrors) && ReturnCode == 0)
	{
		WhereOutput.TrimStartAndEndInline();
		TArray<FString> Lines;
		WhereOutput.ParseIntoArrayLines(Lines);
		if (Lines.Num() > 0)
		{
			CachedCodexPath = Lines[0];
			UE_LOG(LogUnrealCodex, Log, TEXT("Found Codex CLI via lookup: %s"), *CachedCodexPath);
			return CachedCodexPath;
		}
	}

	UE_LOG(LogUnrealCodex, Warning, TEXT("Codex CLI not found. Install the Codex CLI, then run `codex login`."));
	return CachedCodexPath;
}

bool FCodexCodeRunner::ExecuteAsync(
	const FCodexRequestConfig& Config,
	FOnCodexResponse OnComplete,
	FOnCodexProgress OnProgress)
{
	bool Expected = false;
	if (!bIsExecuting.CompareExchange(Expected, true))
	{
		UE_LOG(LogUnrealCodex, Warning, TEXT("Codex is already executing a request"));
		return false;
	}

	if (!IsCodexAvailable())
	{
		bIsExecuting = false;
		OnComplete.ExecuteIfBound(TEXT("Codex CLI not found. Install the Codex CLI, then run `codex login`."), false);
		return false;
	}

	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;
		Thread = nullptr;
	}

	CurrentConfig = Config;
	OnCompleteDelegate = OnComplete;
	OnProgressDelegate = OnProgress;

	Thread = FRunnableThread::Create(this, TEXT("CodexCodeRunner"), 0, TPri_Normal);
	if (!Thread)
	{
		bIsExecuting = false;
		return false;
	}

	return true;
}

bool FCodexCodeRunner::ExecuteSync(const FCodexRequestConfig& Config, FString& OutResponse)
{
	OutResponse = TEXT("Synchronous Codex execution is not supported by UnrealCodex. Use ExecuteAsync instead.");
	return false;
}

FString FCodexCodeRunner::BuildCommandLine(const FCodexRequestConfig& Config)
{
	FString CommandLine = TEXT("exec --skip-git-repo-check --json ");

	if (Config.bSkipPermissions)
	{
		CommandLine += TEXT("--full-auto ");
	}
	else
	{
		CommandLine += TEXT("--sandbox workspace-write ");
	}

	const FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealCodex"));
	IFileManager::Get().MakeDirectory(*TempDir, true);
	TempCodexHomePath.Empty();
	OutputLastMessageFilePath = FPaths::Combine(
		TempDir,
		FString::Printf(TEXT("last-message-%s.txt"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	CommandLine += FString::Printf(
		TEXT("--output-last-message %s "),
		*QuoteCliArg(NormalizeCliPath(OutputLastMessageFilePath)));

	const FString PluginDir = GetPluginDirectory();
	if (!PluginDir.IsEmpty())
	{
		const FString BridgePath = FPaths::Combine(PluginDir, TEXT("Resources"), TEXT("mcp-bridge"), TEXT("index.js"));
		if (FPaths::FileExists(BridgePath))
		{
			const FString NormalizedBridgePath = NormalizeCliPath(BridgePath);
			const FString CandidateCodexHomePath = FPaths::Combine(
				TempDir,
				FString::Printf(TEXT("codex-home-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			IFileManager::Get().MakeDirectory(*CandidateCodexHomePath, true);
			SeedTempCodexHome(CandidateCodexHomePath);
			if (WriteMcpConfigToml(CandidateCodexHomePath, NormalizedBridgePath))
			{
				TempCodexHomePath = CandidateCodexHomePath;
			}
			else
			{
				IFileManager::Get().DeleteDirectory(*CandidateCodexHomePath, false, true);
			}
		}
		else
		{
			UE_LOG(LogUnrealCodex, Warning, TEXT("MCP bridge not found at: %s"), *BridgePath);
		}
	}

	for (const FString& ImagePath : Config.AttachedImagePaths)
	{
		if (!ImagePath.IsEmpty() && FPaths::FileExists(ImagePath))
		{
			CommandLine += FString::Printf(
				TEXT("--image %s "),
				*QuoteCliArg(NormalizeCliPath(ImagePath)));
		}
	}

	CommandLine += TEXT("- ");
	return CommandLine;
}

FString FCodexCodeRunner::BuildPromptText(const FCodexRequestConfig& Config) const
{
	if (Config.SystemPrompt.IsEmpty())
	{
		return Config.Prompt;
	}

	return FString::Printf(
		TEXT("[System Context]\n%s\n\n[User Request]\n%s"),
		*Config.SystemPrompt,
		*Config.Prompt);
}

FString FCodexCodeRunner::BuildStreamJsonPayload(const FString& TextPrompt, const TArray<FString>& ImagePaths)
{
	using namespace UnrealCodexConstants::ClipboardImage;

	const FString ExpectedDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealCodex"), TEXT("screenshots")));

	TArray<TSharedPtr<FJsonValue>> ContentArray;

	if (!TextPrompt.IsEmpty())
	{
		TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
		TextBlock->SetStringField(TEXT("type"), TEXT("text"));
		TextBlock->SetStringField(TEXT("text"), TextPrompt);
		ContentArray.Add(MakeShared<FJsonValueObject>(TextBlock));
	}

	int32 EncodedCount = 0;
	int64 TotalImageBytes = 0;
	const int32 MaxCount = FMath::Min(ImagePaths.Num(), MaxImagesPerMessage);

	for (int32 Index = 0; Index < MaxCount; ++Index)
	{
		const FString& ImagePath = ImagePaths[Index];
		if (ImagePath.IsEmpty())
		{
			continue;
		}

		const FString FullImagePath = FPaths::ConvertRelativePathToFull(ImagePath);
		if (FullImagePath.Contains(TEXT("..")) || !FullImagePath.StartsWith(ExpectedDir) || !FPaths::FileExists(FullImagePath))
		{
			continue;
		}

		const int64 FileSize = IFileManager::Get().FileSize(*FullImagePath);
		if (FileSize > MaxImageFileSize || TotalImageBytes + FileSize > MaxTotalImagePayloadSize)
		{
			continue;
		}

		TArray<uint8> ImageData;
		if (!FFileHelper::LoadFileToArray(ImageData, *FullImagePath))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Source = MakeShared<FJsonObject>();
		Source->SetStringField(TEXT("type"), TEXT("base64"));
		Source->SetStringField(TEXT("media_type"), TEXT("image/png"));
		Source->SetStringField(TEXT("data"), FBase64::Encode(ImageData));

		TSharedPtr<FJsonObject> ImageBlock = MakeShared<FJsonObject>();
		ImageBlock->SetStringField(TEXT("type"), TEXT("image"));
		ImageBlock->SetObjectField(TEXT("source"), Source);
		ContentArray.Add(MakeShared<FJsonValueObject>(ImageBlock));

		TotalImageBytes += FileSize;
		EncodedCount++;
	}

	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("role"), TEXT("user"));
	Message->SetArrayField(TEXT("content"), ContentArray);

	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetStringField(TEXT("type"), TEXT("user"));
	Envelope->SetObjectField(TEXT("message"), Message);

	FString JsonLine;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonLine);
	FJsonSerializer::Serialize(Envelope.ToSharedRef(), Writer);
	Writer->Close();

	JsonLine += TEXT("\n");
	UE_LOG(LogUnrealCodex, Verbose, TEXT("Built legacy image payload: %d chars (%d images)"), JsonLine.Len(), EncodedCount);
	return JsonLine;
}

FString FCodexCodeRunner::ParseStreamJsonOutput(const FString& RawOutput)
{
	TArray<FString> Lines;
	RawOutput.ParseIntoArrayLines(Lines);

	for (const FString& Line : Lines)
	{
		if (Line.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* MessageObj = nullptr;
		if (JsonObj->TryGetObjectField(TEXT("msg"), MessageObj) && MessageObj && (*MessageObj).IsValid())
		{
			JsonObj = *MessageObj;
		}

		FString Type;
		if (!JsonObj->TryGetStringField(TEXT("type"), Type) || Type != TEXT("item.completed"))
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* ItemObj = nullptr;
		if (!JsonObj->TryGetObjectField(TEXT("item"), ItemObj) || !ItemObj || !(*ItemObj).IsValid())
		{
			continue;
		}

		FString ItemType;
		if ((*ItemObj)->TryGetStringField(TEXT("type"), ItemType) && ItemType == TEXT("agent_message"))
		{
			const FString Text = ExtractItemText(*ItemObj);
			if (!Text.IsEmpty())
			{
				return Text;
			}
		}
	}

	UE_LOG(LogUnrealCodex, Warning, TEXT("Failed to parse Codex JSONL output (%d chars)."), RawOutput.Len());
	UE_LOG(LogUnrealCodex, Warning, TEXT("%s"), *RawOutput.Left(2000));
	return TEXT("Error: Failed to parse Codex's response. Check the Output Log for details.");
}

void FCodexCodeRunner::ParseAndEmitJsonlLine(const FString& JsonLine)
{
	if (JsonLine.IsEmpty())
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		UE_LOG(LogUnrealCodex, Verbose, TEXT("JSONL: Non-JSON line (skipping): %.200s"), *JsonLine);
		return;
	}

	const TSharedPtr<FJsonObject>* MessageObj = nullptr;
	if (JsonObj->TryGetObjectField(TEXT("msg"), MessageObj) && MessageObj && (*MessageObj).IsValid())
	{
		JsonObj = *MessageObj;
	}

	FString Type;
	if (!JsonObj->TryGetStringField(TEXT("type"), Type))
	{
		return;
	}

	if (Type == TEXT("thread.started"))
	{
		if (CurrentConfig.OnStreamEvent.IsBound())
		{
			FCodexStreamEvent Event;
			Event.Type = ECodexStreamEventType::SessionInit;
			JsonObj->TryGetStringField(TEXT("thread_id"), Event.SessionId);
			Event.RawJson = JsonLine;
			FOnCodexStreamEvent EventDelegate = CurrentConfig.OnStreamEvent;
			AsyncTask(ENamedThreads::GameThread, [EventDelegate, Event]()
			{
				EventDelegate.ExecuteIfBound(Event);
			});
		}
		return;
	}

	if (Type == TEXT("item.started") || Type == TEXT("item.updated") || Type == TEXT("item.completed"))
	{
		const TSharedPtr<FJsonObject>* ItemObj = nullptr;
		if (!JsonObj->TryGetObjectField(TEXT("item"), ItemObj) || !ItemObj || !(*ItemObj).IsValid())
		{
			return;
		}

		FString ItemType;
		if (!(*ItemObj)->TryGetStringField(TEXT("type"), ItemType))
		{
			return;
		}

		FString ItemId;
		(*ItemObj)->TryGetStringField(TEXT("id"), ItemId);

		if (ItemType == TEXT("agent_message"))
		{
			FString Text;
			if (Type == TEXT("item.updated"))
			{
				JsonObj->TryGetStringField(TEXT("delta"), Text);
			}
			else if (Type == TEXT("item.completed"))
			{
				Text = ExtractItemText(*ItemObj);
			}

			if (Text.IsEmpty())
			{
				return;
			}

			AccumulatedResponseText += Text;

			if (OnProgressDelegate.IsBound())
			{
				FOnCodexProgress ProgressCopy = OnProgressDelegate;
				AsyncTask(ENamedThreads::GameThread, [ProgressCopy, Text]()
				{
					ProgressCopy.ExecuteIfBound(Text);
				});
			}

			if (CurrentConfig.OnStreamEvent.IsBound())
			{
				FCodexStreamEvent Event;
				Event.Type = ECodexStreamEventType::TextContent;
				Event.Text = Text;
				Event.RawJson = JsonLine;
				FOnCodexStreamEvent EventDelegate = CurrentConfig.OnStreamEvent;
				AsyncTask(ENamedThreads::GameThread, [EventDelegate, Event]()
				{
					EventDelegate.ExecuteIfBound(Event);
				});
			}

			return;
		}

		if (!IsToolItemType(ItemType) || !CurrentConfig.OnStreamEvent.IsBound())
		{
			return;
		}

		if (Type == TEXT("item.started"))
		{
			FCodexStreamEvent Event;
			Event.Type = ECodexStreamEventType::ToolUse;
			Event.ToolName = GetToolName(*ItemObj, ItemType);
			Event.ToolCallId = ItemId;
			Event.ToolInput = SerializeJsonObject(*ItemObj);
			Event.RawJson = JsonLine;
			FOnCodexStreamEvent EventDelegate = CurrentConfig.OnStreamEvent;
			AsyncTask(ENamedThreads::GameThread, [EventDelegate, Event]()
			{
				EventDelegate.ExecuteIfBound(Event);
			});
		}
		else if (Type == TEXT("item.completed"))
		{
			FCodexStreamEvent Event;
			Event.Type = ECodexStreamEventType::ToolResult;
			Event.ToolName = GetToolName(*ItemObj, ItemType);
			Event.ToolCallId = ItemId;
			Event.ToolResultContent = GetToolResultContent(*ItemObj);
			Event.RawJson = JsonLine;
			FOnCodexStreamEvent EventDelegate = CurrentConfig.OnStreamEvent;
			AsyncTask(ENamedThreads::GameThread, [EventDelegate, Event]()
			{
				EventDelegate.ExecuteIfBound(Event);
			});
		}

		return;
	}

	if (Type == TEXT("turn.completed") || Type == TEXT("exec.completed"))
	{
		if (CurrentConfig.OnStreamEvent.IsBound())
		{
			FCodexStreamEvent Event;
			Event.Type = ECodexStreamEventType::Result;
			Event.ResultText = AccumulatedResponseText;
			Event.DurationMs = FMath::Max(0, static_cast<int32>((FPlatformTime::Seconds() - RequestStartTimeSeconds) * 1000.0));
			Event.NumTurns = 1;
			Event.RawJson = JsonLine;
			FOnCodexStreamEvent EventDelegate = CurrentConfig.OnStreamEvent;
			AsyncTask(ENamedThreads::GameThread, [EventDelegate, Event]()
			{
				EventDelegate.ExecuteIfBound(Event);
			});
		}
		return;
	}

	if (Type == TEXT("turn.failed") || Type == TEXT("exec.failed"))
	{
		if (CurrentConfig.OnStreamEvent.IsBound())
		{
			FCodexStreamEvent Event;
			Event.Type = ECodexStreamEventType::Result;
			Event.bIsError = true;
			Event.ResultText = ExtractItemText(JsonObj);
			Event.DurationMs = FMath::Max(0, static_cast<int32>((FPlatformTime::Seconds() - RequestStartTimeSeconds) * 1000.0));
			Event.RawJson = JsonLine;
			FOnCodexStreamEvent EventDelegate = CurrentConfig.OnStreamEvent;
			AsyncTask(ENamedThreads::GameThread, [EventDelegate, Event]()
			{
				EventDelegate.ExecuteIfBound(Event);
			});
		}
		return;
	}

	if (Type == TEXT("error"))
	{
		FString ErrorText = ExtractItemText(JsonObj);
		if (ErrorText.IsEmpty())
		{
			ErrorText = SerializeJsonObject(JsonObj);
		}

		UE_LOG(LogUnrealCodex, Warning, TEXT("Codex stream warning: %s"), *ErrorText.Left(1000));
	}
}

void FCodexCodeRunner::Cancel()
{
	StopTaskCounter.Set(1);
	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(ProcessHandle, true);
	}
}

bool FCodexCodeRunner::Init()
{
	StopTaskCounter.Reset();
	JsonlLineBuffer.Empty();
	AccumulatedResponseText.Empty();
	RequestStartTimeSeconds = FPlatformTime::Seconds();
	return true;
}

uint32 FCodexCodeRunner::Run()
{
	ExecuteProcess();
	return 0;
}

void FCodexCodeRunner::Stop()
{
	StopTaskCounter.Increment();
}

void FCodexCodeRunner::Exit()
{
	bIsExecuting = false;
}

bool FCodexCodeRunner::CreateProcessPipes()
{
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe, false))
	{
		UE_LOG(LogUnrealCodex, Error, TEXT("Failed to create stdout pipe"));
		return false;
	}

	if (!FPlatformProcess::CreatePipe(StdInReadPipe, StdInWritePipe, true))
	{
		UE_LOG(LogUnrealCodex, Error, TEXT("Failed to create stdin pipe"));
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		ReadPipe = nullptr;
		WritePipe = nullptr;
		return false;
	}

	return true;
}

bool FCodexCodeRunner::LaunchProcess(const FString& FullCommand, const FString& WorkingDir)
{
	const FString CodexPath = GetCodexPath();
	FString ExecutablePath = CodexPath;
	FString ProcessArgs = FullCommand;
	const FString PreviousCodexHome = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_HOME"));

	if (!TempCodexHomePath.IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("CODEX_HOME"), *FPaths::ConvertRelativePathToFull(TempCodexHomePath));
	}

#if PLATFORM_WINDOWS
	if (IsCmdScriptPath(CodexPath))
	{
		ExecutablePath = FPlatformMisc::GetEnvironmentVariable(TEXT("ComSpec"));
		if (ExecutablePath.IsEmpty())
		{
			ExecutablePath = TEXT("C:/Windows/System32/cmd.exe");
		}

		ProcessArgs = FString::Printf(TEXT("/d /s /c \"\"%s\" %s\""), *CodexPath, *FullCommand);
	}
#endif

	ProcessHandle = FPlatformProcess::CreateProc(
		*ExecutablePath,
		*ProcessArgs,
		false,
		false,
		true,
		nullptr,
		0,
		*WorkingDir,
		WritePipe,
		StdInReadPipe
	);

	if (!TempCodexHomePath.IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("CODEX_HOME"), *PreviousCodexHome);
	}

	if (!ProcessHandle.IsValid())
	{
		UE_LOG(LogUnrealCodex, Error, TEXT("Failed to create Codex process"));
		UE_LOG(LogUnrealCodex, Error, TEXT("Codex Path: %s"), *CodexPath);
		UE_LOG(LogUnrealCodex, Error, TEXT("Executable: %s"), *ExecutablePath);
		UE_LOG(LogUnrealCodex, Error, TEXT("Params: %s"), *ProcessArgs);
		UE_LOG(LogUnrealCodex, Error, TEXT("Working directory: %s"), *WorkingDir);
		return false;
	}

	return true;
}

FString FCodexCodeRunner::ReadProcessOutput()
{
	FString FullOutput;
	JsonlLineBuffer.Empty();
	AccumulatedResponseText.Empty();

	while (!StopTaskCounter.GetValue())
	{
		const FString OutputChunk = FPlatformProcess::ReadPipe(ReadPipe);
		if (!OutputChunk.IsEmpty())
		{
			FullOutput += OutputChunk;
			JsonlLineBuffer += OutputChunk;

			int32 NewlineIdx = INDEX_NONE;
			while (JsonlLineBuffer.FindChar(TEXT('\n'), NewlineIdx))
			{
				FString CompleteLine = JsonlLineBuffer.Left(NewlineIdx);
				CompleteLine.TrimEndInline();
				JsonlLineBuffer.RightChopInline(NewlineIdx + 1);

				if (!CompleteLine.IsEmpty())
				{
					ParseAndEmitJsonlLine(CompleteLine);
				}
			}
		}

		if (!FPlatformProcess::IsProcRunning(ProcessHandle))
		{
			FString RemainingOutput = FPlatformProcess::ReadPipe(ReadPipe);
			while (!RemainingOutput.IsEmpty())
			{
				FullOutput += RemainingOutput;
				JsonlLineBuffer += RemainingOutput;
				RemainingOutput = FPlatformProcess::ReadPipe(ReadPipe);
			}

			int32 FinalNewlineIdx = INDEX_NONE;
			while (JsonlLineBuffer.FindChar(TEXT('\n'), FinalNewlineIdx))
			{
				FString CompleteLine = JsonlLineBuffer.Left(FinalNewlineIdx);
				CompleteLine.TrimEndInline();
				JsonlLineBuffer.RightChopInline(FinalNewlineIdx + 1);

				if (!CompleteLine.IsEmpty())
				{
					ParseAndEmitJsonlLine(CompleteLine);
				}
			}

			JsonlLineBuffer.TrimEndInline();
			if (!JsonlLineBuffer.IsEmpty())
			{
				ParseAndEmitJsonlLine(JsonlLineBuffer);
				JsonlLineBuffer.Empty();
			}

			break;
		}

		FPlatformProcess::Sleep(0.01f);
	}

	return FullOutput;
}

void FCodexCodeRunner::ReportError(const FString& ErrorMessage)
{
	FOnCodexResponse CompleteCopy = OnCompleteDelegate;
	const FString Message = ErrorMessage;
	AsyncTask(ENamedThreads::GameThread, [CompleteCopy, Message]()
	{
		CompleteCopy.ExecuteIfBound(Message, false);
	});
}

void FCodexCodeRunner::ReportCompletion(const FString& Output, bool bSuccess)
{
	FOnCodexResponse CompleteCopy = OnCompleteDelegate;
	const FString FinalOutput = Output;
	AsyncTask(ENamedThreads::GameThread, [CompleteCopy, FinalOutput, bSuccess]()
	{
		CompleteCopy.ExecuteIfBound(FinalOutput, bSuccess);
	});
}

void FCodexCodeRunner::ExecuteProcess()
{
	const FString CodexPath = GetCodexPath();
	if (CodexPath.IsEmpty())
	{
		ReportError(TEXT("Codex CLI not found. Install the Codex CLI, then run `codex login`."));
		return;
	}

	if (!IFileManager::Get().FileExists(*CodexPath))
	{
		ReportError(FString::Printf(TEXT("Codex CLI path invalid: %s"), *CodexPath));
		return;
	}

	const FString CommandLine = BuildCommandLine(CurrentConfig);
	FString WorkingDir = CurrentConfig.WorkingDirectory;
	if (WorkingDir.IsEmpty())
	{
		WorkingDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	}

	if (!CreateProcessPipes())
	{
		ReportError(TEXT("Failed to create pipe for Codex process"));
		return;
	}

	if (!LaunchProcess(CommandLine, WorkingDir))
	{
		CleanupHandles();
		ReportError(TEXT("Failed to start Codex process."));
		return;
	}

	if (StdInWritePipe)
	{
		const FString PromptText = BuildPromptText(CurrentConfig);
		if (!PromptText.IsEmpty())
		{
			FTCHARToUTF8 Utf8Payload(*PromptText);
			int32 BytesWritten = 0;
			FPlatformProcess::WritePipe(
				StdInWritePipe,
				reinterpret_cast<const uint8*>(Utf8Payload.Get()),
				Utf8Payload.Length(),
				&BytesWritten);
		}

		FPlatformProcess::ClosePipe(StdInReadPipe, StdInWritePipe);
		StdInReadPipe = nullptr;
		StdInWritePipe = nullptr;
	}

	const FString FullOutput = ReadProcessOutput();

	int32 ExitCode = 0;
	FPlatformProcess::GetProcReturnCode(ProcessHandle, &ExitCode);

	FString ResponseText = AccumulatedResponseText;
	if (ExitCode == 0 && ResponseText.IsEmpty() && !OutputLastMessageFilePath.IsEmpty())
	{
		FFileHelper::LoadFileToString(ResponseText, *OutputLastMessageFilePath);
		ResponseText.TrimStartAndEndInline();
	}

	if (ResponseText.IsEmpty() && !FullOutput.IsEmpty())
	{
		if (ExitCode == 0)
		{
			ResponseText = ParseStreamJsonOutput(FullOutput);
		}
		else
		{
			ResponseText = FullOutput;
			ResponseText.TrimStartAndEndInline();
		}
	}

	if (ReadPipe || WritePipe)
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		ReadPipe = nullptr;
		WritePipe = nullptr;
	}
	FPlatformProcess::CloseProc(ProcessHandle);

	if (!OutputLastMessageFilePath.IsEmpty())
	{
		IFileManager::Get().Delete(*OutputLastMessageFilePath, false, true, true);
		OutputLastMessageFilePath.Empty();
	}

	if (!TempCodexHomePath.IsEmpty())
	{
		IFileManager::Get().DeleteDirectory(*TempCodexHomePath, false, true);
		TempCodexHomePath.Empty();
	}

	const bool bSuccess = (ExitCode == 0) && !StopTaskCounter.GetValue();
	if (!bSuccess && ResponseText.IsEmpty())
	{
		ResponseText = TEXT("Codex execution failed. Check the Output Log for details.");
	}

	ReportCompletion(ResponseText, bSuccess);
}

// FCodexCodeSubsystem is now in CodexSubsystem.cpp
