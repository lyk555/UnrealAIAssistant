// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UnrealCodexConstants.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUnrealCodex, Log, All);

class FUnrealCodexMCPServer;

class FUnrealCodexModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the singleton instance */
	static FUnrealCodexModule& Get();

	/** Check if module is available */
	static bool IsAvailable();

	/** Get the MCP server instance */
	TSharedPtr<FUnrealCodexMCPServer> GetMCPServer() const { return MCPServer; }

	/** Get MCP server port - uses centralized constant */
	static constexpr uint32 GetMCPServerPort() { return UnrealCodexConstants::MCPServer::DefaultPort; }

private:
	void RegisterMenus();
	void UnregisterMenus();
	void StartMCPServer();
	void StopMCPServer();

	TSharedPtr<class FUICommandList> PluginCommands;
	TSharedPtr<class SDockTab> CodexTab;
	TSharedPtr<FUnrealCodexMCPServer> MCPServer;
};
