// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealCodexModule.h"
#include "UnrealCodexCommands.h"
#include "CodexEditorWidget.h"
#include "CodexCodeRunner.h"
#include "CodexSubsystem.h"
#include "ScriptExecutionManager.h"
#include "MCP/UnrealCodexMCPServer.h"
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

DEFINE_LOG_CATEGORY(LogUnrealCodex);

#define LOCTEXT_NAMESPACE "FUnrealCodexModule"

static const FName CodexTabName("CodexAssistant");

void FUnrealCodexModule::StartupModule()
{
	UE_LOG(LogUnrealCodex, Warning, TEXT("=== UnrealCodex BUILD 20260107-1450 THREAD_TESTS_DISABLED ==="));
	
	// Register commands
	FUnrealCodexCommands::Register();
	
	PluginCommands = MakeShared<FUICommandList>();
	
	// Map commands to actions
	PluginCommands->MapAction(
		FUnrealCodexCommands::Get().OpenCodexPanel,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(CodexTabName);
		}),
		FCanExecuteAction()
	);

	// Map QuickAsk command - shows a popup for quick questions
	PluginCommands->MapAction(
		FUnrealCodexCommands::Get().QuickAsk,
		FExecuteAction::CreateLambda([]()
		{
			// Create a simple input dialog
			TSharedRef<SWindow> QuickAskWindow = SNew(SWindow)
				.Title(LOCTEXT("QuickAskTitle", "Quick Ask Codex"))
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
					.Text(LOCTEXT("QuickAskLabel", "Ask Codex a quick question:"))
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
			return FCodexCodeRunner::IsCodexAvailable();
		})
	);

	// Register the tab spawner
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		CodexTabName,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				.Label(LOCTEXT("CodexTabTitle", "Codex Assistant"))
				[
					SNew(SCodexEditorWidget)
				];
		}))
		.SetDisplayName(LOCTEXT("CodexTabTitle", "Codex Assistant"))
		.SetTooltipText(LOCTEXT("CodexTabTooltip", "Open the Codex AI Assistant for UE5.5 development help"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"));
	
	// Register menus after engine init
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealCodexModule::RegisterMenus));

	// Bind keyboard shortcuts to the Level Editor
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetGlobalLevelEditorActions()->Append(PluginCommands.ToSharedRef());

	// Check Codex availability
	if (FCodexCodeRunner::IsCodexAvailable())
	{
		UE_LOG(LogUnrealCodex, Log, TEXT("Codex CLI found at: %s"), *FCodexCodeRunner::GetCodexPath());
	}
	else
	{
		UE_LOG(LogUnrealCodex, Warning, TEXT("Codex CLI not found. Install the Codex CLI, then run `codex login`."));
	}

	// Start MCP Server
	StartMCPServer();

	// Initialize project context (async, will gather in background)
	FProjectContextManager::Get().RefreshContext();

	// Initialize script execution manager (creates script directories)
	FScriptExecutionManager::Get();
}

void FUnrealCodexModule::ShutdownModule()
{
	UE_LOG(LogUnrealCodex, Log, TEXT("UnrealCodex module shutting down"));

	// Stop MCP Server
	StopMCPServer();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FUnrealCodexCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CodexTabName);
}

FUnrealCodexModule& FUnrealCodexModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUnrealCodexModule>("UnrealCodex");
}

bool FUnrealCodexModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UnrealCodex");
}

void FUnrealCodexModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	
	// Add to the main menu bar under Tools
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->FindOrAddSection("UnrealCodex");

		Section.AddMenuEntryWithCommandList(
			FUnrealCodexCommands::Get().OpenCodexPanel,
			PluginCommands,
			LOCTEXT("OpenCodexMenuItem", "Codex Assistant"),
			LOCTEXT("OpenCodexMenuItemTooltip", "Open the Codex AI Assistant for UE5.5 help (Ctrl+Shift+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		);

		Section.AddMenuEntryWithCommandList(
			FUnrealCodexCommands::Get().QuickAsk,
			PluginCommands,
			LOCTEXT("QuickAskMenuItem", "Quick Ask Codex"),
			LOCTEXT("QuickAskMenuItemTooltip", "Quickly ask Codex a question (Ctrl+Alt+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		);
	}
	
	// Add to the toolbar
	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("UnrealCodex");
		
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			FUnrealCodexCommands::Get().OpenCodexPanel,
			LOCTEXT("CodexToolbarButton", "Codex"),
			LOCTEXT("CodexToolbarTooltip", "Open Codex Assistant (Ctrl+Shift+C)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
		));
	}
}

void FUnrealCodexModule::UnregisterMenus()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FUnrealCodexModule::StartMCPServer()
{
	if (MCPServer.IsValid())
	{
		UE_LOG(LogUnrealCodex, Warning, TEXT("MCP Server already exists"));
		return;
	}

	MCPServer = MakeShared<FUnrealCodexMCPServer>();

	if (!MCPServer->Start(GetMCPServerPort()))
	{
		UE_LOG(LogUnrealCodex, Error, TEXT("Failed to start MCP Server on port %d"), GetMCPServerPort());
		MCPServer.Reset();
	}
}

void FUnrealCodexModule::StopMCPServer()
{
	if (MCPServer.IsValid())
	{
		MCPServer->Stop();
		MCPServer.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUnrealCodexModule, UnrealCodex)
