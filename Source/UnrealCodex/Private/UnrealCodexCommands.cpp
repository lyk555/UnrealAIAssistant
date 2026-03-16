// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealCodexCommands.h"

#define LOCTEXT_NAMESPACE "UnrealCodex"

void FUnrealCodexCommands::RegisterCommands()
{
	UI_COMMAND(
		OpenCodexPanel,
		"Codex Assistant",
		"Open the Codex AI Assistant panel for UE5.5 help",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		QuickAsk,
		"Quick Ask Codex",
		"Quickly ask Codex a question",
		EUserInterfaceActionType::Button,
		FInputChord()
	);
}

#undef LOCTEXT_NAMESPACE
