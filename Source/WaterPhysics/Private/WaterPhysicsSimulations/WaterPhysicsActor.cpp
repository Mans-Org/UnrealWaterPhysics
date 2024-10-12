// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsSimulations/WaterPhysicsActor.h"
#include "WaterPhysicsModule.h"
#include "WaterPhysicsSettingsComponent.h"
#include "WaterPhysicsSceneComponent.h"
#include "WaterPhysicsCollisionInterface.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/ConstructorHelpers.h"

AWaterPhysicsActor::AWaterPhysicsActor()
{
	PrimaryActorTick.bCanEverTick = true;

	WaterPhysicsSceneComponent = CreateDefaultSubobject<UWaterPhysicsSceneComponent>(TEXT("WaterPhysicsSceneComponent"));
	WaterPhysicsSceneComponent->SetWaterInfoGetter(FGetWaterInfoAtLocation::CreateUObject(this, &AWaterPhysicsActor::CalculateWaterInfo), false);
	WaterPhysicsSceneComponent->PreStepWaterPhysicsScene.AddUObject(this, &AWaterPhysicsActor::PreWaterPhysicsSceneTick);
}

void AWaterPhysicsActor::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TickWaterPhysics);

#if WITH_EDITORONLY_DATA
	// HACK: For some reasone Epic finds it a good idea to re-create all the components when a property on the actor gets changed.
	// For it to not completley break our water physics simulations we "probe" one of the components on each actors to see if it has
	// been destroyed and we then re-add it to the water physics. This is only a problem in the editor which is why we accept a certain
	// amount of false positives, for example when a single component gets destroyed we might end up re-adding the actor unnecessarily.
	for (auto It = EditorComponentValidationTable.CreateIterator(); It; ++It)
	{
		if (IsValid(It->Key) && !IsValid(It->Value))
		{
			OnActorComponentsRecreated(It->Key);
			It.RemoveCurrent();
		}
		else if (!IsValid(It->Key))
		{
			It.RemoveCurrent();
		}
	}
#endif
	
	for (auto It = ActorsToRemove.CreateIterator(); It; ++It)
	{
		AActor* Actor = It->ActorToRemove.Get();
		if (!Actor)
		{
			It.RemoveCurrent();
			continue;
		}

		if ((It->Time -= DeltaTime) <= 0.f)
		{
			RemoveActorFromWater(Actor, -1.f);
			It.RemoveCurrent();
		}
	}
}

void AWaterPhysicsActor::AddActorToWater(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		UE_LOG(LogWaterPhysics, Warning, TEXT("Add Actor To Water - Recieved invalid actor"));
		return;
	}

	if (FilterActorFromWaterPhysics(Actor))
		return;

	if (ActorsToRemove.Contains({ Actor, 0.f }))
	{
		ActorsToRemove.Remove({ Actor, 0.f });
		return; // Right now we don't re-add the components if the actor "overlaps" again.
	}

#if WITH_EDITORONLY_DATA
	if (Actor->GetComponents().Num() > 0) // See comment in AWaterPhysics_WaterVolume::Tick for more info on this
		EditorComponentValidationTable.Add(Actor, Actor->GetComponents().Array()[0]);
#endif

	const FGatherWaterPhysicsSettingsResult WaterPhysicsSettingsResult = UWaterPhysicsSettingsComponent::GatherActorWaterPhysicsSettings(Actor);

	if (WaterPhysicsSettingsResult.SettingsComponent)
		WaterPhysicsSettingsResult.SettingsComponent->GetOnWaterPhysicsSettingsChanged().AddUObject(this, &AWaterPhysicsActor::NotifyWaterPhysicsSettingsChanged);

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (!ShouldComponentSimulateWaterPhysics(Component))
			continue;

		if (WaterPhysicsSettingsResult.BlacklistedComponents.Contains(Component) 
			|| (WaterPhysicsSettingsResult.WhitelistedComponents.Num() > 0 && !WaterPhysicsSettingsResult.WhitelistedComponents.Contains(Component)))
			continue;

		FWaterPhysicsSettings ComponentWaterPhysicsSettings;

		if (const FWaterPhysicsSettings* WaterPhysicsSettings = WaterPhysicsSettingsResult.ComponentsWaterPhysicsSettings.Find(Component))
			ComponentWaterPhysicsSettings = *WaterPhysicsSettings;

		WaterPhysicsSceneComponent->AddComponentToWaterPhysics(Component, ComponentWaterPhysicsSettings);
	}

	OnActorAddedToWater(Actor);

	if (WaterPhysicsSettingsResult.SettingsComponent)
		WaterPhysicsSettingsResult.SettingsComponent->OnActorAddedToWaterPhysics.Broadcast(WaterPhysicsSceneComponent);
}

