// Copyright Natali Caggiano. All Rights Reserved.

#include "AssistantSessionManager.h"
#include "UnrealAIAssistantConstants.h"
#include "JsonUtils.h"
#include "UnrealAIAssistantModule.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

FAssistantSessionManager::FAssistantSessionManager()
	: MaxHistorySize(UnrealAIAssistantConstants::Session::MaxHistorySize)
{
}

void FAssistantSessionManager::AddExchange(const FString& Prompt, const FString& Response)
{
	ConversationHistory.Add(TPair<FString, FString>(Prompt, Response));

	// Trim old history if exceeds max size
	while (ConversationHistory.Num() > MaxHistorySize)
	{
		ConversationHistory.RemoveAt(0);
	}
}

void FAssistantSessionManager::ClearHistory()
{
	ConversationHistory.Empty();
}

FString FAssistantSessionManager::GetSessionFilePath() const
{
	FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAIAssistant"));
	return FPaths::Combine(SaveDir, TEXT("session.json"));
}

bool FAssistantSessionManager::HasSavedSession() const
{
	return IFileManager::Get().FileExists(*GetSessionFilePath());
}

bool FAssistantSessionManager::SaveSession()
{
	if (ConversationHistory.Num() == 0)
	{
		return true; // Nothing to save
	}

	FString SessionPath = GetSessionFilePath();
	FString SaveDir = FPaths::GetPath(SessionPath);

	// Create directory if needed
	if (!IFileManager::Get().DirectoryExists(*SaveDir))
	{
		IFileManager::Get().MakeDirectory(*SaveDir, true);
	}

	// Build JSON structure
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();

	// Create messages array
	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const TPair<FString, FString>& Exchange : ConversationHistory)
	{
		TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
		MessageObject->SetStringField(TEXT("user"), Exchange.Key);
		MessageObject->SetStringField(TEXT("assistant"), Exchange.Value);
		MessagesArray.Add(MakeShared<FJsonValueObject>(MessageObject));
	}

	RootObject->SetArrayField(TEXT("messages"), MessagesArray);

	// Add timestamp
	FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y-%m-%dT%H:%M:%SZ"));
	RootObject->SetStringField(TEXT("last_updated"), Timestamp);

	// Serialize and write
	FString JsonString = FJsonUtils::Stringify(RootObject, true);
	if (JsonString.IsEmpty())
	{
		UE_LOG(LogUnrealAIAssistant, Error, TEXT("Failed to serialize session JSON"));
		return false;
	}

	if (!FFileHelper::SaveStringToFile(JsonString, *SessionPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogUnrealAIAssistant, Error, TEXT("Failed to save session to: %s"), *SessionPath);
		return false;
	}

	UE_LOG(LogUnrealAIAssistant, Log, TEXT("Session saved to: %s (%d messages)"), *SessionPath, ConversationHistory.Num());
	return true;
}

bool FAssistantSessionManager::LoadSession()
{
	FString SessionPath = GetSessionFilePath();

	if (!IFileManager::Get().FileExists(*SessionPath))
	{
		UE_LOG(LogUnrealAIAssistant, Log, TEXT("No previous session found at: %s"), *SessionPath);
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *SessionPath))
	{
		UE_LOG(LogUnrealAIAssistant, Error, TEXT("Failed to load session from: %s"), *SessionPath);
		return false;
	}

	TSharedPtr<FJsonObject> RootObject = FJsonUtils::Parse(JsonString);
	if (!RootObject.IsValid())
	{
		UE_LOG(LogUnrealAIAssistant, Error, TEXT("Failed to parse session JSON"));
		return false;
	}

	// Clear existing history
	ConversationHistory.Empty();

	// Load messages array
	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	if (FJsonUtils::GetArrayField(RootObject, TEXT("messages"), MessagesArray))
	{
		for (const TSharedPtr<FJsonValue>& MessageValue : MessagesArray)
		{
			const TSharedPtr<FJsonObject>* MessageObject;
			if (MessageValue->TryGetObject(MessageObject))
			{
				FString UserMessage, AssistantMessage;
				if (FJsonUtils::GetStringField(*MessageObject, TEXT("user"), UserMessage) &&
					FJsonUtils::GetStringField(*MessageObject, TEXT("assistant"), AssistantMessage))
				{
					ConversationHistory.Add(TPair<FString, FString>(UserMessage, AssistantMessage));
				}
			}
		}
	}

	FString LastUpdated;
	if (FJsonUtils::GetStringField(RootObject, TEXT("last_updated"), LastUpdated))
	{
		UE_LOG(LogUnrealAIAssistant, Log, TEXT("Session loaded from: %s (last updated: %s, %d messages)"),
			*SessionPath, *LastUpdated, ConversationHistory.Num());
	}
	else
	{
		UE_LOG(LogUnrealAIAssistant, Log, TEXT("Session loaded from: %s (%d messages)"),
			*SessionPath, ConversationHistory.Num());
	}

	return true;
}
