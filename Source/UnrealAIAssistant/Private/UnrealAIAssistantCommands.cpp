// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealAIAssistantCommands.h"

#define LOCTEXT_NAMESPACE "UnrealAIAssistant"

void FUnrealAIAssistantCommands::RegisterCommands()
{
	UI_COMMAND(
		OpenCodexPanel,
		"assistant",
		"Open the assistant panel for UE5.5 help",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		QuickAsk,
		"quick ask assistant",
		"Quickly ask assistant a question",
		EUserInterfaceActionType::Button,
		FInputChord()
	);
}

#undef LOCTEXT_NAMESPACE
