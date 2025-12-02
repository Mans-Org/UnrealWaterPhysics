// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsSceneComponent.h"
#include "WaterPhysicsModule.h"
#include "WaterPhysicsCollisionInterface.h"
#include "Engine/World.h"
#include "Physics/PhysicsInterfaceScene.h"
#include "GameFramework/WorldSettings.h"
#include "WorldAlignedWaterSurfaceProvider.h"
#include "Components/PrimitiveComponent.h"

UWaterPhysicsSceneComponent::UWaterPhysicsSceneComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	WaterSurfaceProvider = MakeWaterSurfaceProvider();
}

void UWaterPhysicsSceneComponent::BeginPlay()
{
	Super::BeginPlay();

	if (FPhysScene* PhysScene = GetWorld()->GetPhysicsScene())
	{
		PhysScene->OnPhysScenePreTick.AddUObject(this, &UWaterPhysicsSceneComponent::PreStepWaterPhysics);
		PhysScene->OnPhysSceneStep.AddUObject(this, &UWaterPhysicsSceneComponent::StepWaterPhysics);
	}
}

void UWaterPhysicsSceneComponent::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	WaterPhysicsScene.ClearWaterPhysicsScene();

	Super::EndPlay(EndPlayReason);
}

void UWaterPhysicsSceneComponent::AddActorToWaterPhysics(AActor* Actor, const FWaterPhysicsSettings& WaterPhysicsSettings)
{
	if (!IsValid(Actor))
	{
		UE_LOG(LogWaterPhysics, Error, TEXT("%s.AddActorToWaterPhysics Invalid Actor"), *GetName());
		return;
	}
	
	for (UActorComponent* ActorComponent : Actor->GetComponents())
	{
		if (IsValid(ActorComponent) && ActorComponent->Implements<UWaterPhysicsCollisionInterface>())
			AddComponentToWaterPhysics(ActorComponent, WaterPhysicsSettings);
		else if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(ActorComponent))
			AddComponentToWaterPhysics(PrimitiveComponent, WaterPhysicsSettings);
	}
}

bool UWaterPhysicsSceneComponent::RemoveActorFromWaterPhysics(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		UE_LOG(LogWaterPhysics, Error, TEXT("%s.RemoveActorFromWaterPhysics Invalid Actor"), *GetName());
		return false;
	}

	bool bRemoved = false;
	for (UActorComponent* ActorComponent : Actor->GetComponents())
	{
		bRemoved |= RemoveComponentFromWaterPhysics(ActorComponent);
	}
	return bRemoved;
}

void UWaterPhysicsSceneComponent::AddActorsToWaterPhysics(const TArray<AActor*>& Actors, const FWaterPhysicsSettings& WaterPhysicsSettings)
{
	for (AActor* Actor : Actors)
		AddActorToWaterPhysics(Actor, WaterPhysicsSettings);
}

bool UWaterPhysicsSceneComponent::RemoveActorsFromWaterPhysics(const TArray<AActor*>& Actors)
{
	bool bRemoved = false;
	for (AActor* Actor : Actors)
	{
		bRemoved |= RemoveActorFromWaterPhysics(Actor);
	}
	return bRemoved;
}

void UWaterPhysicsSceneComponent::AddComponentToWaterPhysics(UActorComponent* Component, const FWaterPhysicsSettings& WaterPhysicsSettings, bool bAllBodies, FName BodyName)
{
	if (!IsValid(Component))
	{
		UE_LOG(LogWaterPhysics, Error, TEXT("%s.AddComponentToWaterPhysics Invalid Component"), *GetName());
		return;
	}

	const bool bImplementsCollisionInterface = Component->Implements<UWaterPhysicsCollisionInterface>();
	UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);

	if (!bImplementsCollisionInterface && PrimitiveComponent == nullptr)
	{
		UE_LOG(LogWaterPhysics, Error, TEXT("%s.AddComponentToWaterPhysics Tried adding component %s which is not a PrimitiveComponent and does not implement WaterPhysicsCollisionInterface"), *GetName(), *Component->GetName());
		return;
	}

	TArray<FName> Sockets;
	if (bAllBodies)
	{
		if (Component->Implements<UWaterPhysicsCollisionInterface>())
			Sockets = dynamic_cast<IWaterPhysicsCollisionInterface*>(Component)->GetAllBodyNames();
		else
			Sockets = PrimitiveComponent->GetAllSocketNames();

		Sockets.AddUnique(NAME_None);
	}
	else
		Sockets.Add(BodyName);

	for (const FName& SocketName : Sockets)
		WaterPhysicsScene.AddComponentBody(Component, SocketName, WaterPhysicsSettings);
}

