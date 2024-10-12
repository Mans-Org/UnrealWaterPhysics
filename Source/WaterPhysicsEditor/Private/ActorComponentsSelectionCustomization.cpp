// Copyright Mans Isaksson. All Rights Reserved.

#include "ActorComponentsSelectionCustomization.h"
#include "WaterPhysicsCompatibilityLayer.h"
#include "WaterPhysicsTypes.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SToolTip.h"
#include "Styling/SlateIconFinder.h"
#include "SListViewSelectorDropdownMenu.h"
#include "EditorCategoryUtils.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ActorComponentsSelectionCustomization"

DECLARE_DELEGATE_OneParam(FActorComponentsSelectionChanged, FActorComponentsSelection);

typedef TSharedPtr<class FActorComponentSelectionComboEntry> FActorComponentSelectionComboEntryPtr;

class FActorComponentSelectionComboEntry: public TSharedFromThis<FActorComponentSelectionComboEntry>
{
public:
	enum EType
	{
		Heading,
		Separator,
		Component,
		SelectAll
	};

	UClass* ComponentClass;
	FName   ComponentName;
	FText   HeadingText;
	EType   Type;
	bool    bChecked;
	TSharedPtr<SCheckBox> CheckboxWidget;

	FActorComponentSelectionComboEntry(UClass* InComponentClass, const FName& InComponentName, const FText& InHeadingText, EType InType, bool bInChecked)
		: ComponentClass(InComponentClass)
		, ComponentName(InComponentName)
		, HeadingText(InHeadingText)
		, Type(InType)
		, bChecked(bInChecked)
	{
	}

	static TSharedPtr<FActorComponentSelectionComboEntry> MakeHeading(const FText& InHeadingText)
	{
		return MakeShareable(new FActorComponentSelectionComboEntry(nullptr, NAME_None, InHeadingText, EType::Heading, false));
	}

	static TSharedPtr<FActorComponentSelectionComboEntry> MakeSeperator()
	{
		return MakeShareable(new FActorComponentSelectionComboEntry(nullptr, NAME_None, FText(), EType::Separator, false));
	}

	static TSharedPtr<FActorComponentSelectionComboEntry> MakeComponentEntry(const FName& InComponentName, UClass* InCoponentClass, bool InChecked)
	{
		return MakeShareable(new FActorComponentSelectionComboEntry(InCoponentClass, InComponentName, FText::FromName(InComponentName), EType::Component, InChecked));
	}

	static TSharedPtr<FActorComponentSelectionComboEntry> MakeSelectAllEntry(bool bChecked)
	{
		return MakeShareable(new FActorComponentSelectionComboEntry(nullptr, NAME_None, LOCTEXT("SelectAllHeading", "Select All"), EType::SelectAll, bChecked));
	}

	bool ToggleEntryCheckedState()
	{
		bChecked = !bChecked;
		if (CheckboxWidget.IsValid() && CheckboxWidget->IsChecked() != bChecked)
			CheckboxWidget->ToggleCheckedState();

		return bChecked;
	}

	void SetEntryCheckedState(bool bNewIsChecked)
	{
		if (CheckboxWidget.IsValid() && CheckboxWidget->IsChecked() != bNewIsChecked)
			CheckboxWidget->SetIsChecked(bNewIsChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);

		bChecked = bNewIsChecked;
	}

	FORCEINLINE bool IsHeading() const { return Type == EType::Heading; }

	FORCEINLINE bool IsSeparator() const { return Type == EType::Separator; }

	FORCEINLINE bool IsComponent() const { return Type == EType::Component; }

	FORCEINLINE bool IsSelectAll() const { return Type == EType::SelectAll; }
};

class SActorComponentSelectionCombo : public SComboButton
{
private:
	TArray<FActorComponentSelectionComboEntryPtr> EntryList;
	TArray<FActorComponentSelectionComboEntryPtr> FilteredEntryList;
	FActorComponentSelectionComboEntryPtr         SelectAllEntry;

	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<FActorComponentSelectionComboEntryPtr>> ComponentListView;

