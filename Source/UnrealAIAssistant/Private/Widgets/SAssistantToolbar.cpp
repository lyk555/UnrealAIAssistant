// Copyright Natali Caggiano. All Rights Reserved.

#include "SAssistantToolbar.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAIAssistant"

void SAssistantToolbar::Construct(const FArguments& InArgs)
{
	bUE55ContextEnabled = InArgs._bUE55ContextEnabled;
	bProjectContextEnabled = InArgs._bProjectContextEnabled;
	bRestoreEnabled = InArgs._bRestoreEnabled;
	CurrentProvider = InArgs._CurrentProvider;
	OnProviderChanged = InArgs._OnProviderChanged;
	OnUE55ContextChanged = InArgs._OnUE55ContextChanged;
	OnProjectContextChanged = InArgs._OnProjectContextChanged;
	OnRefreshContext = InArgs._OnRefreshContext;
	OnRestoreSession = InArgs._OnRestoreSession;
	OnNewSession = InArgs._OnNewSession;
	OnClear = InArgs._OnClear;
	OnCopyLast = InArgs._OnCopyLast;

	ProviderOptions.Add(MakeShared<ECodexProvider>(ECodexProvider::Codex));
	ProviderOptions.Add(MakeShared<ECodexProvider>(ECodexProvider::Claude));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f, 4.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "assistant"))
				.TextStyle(FAppStyle::Get(), "LargeText")
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Provider", "backend"))
				.TextStyle(FAppStyle::Get(), "SmallText")
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboBox<TSharedPtr<ECodexProvider>>)
				.OptionsSource(&ProviderOptions)
				.InitiallySelectedItem(FindProviderOption(CurrentProvider.Get()))
				.OnGenerateWidget(this, &SAssistantToolbar::GenerateProviderWidget)
				.OnSelectionChanged(this, &SAssistantToolbar::HandleProviderSelectionChanged)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromString(GetCodexProviderDisplayName(CurrentProvider.Get()));
					})
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bUE55ContextEnabled.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { OnUE55ContextChanged.ExecuteIfBound(NewState == ECheckBoxState::Checked); })
				.ToolTipText(LOCTEXT("UE55ContextTip", "Include Unreal Engine 5.5 context in prompts"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UE55Context", "UE5.5 Context"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bProjectContextEnabled.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { OnProjectContextChanged.ExecuteIfBound(NewState == ECheckBoxState::Checked); })
				.ToolTipText(LOCTEXT("ProjectContextTip", "Include project source files and level actors in prompts"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ProjectContext", "Project Context"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshContext", "Refresh Context"))
				.OnClicked_Lambda([this]() { OnRefreshContext.ExecuteIfBound(); return FReply::Handled(); })
				.ToolTipText(LOCTEXT("RefreshContextTip", "Refresh project context (source files, classes, level actors)"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RestoreContext", "Restore Context"))
				.OnClicked_Lambda([this]() { OnRestoreSession.ExecuteIfBound(); return FReply::Handled(); })
				.ToolTipText(LOCTEXT("RestoreContextTip", "Restore previous session context from disk"))
				.IsEnabled_Lambda([this]() { return bRestoreEnabled.Get(); })
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("NewSession", "New Session"))
				.OnClicked_Lambda([this]() { OnNewSession.ExecuteIfBound(); return FReply::Handled(); })
				.ToolTipText(LOCTEXT("NewSessionTip", "Start a new session (clears history)"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Clear", "Clear"))
				.OnClicked_Lambda([this]() { OnClear.ExecuteIfBound(); return FReply::Handled(); })
				.ToolTipText(LOCTEXT("ClearTip", "Clear chat display"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Copy", "Copy Last"))
				.OnClicked_Lambda([this]() { OnCopyLast.ExecuteIfBound(); return FReply::Handled(); })
				.ToolTipText(LOCTEXT("CopyTip", "Copy last response to clipboard"))
			]
		]
	];
}

TSharedRef<SWidget> SAssistantToolbar::GenerateProviderWidget(TSharedPtr<ECodexProvider> InOption) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(InOption.IsValid() ? GetCodexProviderDisplayName(*InOption) : GetCodexProviderDisplayName(ECodexProvider::Codex)));
}

void SAssistantToolbar::HandleProviderSelectionChanged(TSharedPtr<ECodexProvider> InOption, ESelectInfo::Type SelectInfo)
{
	if (InOption.IsValid())
	{
		OnProviderChanged.ExecuteIfBound(*InOption);
	}
}

TSharedPtr<ECodexProvider> SAssistantToolbar::FindProviderOption(ECodexProvider Provider) const
{
	for (const TSharedPtr<ECodexProvider>& Option : ProviderOptions)
	{
		if (Option.IsValid() && *Option == Provider)
		{
			return Option;
		}
	}

	return ProviderOptions.Num() > 0 ? ProviderOptions[0] : nullptr;
}

#undef LOCTEXT_NAMESPACE
