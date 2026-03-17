// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

/**
 * Editor commands for the Unreal Codex plugin
 */
class FUnrealAIAssistantCommands : public TCommands<FUnrealAIAssistantCommands>
{
public:
	FUnrealAIAssistantCommands()
		: TCommands<FUnrealAIAssistantCommands>(
			TEXT("UnrealAIAssistant"),
			NSLOCTEXT("Contexts", "UnrealAIAssistant", "UnrealAIAssistant"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	// TCommands interface
	virtual void RegisterCommands() override;

public:
	/** Open the Codex Assistant panel */
	TSharedPtr<FUICommandInfo> OpenCodexPanel;
	
	/** Quick ask - opens a small dialog for quick questions */
	TSharedPtr<FUICommandInfo> QuickAsk;
};