	TWeakObjectPtr<const AActor> Actor;
	TArray<UClass*> ShowClassFilter;
	TArray<UClass*> HideClassFilter;

	FActorComponentsSelection ComponentsSelection;
	FText CurrentSearchString;

	FActorComponentsSelectionChanged OnComponentSelectionChanged;
	FSimpleDelegate OnComboBoxClosed;

public:
	SLATE_BEGIN_ARGS(SActorComponentSelectionCombo)
	{}
		SLATE_ARGUMENT(const AActor*, Actor)
		SLATE_ARGUMENT(TArray<UClass*>, ShowClassFilter)
		SLATE_ARGUMENT(TArray<UClass*>, HideClassFilter)
		SLATE_ARGUMENT(FActorComponentsSelection, InitialComponentsSelection)
		SLATE_EVENT(FActorComponentsSelectionChanged, OnComponentSelectionChanged)
		SLATE_EVENT(FSimpleDelegate, OnComboBoxClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnComponentSelectionChanged  = InArgs._OnComponentSelectionChanged;
		Actor                        = InArgs._Actor;
		ShowClassFilter              = InArgs._ShowClassFilter;
		HideClassFilter              = InArgs._HideClassFilter;
		ComponentsSelection          = InArgs._InitialComponentsSelection;
		OnComboBoxClosed             = InArgs._OnComboBoxClosed;

		if (ShowClassFilter.Num() == 0)
			ShowClassFilter.Add(UActorComponent::StaticClass());

		InitComponentList();

		SAssignNew(ComponentListView, SListView<FActorComponentSelectionComboEntryPtr>)
			.ListItemsSource(&FilteredEntryList)
			.OnSelectionChanged(this, &SActorComponentSelectionCombo::OnListViewSelectionChanged)
			.OnGenerateRow(this, &SActorComponentSelectionCombo::GenerateActorComponentRow)
			.SelectionMode(ESelectionMode::Single);

		SAssignNew(SearchBox, SSearchBox)
			.HintText(LOCTEXT("ActorComponentSelectionSearchBoxHint", "Search Components"))
			.OnTextChanged(this, &SActorComponentSelectionCombo::OnSearchBoxTextChanged)
			.OnTextCommitted(this, &SActorComponentSelectionCombo::OnSearchBoxTextCommitted);

		SComboButton::FArguments Args;
		Args.ButtonContent()
		[
			SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SSpacer)
					.Size(FVector2D(8.0f,1.0f))
				]
				+SHorizontalBox::Slot()
				.Padding(1.0f)
				.AutoWidth()
				[
					SNew(SImage)
					.Image(this, &SActorComponentSelectionCombo::GetButtonIconBrush)
					.Visibility(this, &SActorComponentSelectionCombo::GetButtonIconVisibility)
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SSpacer)
					.Size(FVector2D(3.0f,1.0f))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SActorComponentSelectionCombo::GetButtonText)
				]
		]
		.MenuContent()
		[
			SNew(SListViewSelectorDropdownMenu<FActorComponentSelectionComboEntryPtr>, SearchBox, ComponentListView)
			[
				SNew(SBorder)
				.BorderImage(WaterPhysicsCompat::EditorStyle::GetBrush("Menu.Background"))
				.Padding(2)
				[
					SNew(SBox)
					.WidthOverride(250)
					[
						SNew(SVerticalBox)
						+SVerticalBox::Slot()
						.Padding(1.f)
						.AutoHeight()
						[
							SearchBox.ToSharedRef()
						]
						+SVerticalBox::Slot()
						.MaxHeight(400)
						[
							ComponentListView.ToSharedRef()
						]
					]
				]
			]
		]
		.IsFocusable(true)
		.ContentPadding(FMargin(5, 0))
		.ButtonStyle(WaterPhysicsCompat::EditorStyle::Get(), "PropertyEditor.AssetComboStyle")
		.ForegroundColor(WaterPhysicsCompat::EditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
		.OnComboBoxOpened(this, &SActorComponentSelectionCombo::ClearSelection)
		.OnMenuOpenChanged(this, &SActorComponentSelectionCombo::OnMenuOpenChanged);

		SComboButton::Construct(Args);

		ComponentListView->EnableToolTipForceField(true);
		
		// The base class can automatically handle setting focus to a specified control when the combo button is opened
		SetMenuContentWidgetToFocus(SearchBox);
	}

	void OnMenuOpenChanged(bool bMenuOpen)
	{
		if (!bMenuOpen)
			OnComboBoxClosed.ExecuteIfBound();
	}

	void ClearSelection()
	{
		SearchBox->SetText(FText::GetEmpty());

		// Clear the selection in such a way as to also clear the keyboard selector
		ComponentListView->SetSelection(nullptr, ESelectInfo::OnNavigation);

		// Make sure we scroll to the top
		if (EntryList.Num() > 0)
			ComponentListView->RequestScrollIntoView(EntryList[0]);
	}

	void GenerateFilteredComponentList(const FString& InSearchText)
	{
		if (InSearchText.IsEmpty())
		{
			FilteredEntryList = EntryList;
		}
		else
		{
			FilteredEntryList.Empty();

			int32 LastHeadingIndex = INDEX_NONE;
			FActorComponentSelectionComboEntryPtr* LastHeadingPtr = nullptr;

			for (int32 ComponentIndex = 0; ComponentIndex < EntryList.Num(); ComponentIndex++)
			{
				FActorComponentSelectionComboEntryPtr& CurrentEntry = EntryList[ComponentIndex];

				if (CurrentEntry->IsHeading())
				{
					LastHeadingIndex = FilteredEntryList.Num();
					LastHeadingPtr   = &CurrentEntry;
				}
				else if (CurrentEntry->IsComponent())
				{
					const FString FriendlyComponentName = CurrentEntry->ComponentName.ToString();
					if (FriendlyComponentName.Contains(InSearchText, ESearchCase::IgnoreCase))
					{
						// Add the heading first if it hasn't already been added
						if (LastHeadingIndex != INDEX_NONE)
						{
							FilteredEntryList.Insert(*LastHeadingPtr, LastHeadingIndex);
							LastHeadingIndex = INDEX_NONE;
							LastHeadingPtr = nullptr;
						}

						// Add the class
						FilteredEntryList.Add(CurrentEntry);
					}
				}
			}

			// Select the first non-category item that passed the filter
			for (FActorComponentSelectionComboEntryPtr& TestEntry : FilteredEntryList)
			{
				if (TestEntry->IsComponent())
				{
					ComponentListView->SetSelection(TestEntry, ESelectInfo::OnNavigation);
					break;
				}
			}
		}
	}

	void OnSearchBoxTextChanged(const FText& InSearchText)
	{
		CurrentSearchString = InSearchText;
		GenerateFilteredComponentList(CurrentSearchString.ToString());
		ComponentListView->RequestListRefresh();
	}

	void OnSearchBoxTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo)
	{
		if (CommitInfo == ETextCommit::OnEnter)
		{
			const auto SelectedItems = ComponentListView->GetSelectedItems();
			if (SelectedItems.Num() > 0)
			{
				ComponentListView->SetSelection(SelectedItems[0]);
			}
		}
	}

	void OnListViewSelectionChanged(FActorComponentSelectionComboEntryPtr InItem, ESelectInfo::Type SelectInfo)
	{
		if (!InItem.IsValid() || SelectInfo == ESelectInfo::OnNavigation)
			return;

		if (InItem->IsComponent() || InItem->IsSelectAll())
			InItem->ToggleEntryCheckedState();
	}

	TSharedRef<ITableRow> GenerateActorComponentRow(FActorComponentSelectionComboEntryPtr Entry, const TSharedRef<STableViewBase>& OwnerTable)
	{
		if (Entry->IsHeading())
		{
			return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
				.Style(&WaterPhysicsCompat::EditorStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.NoHoverTableRow"))
				.ShowSelection(false)
				[
					SNew(SBox)
					.Padding(1.f)
					[
						SNew(STextBlock)
						.Text(Entry->HeadingText)
						.TextStyle(WaterPhysicsCompat::EditorStyle::Get(), TEXT("Menu.Heading"))
					]
				];
		}
		else if (Entry->IsSeparator())
		{
			return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
				.Style(&WaterPhysicsCompat::EditorStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.NoHoverTableRow"))
				.ShowSelection(false)
				[
					SNew(SBox)
					.Padding(1.f)
					[
						SNew(SBorder)
						.Padding(WaterPhysicsCompat::EditorStyle::GetMargin(TEXT("Menu.Separator.Padding")))
						.BorderImage(WaterPhysicsCompat::EditorStyle::GetBrush(TEXT("Menu.Separator")))
					]
				];
		}
		else
		{
			return SNew(SComboRow<TSharedPtr<FString>>, OwnerTable)
				.ToolTip(SNew(SToolTip).Text(Entry->ComponentClass
					? FText::Format(FTextFormat::FromString("{0} {1}"), LOCTEXT("SelectActorComponentsToolTip", "Select Component"), FText::FromName(Entry->ComponentName))
					: LOCTEXT("ClearActorComponentSelection", "Clear current selection")))
				[
					SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SAssignNew(Entry->CheckboxWidget, SCheckBox)
							.OnCheckStateChanged(this, &SActorComponentSelectionCombo::SetEntryCheckedState, Entry)
							.IsChecked(Entry->bChecked)
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SSpacer)
							.Size(FVector2D(8.0f,1.0f))
						]
						+SHorizontalBox::Slot()
						.Padding(1.0f)
						.AutoWidth()
						[
							SNew(SImage)
							.Image(FSlateIconFinder::FindIconBrushForClass(Entry->ComponentClass))
							.Visibility(Entry->ComponentClass ? EVisibility::Visible : EVisibility::Collapsed)
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SSpacer)
							.Size(FVector2D(3.0f,1.0f))
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.HighlightText(this, &SActorComponentSelectionCombo::GetCurrentSearchString)
							.Text_Lambda([Entry]() { return Entry->HeadingText; })
						]
				];
		}
	}

	const FSlateBrush* GetButtonIconBrush() const
	{
		if (!ComponentsSelection.bSelectAll && ComponentsSelection.ComponentNames.Num() == 1 && ComponentsSelection.ComponentNames[0] != NAME_None)
		{
			for (const auto& Entry : EntryList)
			{
				if (Entry->ComponentName == ComponentsSelection.ComponentNames[0])
					return FSlateIconFinder::FindIconBrushForClass(Entry->ComponentClass);
			}
		}

		return nullptr;
	}

	EVisibility GetButtonIconVisibility() const
	{
		return !ComponentsSelection.bSelectAll && ComponentsSelection.ComponentNames.Num() == 1 && ComponentsSelection.ComponentNames[0] != NAME_None
			? EVisibility::Visible : EVisibility::Collapsed;
	}

	FText GetButtonText() const
	{
		if (!ComponentsSelection.bSelectAll && ComponentsSelection.ComponentNames.Num() == 1 && ComponentsSelection.ComponentNames[0] != NAME_None)
		{
			for (const auto& Entry : EntryList)
			{
				if (Entry->ComponentName == ComponentsSelection.ComponentNames[0])
					return Entry->HeadingText;
			}
		}
		else if (ComponentsSelection.bSelectAll)
		{
			return LOCTEXT("AllComponentsSelected", "All Selected");
		}
		else if (ComponentsSelection.ComponentNames.Num() > 0)
		{
			return LOCTEXT("AllComponentsSelected", "Multiple Selected");
		}

		return LOCTEXT("NoComponentsSelected", "None");
	}

	void SetEntryCheckedState(ECheckBoxState CheckBoxState, FActorComponentSelectionComboEntryPtr Entry)
	{
		Entry->SetEntryCheckedState(CheckBoxState == ECheckBoxState::Checked ? true : false);

		if (Entry->IsSelectAll())
		{
			for (const auto& It : EntryList)
				It->SetEntryCheckedState(Entry->bChecked);

			ComponentsSelection.ComponentNames.Empty();
			ComponentsSelection.bSelectAll = Entry->bChecked;

			OnComponentSelectionChanged.ExecuteIfBound(ComponentsSelection);
		}
		else if (Entry->IsComponent())
		{
			SelectAllEntry->SetEntryCheckedState(false);
			ComponentsSelection.bSelectAll = false;

			ComponentsSelection.ComponentNames.Empty(ComponentsSelection.ComponentNames.Num());
			for (const auto& It : EntryList)
			{
				if (It->bChecked && It->ComponentName != NAME_None && !It->ComponentName.ToString().IsEmpty())
					ComponentsSelection.ComponentNames.Add(It->ComponentName);
			}

			OnComponentSelectionChanged.ExecuteIfBound(ComponentsSelection);
		}

		ComponentListView->ClearSelection();
	}

	void InitComponentList()
	{
		EntryList.Empty();
		EntryList.Add(FActorComponentSelectionComboEntry::MakeHeading(LOCTEXT("ActorComponentsSelectionHeading", "Select Component")));
		SelectAllEntry = EntryList[EntryList.Add(FActorComponentSelectionComboEntry::MakeSelectAllEntry(ComponentsSelection.bSelectAll))];
		EntryList.Add(FActorComponentSelectionComboEntry::MakeSeperator());

		if (Actor.IsValid())
		{
			for (TFieldIterator<FObjectPropertyBase> It(Actor->GetClass()); It; ++It)
			{
				if (UObject* Object = It->PropertyClass->GetDefaultObject())
				{
					if (ShowClassFilter.FindByPredicate([&](UClass* ClassFilter) { return Object->IsA(ClassFilter); }) != nullptr)
					{
						if (HideClassFilter.FindByPredicate([&](UClass* ClassFilter) { return Object->IsA(ClassFilter); }) == nullptr)
						{
							EntryList.Add(FActorComponentSelectionComboEntry::MakeComponentEntry(
								It->GetFName(),
								Object->GetClass(),
								ComponentsSelection.bSelectAll || ComponentsSelection.ComponentNames.Contains(It->GetFName())
							));
						}
					}
				}
			}
		}

		GenerateFilteredComponentList(CurrentSearchString.ToString());
	}

	FText GetCurrentSearchString() const { return CurrentSearchString; }

	void SetComponentsSelection(FActorComponentsSelection InComponentsSelection)
	{
		ComponentsSelection = InComponentsSelection;
		InitComponentList();
	}
};

