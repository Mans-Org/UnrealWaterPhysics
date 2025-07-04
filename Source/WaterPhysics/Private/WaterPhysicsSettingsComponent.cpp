// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsSettingsComponent.h"
#include "WaterPhysicsCollisionComponent.h"
#include "Components/PrimitiveComponent.h"

UWaterPhysicsSettingsComponent::UWaterPhysicsSettingsComponent()
{
	BlacklistComponents.bSelectAll = false;
	WhitelistComponents.bSelectAll = true;
}

FGatherWaterPhysicsSettingsResult UWaterPhysicsSettingsComponent::GatherActorWaterPhysicsSettings(AActor* Actor)
{
	FGatherWaterPhysicsSettingsResult OutSettings;

	for (UActorComponent* ActorComponent: Actor->GetComponents())
	{
		OutSettings.SettingsComponent = Cast<UWaterPhysicsSettingsComponent>(ActorComponent);
		if (OutSettings.SettingsComponent != nullptr)
			break;
	}

	// Mirror of ShowComponentClasses in FComponentsWaterPhysicsSettings
	const TArray<UClass*> IncludeComponentClasses = { UPrimitiveComponent::StaticClass(), UWaterPhysicsCollisionComponent::StaticClass() };

	const TArray<UClass*> ExcludeComponentClasses = []()->TArray<UClass*>
	{
		// Mirror of HideComponentClasses in FComponentsWaterPhysicsSettings
		const static TArray<FString> ClassNames =
		{
			"ArrowComponent",
			"PaperTerrainComponent",
			"BillboardComponent",
			"DrawFrustumComponent",
			"LineBatchComponent",
			"SplineComponent",
			"TextRenderComponent",
			"VectorFieldComponent",
			"FXSystemComponent",
			"FieldSystemComponent"
		};

		TArray<UClass*> OutArray;
		for (const FString& ClassStr : ClassNames)
		{
			if (UClass* Filter = FindFirstObject<UClass>(*ClassStr))
				OutArray.AddUnique(Filter);
		}
		return OutArray;
	}();
	
	if (OutSettings.SettingsComponent == nullptr)
	{
		const auto ShouldIncludeComponent = [&](UActorComponent* ActorComponent)
		{
			if (IncludeComponentClasses.FindByPredicate([&](UClass* ClassFilter) { return ActorComponent->IsA(ClassFilter); }) != nullptr)
			{
				if (ExcludeComponentClasses.FindByPredicate([&](UClass* ClassFilter) { return ActorComponent->IsA(ClassFilter); }) == nullptr)
					return true;
			}

			return false;
		};

		for (UActorComponent* ActorComponent : Actor->GetComponents())
		{
			if (ShouldIncludeComponent(ActorComponent))
				OutSettings.ComponentsWaterPhysicsSettings.Add(Cast<USceneComponent>(ActorComponent), FWaterPhysicsSettings());
		}
	}
	else
	{
		OutSettings.BlacklistedComponents = OutSettings.SettingsComponent->BlacklistComponents.GetComponents(Actor, IncludeComponentClasses, ExcludeComponentClasses);
		OutSettings.WhitelistedComponents = OutSettings.SettingsComponent->WhitelistComponents.bSelectAll 
			? TArray<UActorComponent*>()
			: OutSettings.SettingsComponent->WhitelistComponents.GetComponents(Actor, IncludeComponentClasses, ExcludeComponentClasses);
		
		for (FComponentsWaterPhysicsSettings& It : OutSettings.SettingsComponent->WaterPhysicsSettings)
		{
			const TArray<UActorComponent*> SelectedComponents = It.ActorComponentsSelection.GetComponents(Actor, IncludeComponentClasses, ExcludeComponentClasses);
			for (UActorComponent* Component : SelectedComponents)
			{
				if (OutSettings.BlacklistedComponents.Contains(Component) 
					|| (OutSettings.WhitelistedComponents.Num() > 0 && !OutSettings.WhitelistedComponents.Contains(Component)))
					continue;

				if (FWaterPhysicsSettings* WaterPhysicsSettings = OutSettings.ComponentsWaterPhysicsSettings.Find(Component))
					*WaterPhysicsSettings = FWaterPhysicsSettings::MergeWaterPhysicsSettings(*WaterPhysicsSettings, It.WaterPhysicsSettings);
				else
					OutSettings.ComponentsWaterPhysicsSettings.Add(Component, It.WaterPhysicsSettings);
			}
		}
	}

	return OutSettings;
}

void UWaterPhysicsSettingsComponent::NotifyWaterPhysicsSettingsChanged()
{
	OnWaterPhysicsSettingsChanged.Broadcast(this);
}

#if WITH_EDITOR
void UWaterPhysicsSettingsComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.MemberProperty != nullptr 
		&& (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UWaterPhysicsSettingsComponent, WaterPhysicsSettings)
		||  PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UWaterPhysicsSettingsComponent, BlacklistComponents)
		||  PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UWaterPhysicsSettingsComponent, WhitelistComponents)))
	{
		NotifyWaterPhysicsSettingsChanged();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UWaterPhysicsSettingsComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.PropertyChain.GetHead()->GetValue();
	if (Property != nullptr 
		&& (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UWaterPhysicsSettingsComponent, WaterPhysicsSettings)
		||  Property->GetFName() == GET_MEMBER_NAME_CHECKED(UWaterPhysicsSettingsComponent, BlacklistComponents)
		||  Property->GetFName() == GET_MEMBER_NAME_CHECKED(UWaterPhysicsSettingsComponent, WhitelistComponents)))
	{
		NotifyWaterPhysicsSettingsChanged();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif