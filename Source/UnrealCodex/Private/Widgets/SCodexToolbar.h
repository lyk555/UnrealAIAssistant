// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

DECLARE_DELEGATE(FOnToolbarAction)
DECLARE_DELEGATE_OneParam(FOnCheckboxChanged, bool)

/**
 * Toolbar widget for Codex Editor
 * Handles UE context toggles, session management buttons
 */
class SCodexToolbar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCodexToolbar)
		: _bUE55ContextEnabled(true)
		, _bProjectContextEnabled(true)
		, _bRestoreEnabled(false)
	{}
		SLATE_ATTRIBUTE(bool, bUE55ContextEnabled)
		SLATE_ATTRIBUTE(bool, bProjectContextEnabled)
		SLATE_ATTRIBUTE(bool, bRestoreEnabled)
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
	TAttribute<bool> bUE55ContextEnabled;
	TAttribute<bool> bProjectContextEnabled;
	TAttribute<bool> bRestoreEnabled;

	FOnCheckboxChanged OnUE55ContextChanged;
	FOnCheckboxChanged OnProjectContextChanged;
	FOnToolbarAction OnRefreshContext;
	FOnToolbarAction OnRestoreSession;
	FOnToolbarAction OnNewSession;
	FOnToolbarAction OnClear;
	FOnToolbarAction OnCopyLast;
};
