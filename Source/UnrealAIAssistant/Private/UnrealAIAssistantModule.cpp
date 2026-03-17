// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealAIAssistantModule.h"
#include "UnrealAIAssistantCommands.h"
#include "AssistantEditorWidget.h"
#include "AssistantCodeRunner.h"
#include "AssistantSubsystem.h"
#include "ScriptExecutionManager.h"
#include "MCP/UnrealAIAssistantMCPServer.h"
#include "ProjectContext.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HttpServerModule.h"

DEFINE_LOG_CATEGORY(LogUnrealAIAssistant);

#define LOCTEXT_NAMESPACE "FUnrealAIAssistantModule"

static const FName CodexTabName("CodexAssistant");

void FUnrealAIAssistantModule::StartupModule()
{
	UE_LOG(LogUnrealAIAssistant, Warning, TEXT("=== UnrealAIAssistant BUILD 20260107-1450 THREAD_TESTS_DISABLED ==="));
	
	// Register commands
	FUnrealAIAssistantCommands::Register();
	
	PluginCommands = MakeShared<FUICommandList>();
	
	// Map commands to actions
	PluginCommands->MapAction(
		FUnrealAIAssistantCommands::Get().OpenCodexPanel,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(CodexTabName);
		}),
		FCanExecuteAction()
	);

	// Map QuickAsk command - shows a popup for quick questions
	PluginCommands->MapAction(
		FUnrealAIAssistantCommands::Get().QuickAsk,
		FExecuteAction::CreateLambda([]()
		{
			// Create a simple input dialog
			TSharedRef<SWindow> QuickAskWindow = SNew(SWindow)
				.Title(LOCTEXT("QuickAskTitle", "quick ask assistant"))
				.ClientSize(FVector2D(500, 100))
				.SupportsMinimize(false)
				.SupportsMaximize(false);

			TSharedPtr<SEditableTextBox> InputBox;

			QuickAskWindow->SetContent(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(10)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("QuickAskLabel", "Ask assistant a quick question:"))
				]
				+ SVerticalBox::Slot()
				.Padding(10, 0, 10, 10)
				.FillHeight(1.0f)
				[
					SAssignNew(InputBox, SEditableTextBox)
					.HintText(LOCTEXT("QuickAskHint", "Type your question here..."))
					.OnTextCommitted_Lambda([QuickAskWindow](const FText& Text, ETextCommit::Type CommitType)
					{
						if (CommitType == ETextCommit::OnEnter && !Text.IsEmpty())
						{
							// Close the window
							QuickAskWindow->RequestDestroyWindow();

							// Send prompt to Codex
							FString Prompt = Text.ToString();
							FCodexPromptOptions Options;
							Options.bIncludeEngineContext = true;
							Options.bIncludeProjectContext = true;
							FCodexCodeSubsystem::Get().SendPrompt(
								Prompt,
								FOnCodexResponse::CreateLambda([](const FString& Response, bool bSuccess)
								{
									// Show response in notification
									FNotificationInfo Info(FText::FromString(
										bSuccess
											? (Response.Len() > 300 ? Response.Left(300) + TEXT("...") : Response)
											: TEXT("Error: ") + Response));
									Info.ExpireDuration = bSuccess ? 15.0f : 5.0f;
									Info.bUseLargeFont = false;
									Info.bUseSuccessFailIcons = true;
									FSlateNotificationManager::Get().AddNotification(Info);
								}),
								Options
							);
						}
					})
				]
			);

			FSlateApplication::Get().AddWindow(QuickAskWindow);

			// Focus the input box
			if (InputBox.IsValid())
			{
				FSlateApplication::Get().SetKeyboardFocus(InputBox);
			}
		}),
		FCanExecuteAction::CreateLambda([]()
		{
			return FCodexCodeSubsystem::Get().HasAnyAvailableProvider();
		})
	);

	// Register the tab spawner
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		CodexTabName,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				.Label(LOCTEXT("CodexTabTitle", "assistant"))
				[
					SNew(SAssistantEditorWidget)
				];
		}))
		.SetDisplayName(LOCTEXT("CodexTabTitle", "assistant"))
		.SetTooltipText(LOCTEXT("CodexTabTooltip", "Open the assistant for UE5.5 development help"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"));
	
	// Register menus after engine init
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealAIAssistantModule::RegisterMenus));

	// Bind keyboard shortcuts to the Level Editor
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetGlobalLevelEditorActions()->Append(PluginCommands.ToSharedRef());

	// Check available providers
	if (FCodexCodeSubsystem::Get().HasAnyAvailableProvider())
	{
		UE_LOG(LogUnrealAIAssistant, Log, TEXT("Active UnrealAIAssistant provider: %s"), *FCodexCodeSubsystem::Get().GetActiveProviderDisplayName());
	}
	else
	{
		UE_LOG(LogUnrealAIAssistant, Warning, TEXT("No supported AI backend was found. Install a supported CLI backend to enable assistant."));
	}

	// Start MCP Server
	StartMCPServer();

	// Initialize project context (async, will gather in background)
	FProjectContextManager::Get().RefreshContext();

	// Initialize script execution manager (creates script directories)
	FScriptExecutionManager::Get();
}

void FUnrealAIAssistantModule::ShutdownModule()
{
	UE_LOG(LogUnrealAIAssistant, Log, TEXT("UnrealAIAssistant module shutting down"));

	// Stop MCP Server
	StopMCPServer();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FUnrealAIAssistantCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CodexTabName);
}

FUnrealAIAssistantModule& FUnrealAIAssistantModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUnrealAIAssistantModule>("UnrealAIAssistant");
}

bool FUnrealAIAssistantModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UnrealAIAssistant");
}

void FUnrealAIAssistantModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	
	// Add to the main menu bar under Tools
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->FindOrAddSection("UnrealAIAssistant");

		Section.AddMenuEntryWithCommandList(
			FUnrealAIAssistantCommands::Get().OpenCodexPanel,
			PluginCommands,
			LOCTEXT("OpenCodexMenuItem", "assistant"),
			LOCTEXT("OpenCodexMenuItemTooltip", "Open the assistant for UE5.5 help (Ctrl+Shift+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		);

		Section.AddMenuEntryWithCommandList(
			FUnrealAIAssistantCommands::Get().QuickAsk,
			PluginCommands,
			LOCTEXT("QuickAskMenuItem", "quick ask assistant"),
			LOCTEXT("QuickAskMenuItemTooltip", "Quickly ask assistant a question (Ctrl+Alt+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		);
	}
	
	// Add to the toolbar
	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("UnrealAIAssistant");
		
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			FUnrealAIAssistantCommands::Get().OpenCodexPanel,
			LOCTEXT("CodexToolbarButton", "assistant"),
			LOCTEXT("CodexToolbarTooltip", "Open assistant (Ctrl+Shift+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		));
	}
}

void FUnrealAIAssistantModule::UnregisterMenus()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FUnrealAIAssistantModule::StartMCPServer()
{
	if (MCPServer.IsValid())
	{
		UE_LOG(LogUnrealAIAssistant, Warning, TEXT("MCP Server already exists"));
		return;
	}

	MCPServer = MakeShared<FUnrealAIAssistantMCPServer>();

	if (!MCPServer->Start(GetMCPServerPort()))
	{
		UE_LOG(LogUnrealAIAssistant, Error, TEXT("Failed to start MCP Server on port %d"), GetMCPServerPort());
		MCPServer.Reset();
	}
}

void FUnrealAIAssistantModule::StopMCPServer()
{
	if (MCPServer.IsValid())
	{
		MCPServer->Stop();
		MCPServer.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUnrealAIAssistantModule, UnrealAIAssistant)