void AWaterPhysicsActor::RemoveActorFromWater(AActor* Actor, float RemoveDelay)
{
	if (!IsValid(Actor))
	{
		UE_LOG(LogWaterPhysics, Warning, TEXT("Remove Actor From Water - Recieved invalid actor"));
		return;
	}

	if (RemoveDelay < 0.f)
	{
		bool bRemoved = false;
		for (UActorComponent* Component : Actor->GetComponents())
			bRemoved |= WaterPhysicsSceneComponent->RemoveComponentFromWaterPhysics(Component);

		UWaterPhysicsSettingsComponent* WaterPhysicsSettingsComponent = static_cast<UWaterPhysicsSettingsComponent*>(Actor->GetComponentByClass(UWaterPhysicsSettingsComponent::StaticClass()));
		if (WaterPhysicsSettingsComponent)
		{
			WaterPhysicsSettingsComponent->GetOnWaterPhysicsSettingsChanged().RemoveAll(this);
		}

#if WITH_EDITORONLY_DATA
		bRemoved |= EditorComponentValidationTable.Remove(Actor) != 0;
#endif

		if (bRemoved)
		{
			OnActorRemovedFromWater(Actor);
			if (WaterPhysicsSettingsComponent)
				WaterPhysicsSettingsComponent->OnActorRemovedFromWaterPhysics.Broadcast(WaterPhysicsSceneComponent);
		}
	}
	else
	{
		ActorsToRemove.Add({ Actor, RemoveDelay });
	}
}

void AWaterPhysicsActor::NotifyWaterPhysicsSettingsChanged(UWaterPhysicsSettingsComponent* WaterPhysicsSettingsComponent)
{
	AActor* DirtyActor = WaterPhysicsSettingsComponent->GetOwner();

	check(DirtyActor);

	const auto& NewWaterPhysicsSettings = UWaterPhysicsSettingsComponent::GatherActorWaterPhysicsSettings(DirtyActor);

	for (UActorComponent* Component : DirtyActor->GetComponents())
	{
		FWaterPhysicsSettings ComponentWaterPhysicsSettings = FWaterPhysicsSettings();

		if (const FWaterPhysicsSettings* WaterPhysicsSettings = NewWaterPhysicsSettings.ComponentsWaterPhysicsSettings.Find(Component))
			ComponentWaterPhysicsSettings = *WaterPhysicsSettings;

		if (WaterPhysicsSceneComponent->ContainsComponent(Component))
		{
			if (NewWaterPhysicsSettings.BlacklistedComponents.Contains(Component) 
				|| (NewWaterPhysicsSettings.WhitelistedComponents.Num() > 0 && !NewWaterPhysicsSettings.WhitelistedComponents.Contains(Component)))
			{
				// Remove newly blacklisted components
				WaterPhysicsSceneComponent->RemoveComponentFromWaterPhysics(Component, true);
			}
			else // Update water physics settings on components in water physics scene
			{
				WaterPhysicsSceneComponent->SetComponentWaterPhysicsSettings(Component, ComponentWaterPhysicsSettings);
			}
		}
	}
}

FGetWaterInfoResult AWaterPhysicsActor::CalculateWaterInfo(const UActorComponent* Component, const FVector& Location)
{
	return ReceiveCalculateWaterInfo(Component, Location);
}

void AWaterPhysicsActor::OnActorAddedToWater(AActor* Actor)
{
	ReceiveOnActorAddedToWater(Actor);
}

void AWaterPhysicsActor::OnActorRemovedFromWater(AActor* Actor)
{
	ReceiveOnActorRemovedFromWater(Actor);
}

void AWaterPhysicsActor::PreWaterPhysicsSceneTick()
{
	ReceivePreWaterPhysicsSceneTick();
}

bool AWaterPhysicsActor::FilterActorFromWaterPhysics(AActor* Actor)
{
	if (ReceiveFilterActorFromWaterPhysics(Actor))
		return true;

	return WaterPhysicsFilter.Num() != 0 
		&& FWaterPhysicsFilter::ProcessFilterList(Actor, WaterPhysicsFilter) != true;
}

#if WITH_EDITORONLY_DATA
void AWaterPhysicsActor::OnActorComponentsRecreated(AActor* Actor)
{
	AddActorToWater(Actor);
}
#endif

bool AWaterPhysicsActor::ShouldComponentSimulateWaterPhysics(UActorComponent* Component)
{
	const bool bImplementsInterface = Component->Implements<UWaterPhysicsCollisionInterface>();
	const bool bIsPrimitiveComponent = Component->IsA<UPrimitiveComponent>();
	return bImplementsInterface || (bIsPrimitiveComponent && Cast<UPrimitiveComponent>(Component)->Mobility == EComponentMobility::Movable);
}