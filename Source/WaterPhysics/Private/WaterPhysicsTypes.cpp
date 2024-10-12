// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsTypes.h"

FWaterPhysicsSettings FWaterPhysicsSettings::MergeWaterPhysicsSettings(const FWaterPhysicsSettings& DefaultSettings, const FWaterPhysicsSettings& OverrideSettings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MergeWaterPhysicsSettings)

	FWaterPhysicsSettings OutSettings;

	struct FPropertyMemberAddr
	{
		FBoolProperty* OverrideBoolProperty;
		FProperty* Property;
	};
	static TArray<FPropertyMemberAddr, TInlineAllocator<32>> OverrideAndPropertyMemberValueAddr;
	static bool bAreValueAddrInitialized = false;
	static FCriticalSection CriticalSection;

	if (!bAreValueAddrInitialized)
	{
		FScopeLock Lock(&CriticalSection);
		if (!bAreValueAddrInitialized)
		{
			// Save property pointer locations on first merge for major performance improvements
			if (OverrideAndPropertyMemberValueAddr.Num() == 0)
			{
				TMap<FName, FProperty*> OverrideProperties;
				OverrideProperties.Reserve(64);
				for (FProperty* Property = FWaterPhysicsSettings::StaticStruct()->PropertyLink; Property; Property = Property->PropertyLinkNext)
				{
					const FString PropertyName = Property->GetName();
					if (PropertyName.StartsWith("bOverride_"))
					{
						OverrideProperties.Add(*PropertyName, Property);
					}
					else if (FProperty** OverrideProperty = OverrideProperties.Find(*FString::Printf(TEXT("bOverride_%s"), *PropertyName)))
					{
						OverrideAndPropertyMemberValueAddr.Add(FPropertyMemberAddr { CastFieldChecked<FBoolProperty>(*OverrideProperty), Property });
					}
				}
			}

			bAreValueAddrInitialized = true;
		}
	}

	for (const FPropertyMemberAddr& It : OverrideAndPropertyMemberValueAddr)
	{
		if (It.OverrideBoolProperty->GetPropertyValue(It.OverrideBoolProperty->ContainerPtrToValuePtr<void>(&OverrideSettings))) // Is bOverride_ set in OverrideSettings
		{
			It.OverrideBoolProperty->SetPropertyValue(It.OverrideBoolProperty->ContainerPtrToValuePtr<void>((void*)&OutSettings), true);
			It.Property->CopyCompleteValue(
				It.Property->ContainerPtrToValuePtr<void>((void*)&OutSettings), 
				It.Property->ContainerPtrToValuePtr<void>(&OverrideSettings)
			);
		}
		else if (It.OverrideBoolProperty->GetPropertyValue(It.OverrideBoolProperty->ContainerPtrToValuePtr<void>(&DefaultSettings))) // Is bOverride_ set in DefaultSettings
		{
			It.OverrideBoolProperty->SetPropertyValue(It.OverrideBoolProperty->ContainerPtrToValuePtr<void>((void*)&OutSettings), true);
			It.Property->CopyCompleteValue(
				It.Property->ContainerPtrToValuePtr<void>((void*)&OutSettings), 
				It.Property->ContainerPtrToValuePtr<void>(&DefaultSettings)
			);
		}
		else
		{
			It.OverrideBoolProperty->ClearValue(It.OverrideBoolProperty->ContainerPtrToValuePtr<void>((void*)&OutSettings)); // Bitfields are initialized to 1, set them to 0 for merging
		}
	}

	return OutSettings;
}


TArray<UActorComponent*> FActorComponentsSelection::GetComponents(AActor* SearchActor, const TArray<UClass*>& IncludeComponentClasses, const TArray<UClass*>& ExcludeComponentClasses) const
{
	/* 
		TODO: Right now we pass IncludeComponentClasses and ExcludeComponentClasses as parameters to this function, however for the UI
		they are declared as meta properties, we should be able to move them into the struct and set them as part of the property default.

		This could also allow us to create custom Blueprint UI for setting Hidden/Shown components.
	*/
	TArray<UActorComponent*> OutComponents;
	if (!IsValid(SearchActor))
		return OutComponents;

	if (bSelectAll)
	{
		for (UActorComponent* Component : SearchActor->GetComponents())
		{
			if (!IsValid(Component))
				continue;

			if (IncludeComponentClasses.Num() == 0 || IncludeComponentClasses.FindByPredicate([&](UClass* ClassFilter) { return Component->IsA(ClassFilter); }) != nullptr)
			{
				if (ExcludeComponentClasses.FindByPredicate([&](UClass* ClassFilter) { return Component->IsA(ClassFilter); }) == nullptr)
					OutComponents.Add(Component);
			}
		}
	}
	else
	{
		for (const FName& ComponentName : ComponentNames)
		{
			if (FObjectPropertyBase* ObjProp = FindFProperty<FObjectPropertyBase>(SearchActor->GetClass(), ComponentName))
			{
				if (UActorComponent* Component = Cast<UActorComponent>(ObjProp->GetObjectPropertyValue_InContainer(SearchActor)))
				{
					if (IncludeComponentClasses.Num() == 0 || IncludeComponentClasses.FindByPredicate([&](UClass* ClassFilter) { return Component->IsA(ClassFilter); }) != nullptr)
					{
						if (ExcludeComponentClasses.FindByPredicate([&](UClass* ClassFilter) { return Component->IsA(ClassFilter); }) == nullptr)
							OutComponents.Add(Component);
					}
				}
			}
		}
	}

	return OutComponents;
}

bool FWaterPhysicsFilter::ProcessFilter(AActor* Actor) const
{
	check(IsValid(Actor));

	switch (FilterType)
	{
	case EWaterPhysicsFilterType::Tag:
		return Actor->ActorHasTag(Tag) ^ Not;
	case EWaterPhysicsFilterType::ActorClass:
		return Actor->IsA(ActorsClass.Get()) ^ Not;
	case EWaterPhysicsFilterType::ComponentClass:
		return (Actor->FindComponentByClass(ComponentClass) != nullptr) ^ Not;
	default:
		checkf(0, TEXT("Filter not implemented!"));
		break;
	}

	return false;
}

bool FWaterPhysicsFilter::ProcessFilterList(AActor* Actor, const TArray<FWaterPhysicsFilter>& FilterList)
{
	if (FilterList.Num() == 0)
		return false;

	// Get a list of all AND filters e.g. [A & B | C & D] => [[A, B], [C, D]]
	TArray<TArray<const FWaterPhysicsFilter*>> AndExpressionsList;
	{
		AndExpressionsList.AddDefaulted();
		for (const FWaterPhysicsFilter& Filter : FilterList)
		{
			if (Filter.FilterOperation == EWaterPhysicsFilterOperation::And 
				|| FilterList.GetData() == &Filter) // First element is always treated as an AND
				AndExpressionsList.Last().Add(&Filter);
			else
				AndExpressionsList.Add({ &Filter });
		}
	}

	const auto EvaluateAndExpressions = [&](const TArray<const FWaterPhysicsFilter*>& AndExpressions)
	{
		for (const FWaterPhysicsFilter* Filter : AndExpressions)
		{
			if (Filter->ProcessFilter(Actor) != true)
				return false;
		}
		return true;
	};

	for (const TArray<const FWaterPhysicsFilter*>& AndExpressions : AndExpressionsList)
	{
		if (EvaluateAndExpressions(AndExpressions) == true)
			return true; // We have found an AND expression chain which evaluated to true, this actor satisfies the filter
	}

	// No expression evaluated to true, this actor does not satisfy the filter
	return false;
}