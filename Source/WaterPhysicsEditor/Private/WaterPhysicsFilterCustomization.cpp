// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsFilterCustomization.h"
#include "WaterPhysicsTypes.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "Widgets/SNullWidget.h"
#include "DetailWidgetRow.h"
#include "EditorCategoryUtils.h"

#define LOCTEXT_NAMESPACE "WaterPhysicsFilterCustomization"

void FWaterPhysicsFilterCustomization::CustomizeStructHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IStructCustomizationUtils& StructCustomizationUtils)
{
	TSharedPtr<IPropertyHandle> NotPropertyHandle     = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWaterPhysicsFilter, Not));
	TSharedPtr<IPropertyHandle> FilterOperationHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWaterPhysicsFilter, FilterOperation));
	TSharedPtr<IPropertyHandle> FilterTypeHandle      = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWaterPhysicsFilter, FilterType));
	
	TSharedPtr<IPropertyHandle> TagHandle             = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWaterPhysicsFilter, Tag));
	TSharedPtr<IPropertyHandle> ActorsClassHandle     = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWaterPhysicsFilter, ActorsClass));
	TSharedPtr<IPropertyHandle> ComponentClassHandle  = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWaterPhysicsFilter, ComponentClass));

	const auto CreateContentWidget = [FilterTypeHandle, TagHandle, ActorsClassHandle, ComponentClassHandle]()->TSharedRef<SWidget>
	{
		EWaterPhysicsFilterType* ValuePtr = nullptr;
		FilterTypeHandle->GetValueData((void*&)ValuePtr);

		switch (ValuePtr ? *ValuePtr : EWaterPhysicsFilterType::Tag)
		{
		case EWaterPhysicsFilterType::Tag:
			return TagHandle->CreatePropertyValueWidget();
		case EWaterPhysicsFilterType::ActorClass:
			return ActorsClassHandle->CreatePropertyValueWidget();
		case EWaterPhysicsFilterType::ComponentClass:
			return ComponentClassHandle->CreatePropertyValueWidget();
		default:
			checkf(0, TEXT("Water Physics Filter Type not implemented"));
			break;
		}

		return SNew(SBox);
	};

	SVerticalBox::FSlot* ContentSlot = nullptr;
	
	const auto CreateFilterOperationWidget = [StructPropertyHandle, FilterOperationHandle]()->TSharedRef<SWidget>
	{
		TSharedRef<SWidget> FilterOperationWidget = FilterOperationHandle->CreatePropertyValueWidget();
		FilterOperationWidget->SetVisibility(StructPropertyHandle->GetIndexInArray() != 0 ? EVisibility::Visible : EVisibility::Collapsed);
		return FilterOperationWidget;
	};

	HeaderRow.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MaxDesiredWidth(350.f)
	.MinDesiredWidth(100.f)
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.f, 0.f, 3.f, 0.f))
			[
				CreateFilterOperationWidget()
			]
			+SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(EVerticalAlignment::VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 3.f, 0.f))
			[
				SNew(STextBlock)
				.Text_Lambda([NotPropertyHandle]()->FText
				{
					bool bVal = false;
					NotPropertyHandle->GetValue(bVal); 
					return bVal ? LOCTEXT("WaterPhysicsFilter_DoesNot", "Does Not Have") : LOCTEXT("WaterPhysicsFilter_Does", "Does Have");
				})
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.f, 0.f, 3.f, 0.f))
			[
				FilterTypeHandle->CreatePropertyValueWidget()
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				NotPropertyHandle->CreatePropertyValueWidget()
			]
		]
		+SVerticalBox::Slot()
		.Expose(ContentSlot)
		[ 
			CreateContentWidget()
		]
		.AutoHeight()
	];

	const auto UpdateContentWidget = FSimpleDelegate::CreateLambda([ContentSlot, CreateContentWidget]()
	{
		ContentSlot->AttachWidget(CreateContentWidget());
	});

	FilterTypeHandle->SetOnPropertyValueChanged(UpdateContentWidget);
	StructPropertyHandle->SetOnPropertyResetToDefault(UpdateContentWidget);
}

#undef LOCTEXT_NAMESPACE