void FActorComponentsSelectionCustomization::CustomizeStructHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IStructCustomizationUtils& StructCustomizationUtils)
{
	const auto GetOwningActor = [&](UObject* Object)->AActor*
	{
		if (!IsValid(Object))
			return nullptr;

		// The property lies directly on the actor
		if (AActor* Actor = Cast<AActor>(Object))
			return Actor;

		const auto GetOwningActorComponent = [](UObject* Object)->UActorComponent*
		{
			if (!IsValid(Object))
				return nullptr;
			if (UActorComponent* ActorComponent = Cast<UActorComponent>(Object))
				return ActorComponent;
			if (UActorComponent* ActorComponent = Cast<UActorComponent>(Object->GetOuter()))
				return ActorComponent;
			return nullptr;
		};

		if (UActorComponent* ActorComponent = GetOwningActorComponent(Object))
		{
			// If we're in the scene and have an actor as outer
			if (AActor* Actor = Cast<AActor>(ActorComponent->GetOuter()))
				return Actor;

			// If we're in the blueprint editor and our outer is the actor class
			if (UClass* BlueprintClass = Cast<UClass>(ActorComponent->GetOuter()))
				if (AActor* Actor = BlueprintClass->GetDefaultObject<AActor>())
					return Actor;
		}

		// If our direct outer is an actor, for example a UObject owned by the actor, we check actor component first since it has custom states in blueprint editor
		if (AActor* Actor = Cast<AActor>(Object->GetOuter()))
			return Actor;

		return nullptr;
	};

	// Find the owning actor of this property
	const AActor* OuterActor = [&]()->AActor* 
	{
		TArray<UObject*> Outers;
		StructPropertyHandle->GetOuterObjects(Outers);

		for (UObject* Outer : Outers)
		{
			if (AActor* OwningActor = GetOwningActor(Outer))
				return OwningActor;
		}

		return nullptr;
	}();
	
	const auto ParseClassStrArray = [StructPropertyHandle](const FName& InMetaDataKey)
	{
		TArray<FString> ClassStrArray;
		if (StructPropertyHandle->HasMetaData(InMetaDataKey))
			StructPropertyHandle->GetMetaData(InMetaDataKey).ParseIntoArray(ClassStrArray, TEXT(","), /*InCullEmpty =*/true);

		TArray<UClass*> OutArray;
		for (const FString& ClassStr : ClassStrArray)
		{
			if (UClass* Filter = FindFirstObject<UClass>(*ClassStr))
				OutArray.AddUnique(Filter);
		}
		return OutArray;
	};

	const TArray<UClass*> HideClasses = ParseClassStrArray("HideComponentClasses");
	const TArray<UClass*> ShowClasses = ParseClassStrArray("ShowComponentClasses");

	StructPropertyHandle->SetOnPropertyResetToDefault(FSimpleDelegate::CreateLambda([this, StructPropertyHandle]()
	{
		FActorComponentsSelection* ActorComponentsSelectionValue = nullptr;
		if (StructPropertyHandle->GetValueData((void*&)ActorComponentsSelectionValue) == FPropertyAccess::Success)
		{
			ActorComponentSelectionCombo->SetComponentsSelection(*ActorComponentsSelectionValue);
		}
	}));

	FActorComponentsSelection* InitialActorComponentsSelection = nullptr;
	StructPropertyHandle->GetValueData((void*&)InitialActorComponentsSelection);

	HeaderRow.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.HAlign(EHorizontalAlignment::HAlign_Fill)
	[
		SAssignNew(ActorComponentSelectionCombo, SActorComponentSelectionCombo)
		.Actor(OuterActor)
		.ShowClassFilter(ShowClasses)
		.HideClassFilter(HideClasses)
		.InitialComponentsSelection(InitialActorComponentsSelection ? *InitialActorComponentsSelection : FActorComponentsSelection())
		.OnComponentSelectionChanged_Lambda([StructPropertyHandle](FActorComponentsSelection NewActorComponentsSelection)
		{
			FActorComponentsSelection* ActorComponentsSelectionValue = nullptr;
			if (StructPropertyHandle->GetValueData((void*&)ActorComponentsSelectionValue) == FPropertyAccess::Success)
			{
				StructPropertyHandle->NotifyPreChange();
				*ActorComponentsSelectionValue = NewActorComponentsSelection;
				StructPropertyHandle->NotifyPostChange(EPropertyChangeType::Interactive);
			}
		})
		.OnComboBoxClosed_Lambda([StructPropertyHandle]()
		{
			StructPropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		})
	];
}

#undef LOCTEXT_NAMESPACE