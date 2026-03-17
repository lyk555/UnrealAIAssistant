// Copyright Natali Caggiano. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "IAssistantRunner.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboBox.h"
DECLARE_DELEGATE(FOnToolbarAction)
DECLARE_DELEGATE_OneParam(FOnCheckboxChanged, bool)
DECLARE_DELEGATE_OneParam(FOnProviderChanged, ECodexProvider)
/**
 * Toolbar widget for UnrealAIAssistant editor interactions.
 */
class SAssistantToolbar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssistantToolbar)
		: _bUE55ContextEnabled(true)
		, _bProjectContextEnabled(true)
		, _bRestoreEnabled(false)
		, _CurrentProvider(ECodexProvider::Codex)
	{}
		SLATE_ATTRIBUTE(bool, bUE55ContextEnabled)
		SLATE_ATTRIBUTE(bool, bProjectContextEnabled)
		SLATE_ATTRIBUTE(bool, bRestoreEnabled)
		SLATE_ATTRIBUTE(ECodexProvider, CurrentProvider)
		SLATE_EVENT(FOnProviderChanged, OnProviderChanged)
		SLATE_EVENT(FOnCheckboxChanged, OnUE55ContextChanged)
		SLATE_EVENT(FOnCheckboxChanged, OnProjectContextChanged)
		SLATE_EVENT(FOnToolbarAction, OnRefreshContext)
		SLATE_EVENT(FOnToolbarAction, OnRestoreSession)
		SLATE_EVENT(FOnToolbarAction, OnNewSession)
		SLATE_EVENT(FOnToolbarAction, OnClear)
		SLATE_EVENT(FOnToolbarAction, OnCopyLast)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
private:
	TSharedRef<SWidget> GenerateProviderWidget(TSharedPtr<ECodexProvider> InOption) const;
	void HandleProviderSelectionChanged(TSharedPtr<ECodexProvider> InOption, ESelectInfo::Type SelectInfo);
	TSharedPtr<ECodexProvider> FindProviderOption(ECodexProvider Provider) const;
	TAttribute<bool> bUE55ContextEnabled;
	TAttribute<bool> bProjectContextEnabled;
	TAttribute<bool> bRestoreEnabled;
	TAttribute<ECodexProvider> CurrentProvider;
	TArray<TSharedPtr<ECodexProvider>> ProviderOptions;
	FOnProviderChanged OnProviderChanged;
	FOnCheckboxChanged OnUE55ContextChanged;
	FOnCheckboxChanged OnProjectContextChanged;
	FOnToolbarAction OnRefreshContext;
	FOnToolbarAction OnRestoreSession;
	FOnToolbarAction OnNewSession;
	FOnToolbarAction OnClear;
	FOnToolbarAction OnCopyLast;
};