bool UWaterPhysicsSceneComponent::RemoveComponentFromWaterPhysics(UActorComponent* Component, bool bAllBodies, FName BodyName)
{
	if (bAllBodies)
		return WaterPhysicsScene.RemoveComponent(Component);
	else
		return WaterPhysicsScene.RemoveComponentBody(Component, BodyName);
}

void UWaterPhysicsSceneComponent::SetComponentWaterPhysicsSettings(UActorComponent* Component, const FWaterPhysicsSettings& WaterPhysicsSettings, bool bAllBodies, FName BodyName)
{
	if (!IsValid(Component))
	{
		UE_LOG(LogWaterPhysics, Error, TEXT("%s.SetPrimitiveWaterPhysicsSettings Invalid Component"), *GetName());
		return;
	}

	if (bAllBodies)
	{
		auto* Bodies = WaterPhysicsScene.FindComponentBodies(Component);
		if (!Bodies)
		{
			UE_LOG(LogWaterPhysics, Error, TEXT("%s.SetPrimitiveWaterPhysicsSettings No Valid Water Physics for Component %s"), *GetName(), *Component->GetName());
			return;
		}

		for (auto& Body : *Bodies)
			Body.WaterPhysicsSettings = WaterPhysicsSettings;
	}
	else
	{
		auto* Body = WaterPhysicsScene.FindComponentBody(Component, BodyName);
		if (!Body)
		{
			UE_LOG(LogWaterPhysics, Error, TEXT("%s.SetPrimitiveWaterPhysicsSettings No Valid Water Physics for Component body %s.%s"), *GetName(), *Component->GetName(), *BodyName.ToString());
			return;
		}

		Body->WaterPhysicsSettings = WaterPhysicsSettings;
	}
}

bool UWaterPhysicsSceneComponent::ContainsComponent(UActorComponent* Component) const
{
	return WaterPhysicsScene.ContainsComponent(Component);
}

bool UWaterPhysicsSceneComponent::ContainsComponentBody(UActorComponent* Component, FName BodyName) const
{
	return WaterPhysicsScene.FindComponentBody(Component, BodyName) != nullptr;
}

FWaterPhysicsActingForces UWaterPhysicsSceneComponent::GetComponentActingWaterPhysicsForces(UActorComponent* Component) const
{
	FWaterPhysicsActingForces OutActingForces;

	if (const TArray<FWaterPhysicsScene::FWaterPhysicsBody>* WaterPhysicsBodies = WaterPhysicsScene.FindComponentBodies(Component))
	{
		for (const FWaterPhysicsScene::FWaterPhysicsBody& WaterPhysicsBody : *WaterPhysicsBodies)
			OutActingForces += FWaterPhysicsActingForces(WaterPhysicsBody.ActingForces);
	}

	return OutActingForces;
}

FWaterPhysicsActingForces UWaterPhysicsSceneComponent::GetComponentBodyActingWaterPhysicsForces(UActorComponent* Component, FName BodyName) const
{
	if (const FWaterPhysicsScene::FWaterPhysicsBody* WaterPhysicsBody = WaterPhysicsScene.FindComponentBody(Component, BodyName))
		return FWaterPhysicsActingForces(WaterPhysicsBody->ActingForces);

	return FWaterPhysicsActingForces();
}

