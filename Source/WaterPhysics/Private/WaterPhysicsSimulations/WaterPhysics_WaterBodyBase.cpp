// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsSimulations/WaterPhysics_WaterBodyBase.h"
#include "WaterPhysicsSceneComponent.h"
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"

AWaterPhysics_WaterBodyBase::AWaterPhysics_WaterBodyBase()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(FName(TEXT("Root Component"))));

#if WITH_EDITORONLY_DATA
	ConstructorHelpers::FObjectFinder<UTexture2D> BillboardIconFinder(TEXT("/WaterPhysics/Icons/WaterPhysics"));
	if (UBillboardComponent* BillboardComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("BillboardComponent"), true))
	{
		BillboardComponent->SetSprite(BillboardIconFinder.Object);
		BillboardComponent->bIsScreenSizeScaled = true;
		BillboardComponent->SetupAttachment(GetRootComponent());
	}
	SpriteScale = 2.f;
#endif

	WaterPhysicsSceneComponent->SetWaterInfoGetterThreadSafe(true);
}

void AWaterPhysics_WaterBodyBase::BeginPlay()
{
	Super::BeginPlay();

	// Cache the result of IsFunctionImplementedInScript since CalculateWaterBodyWaterInfo is very much a hot-path and we want to avoid calling this every time.
	bBPOverridesCalculateWaterInfoForWaterBody = GetClass()->IsFunctionImplementedInScript(TEXT("ReceiveCalculateWaterInfoForWaterBody"));

	// TODO: This does not allow for changing active water bodies at runtime, that should be added at some point
	for (AActor* WaterBody : GetWaterBodies())
	{
		if (IsValid(WaterBody))
		{
			WaterBody->OnActorBeginOverlap.AddDynamic(this, &AWaterPhysics_WaterBodyBase::OnActorBeginOverlapWaterBody);
			WaterBody->OnActorEndOverlap.AddDynamic(this, &AWaterPhysics_WaterBodyBase::OnActorEndOverlapWaterBody);

			TArray<AActor*> OverlappingActors;
			WaterBody->UpdateOverlaps(false);
			WaterBody->GetOverlappingActors(OverlappingActors);
			for (AActor* Actor : OverlappingActors)
				OnActorBeginOverlapWaterBody(WaterBody, Actor);
		}
	}
}

void AWaterPhysics_WaterBodyBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (auto It = WaterBodiesToRemove.CreateIterator(); It; ++It)
	{
		AActor* Actor = It->Actor.Get();
		if (!Actor)
		{
			It.RemoveCurrent();
			continue;
		}

		if ((It->Time -= DeltaTime) <= 0.f)
		{
			if (FWaterBodyArray* CurrentWaterBodies = ActorCurrentWaterBodies.Find(Actor))
			{
				(*CurrentWaterBodies)->Remove(It->WaterBody.GetEvenIfUnreachable());
				if ((*CurrentWaterBodies)->Num() == 0)
				{
					ActorCurrentWaterBodies.Remove(Actor);
					RemoveActorFromWater(Actor, -1.f);
				}
			}
			It.RemoveCurrent();
		}
	}
}

void AWaterPhysics_WaterBodyBase::OnActorBeginOverlapWaterBody(AActor* OverlappedActor, AActor* OtherActor)
{
	FWaterBodyArray& CurrentWaterBodies = ActorCurrentWaterBodies.FindOrAdd(OtherActor);
	CurrentWaterBodies->AddUnique(OverlappedActor);

	// Priority sort CurrentWaterBodies here, to avoid the expensive operation inside CalculateWaterInfo
	PrioritySortWaterBodyArray(OtherActor, CurrentWaterBodies);

	const int32 RemovedNum = WaterBodiesToRemove.RemoveAll([&](const auto& X) { return X.WaterBody == OverlappedActor && X.Actor == OtherActor; });

	// Avoid the expensive operation of adding the actor to the water physics if it was pending remove, or if it's already in water physics scene
	if (RemovedNum == 0 && CurrentWaterBodies->Num() == 1) 
	{
		AddActorToWater(OtherActor);
	}
}

void AWaterPhysics_WaterBodyBase::OnActorEndOverlapWaterBody(AActor* OverlappedActor, AActor* OtherActor)
{
	// Remove delay to reduce adding/removal when object is skipping along the surface
	WaterBodiesToRemove.Add({ OverlappedActor, OtherActor, 0.5f });

	// Since the body is pending remove, its priority has changed
	if (FWaterBodyArray* CurrentWaterBodies = ActorCurrentWaterBodies.Find(OtherActor))
		PrioritySortWaterBodyArray(OtherActor, *CurrentWaterBodies);
}

FGetWaterInfoResult AWaterPhysics_WaterBodyBase::CalculateWaterInfo(const UActorComponent* Component, const FVector& Location)
{
	check(IsValid(Component));

	if (FWaterBodyArray* CurrentWaterBodies = ActorCurrentWaterBodies.Find(Component->GetOwner()))
	{
		for (AActor* CurrentWaterBody : **CurrentWaterBodies)
		{
			if (bBPOverridesCalculateWaterInfoForWaterBody)
			{
				return ReceiveCalculateWaterInfoForWaterBody(CurrentWaterBody, Component, Location);
			}
			else
			{
				return CalculateWaterBodyWaterInfo(CurrentWaterBody, Component, Location);
			}
		}
	}

	ensureMsgf(false, TEXT("Tried to get water info in actor which is not in any water body %s.%s"), 
		(Component->GetOwner() ? *Component->GetOwner()->GetName() : TEXT("None")), *Component->GetName());

	return FGetWaterInfoResult();
}

int32 AWaterPhysics_WaterBodyBase::GetWaterBodyPriority(AActor* InWaterBody) const
{
	return -1;
}

TArray<AActor*> AWaterPhysics_WaterBodyBase::GetWaterBodies() const
{
	ensureMsgf(false, TEXT("Base implementation of GetWaterBodies called"));
	return TArray<AActor*>();
}

FGetWaterInfoResult AWaterPhysics_WaterBodyBase::CalculateWaterBodyWaterInfo(AActor* WaterBody, const UActorComponent* Component, const FVector& Location) const
{
	ensureMsgf(false, TEXT("Base implementation of CalculateWaterBodyWaterInfo called"));
	return FGetWaterInfoResult();
}

void AWaterPhysics_WaterBodyBase::PrioritySortWaterBodyArray(AActor* WaterBodyArrayOwner, FWaterBodyArray& WaterBodyArray)
{
	TMap<AActor*, int32> WaterBodyPriority;
	WaterBodyPriority.Reserve(WaterBodyArray->Num() * 2);
	for (int32 i = 0; i < WaterBodyArray->Num(); i++)
	{
		AActor* WaterBody = (*WaterBodyArray)[i];
		WaterBodyPriority.Add(WaterBody, GetWaterBodyPriority(WaterBody));
	}

	WaterBodyArray->Sort([&](AActor& A, AActor& B) { return WaterBodyPriority[&A] < WaterBodyPriority[&B]; });
}
