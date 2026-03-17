// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UnrealAIAssistantConstants.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUnrealAIAssistant, Log, All);

class FUnrealAIAssistantMCPServer;

class FUnrealAIAssistantModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the singleton instance */
	static FUnrealAIAssistantModule& Get();

	/** Check if module is available */
	static bool IsAvailable();

	/** Get the MCP server instance */
	TSharedPtr<FUnrealAIAssistantMCPServer> GetMCPServer() const { return MCPServer; }

	/** Get MCP server port - uses centralized constant */
	static constexpr uint32 GetMCPServerPort() { return UnrealAIAssistantConstants::MCPServer::DefaultPort; }

private:
	void RegisterMenus();
	void UnregisterMenus();
	void StartMCPServer();
	void StopMCPServer();

	TSharedPtr<class FUICommandList> PluginCommands;
	TSharedPtr<class SDockTab> CodexTab;
	TSharedPtr<FUnrealAIAssistantMCPServer> MCPServer;
};