float UWaterPhysicsSceneComponent::GetComponentSubmergedArea(UActorComponent* Component) const
{
	float OutSubmergedArea = 0.f;

	if (const TArray<FWaterPhysicsScene::FWaterPhysicsBody>* WaterPhysicsBodies = WaterPhysicsScene.FindComponentBodies(Component))
	{
		for (const FWaterPhysicsScene::FWaterPhysicsBody& WaterPhysicsBody : *WaterPhysicsBodies)
			OutSubmergedArea += WaterPhysicsBody.SubmergedArea;
	}

	return OutSubmergedArea;
}

float UWaterPhysicsSceneComponent::GetComponentBodySubmergedArea(UActorComponent* Component, FName BodyName) const
{
	if (const FWaterPhysicsScene::FWaterPhysicsBody* WaterPhysicsBody = WaterPhysicsScene.FindComponentBody(Component, BodyName))
		return WaterPhysicsBody->SubmergedArea;

	return 0.f;
}

void UWaterPhysicsSceneComponent::K2_SetWaterInfoGetter(const FBlueprintGetWaterInfoAtLocation& InWaterInfoGetter, bool bThreadSafe)
{
	UObject* BoundObject = const_cast<UObject*>(InWaterInfoGetter.GetUObject()); // CreateWeakLambda cannot take const UObject* for some reason
	WaterInfoGetter = FGetWaterInfoAtLocation::CreateWeakLambda(BoundObject, [InWaterInfoGetter](const UActorComponent* Component, const FVector& Location)->FGetWaterInfoResult
	{
		return InWaterInfoGetter.Execute(Component, Location);
	});
	bWaterInfoGetterThreadSafe = bThreadSafe;
}

void UWaterPhysicsSceneComponent::SetWaterInfoGetter(const FGetWaterInfoAtLocation& InWaterInfoGetter, bool bThreadSafe)
{
	WaterInfoGetter = InWaterInfoGetter;
	bWaterInfoGetterThreadSafe = bThreadSafe;
}

void UWaterPhysicsSceneComponent::SetWaterSurfaceProvider(const TSharedPtr<FWaterSurfaceProvider>& NewWaterSurfaceProvider)
{
	WaterSurfaceProvider = NewWaterSurfaceProvider;
}

void UWaterPhysicsSceneComponent::SetWaterInfoGetterThreadSafe(bool bThreadSafe)
{
	bWaterInfoGetterThreadSafe = bThreadSafe;
}

void UWaterPhysicsSceneComponent::PreStepWaterPhysics(FPhysScene* PhysScene, float DeltaTime)
{
	if (IsComponentTickEnabled())
	{
		PreStepWaterPhysicsScene.Broadcast();
		K2_PreStepWaterPhysicsScene.Broadcast();
	}
}

void UWaterPhysicsSceneComponent::StepWaterPhysics(FPhysScene* PhysScene, float DeltaTime)
{
	if (IsComponentTickEnabled())
	{
		auto World = GetWorld();
		if(!World)
		{
			return;
		}
		auto WorldSettings = World->GetWorldSettings();
		if(!WorldSettings)
		{
			return;
		}
		const FVector Gravity = FVector(0, 0, GetWorld()->GetWorldSettings() ? GetWorld()->GetWorldSettings()->GetGravityZ() : -980.f);
		WaterPhysicsScene.StepWaterPhysicsScene(DeltaTime, Gravity, DefaultWaterPhysicsSettings, WaterInfoGetter, bWaterInfoGetterThreadSafe, WaterSurfaceProvider.Get(), this);

		if (bDrawWaterInfoDebug)
			WaterSurfaceProvider->DrawDebugProvider(GetWorld());
	}
}

TSharedPtr<FWaterSurfaceProvider> UWaterPhysicsSceneComponent::MakeWaterSurfaceProvider() const
{
	return MakeShared<FWorldAlignedWaterSurfaceProvider>();
}