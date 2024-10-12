// Copyright Mans Isaksson 2021. All Rights Reserved.

#pragma once
#include "WaterPhysicsSimulations/WaterPhysicsActor.h"
#include "WaterPhysics_Oceanology.generated.h"

// UE4 Reflection cannot handle nested containers, so we wrap this array with a struct
USTRUCT(BlueprintType)
struct FActorArray
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Transient)
	TArray<AActor*> Actors;

	TArray<AActor*>* operator->() { return &Actors; }
	const TArray<AActor*>* operator->() const { return &Actors; }
	const TArray<AActor*>& operator*() const { return Actors; }
};

USTRUCT()
struct FOceanologyThreadCopy
{
	GENERATED_BODY()

	FOceanologyThreadCopy() = default;
	virtual ~FOceanologyThreadCopy();

	UPROPERTY(Transient)
	AActor* ThreadCopy = nullptr;

	TArray<FProperty*, TInlineAllocator<8>> PropertiesToCopy;

	void SyncWithMaster(AActor* MasterOceanologyActor);

	void DestroyThreadCopy();
};

UCLASS(BlueprintType, meta=(DisplayName="Water Physics - Oceanology"))
class OCEANOLOGYINTEGRATION_API AWaterPhysics_Oceanology : public AWaterPhysicsActor
{
	GENERATED_BODY()

private:
	UPROPERTY(Transient)
	UFunction* GetWaveHightFunction = nullptr;

	UPROPERTY(Transient)
	TMap<uint32, FOceanologyThreadCopy> OceanologyThreadCopies;

	FCriticalSection ThreadCopiesCS;

	UPROPERTY(Transient)
	TMap<AActor*, FActorArray> ActorOverlapTracker;

	bool bSupportsParallelWaterHeightFetching = false;

public:

	// A reference to the oceanology water which should have water physics added to it
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	AActor* OceanologyWater = nullptr;

	// Actors which should float on the Oceanology water. To dynamically add/remove actors from the water physics simulation,
	// call the AddActorToWater and RemoveActorFromWater functions on this actor.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	TArray<AActor*> InitiallySimulatedActors;

	// When these actors generate OnActorBegin/End overlap events, the overlapped actor will be automatically added/removed from the water physics
	// simulation. As long as the actor overlaps any of the actors in this list it will be part of the water physics simulation.
	// 
	// Usefull to define the bounds of the ocean as Oceanology does not generate overlap events.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	TArray<AActor*> OceanBoundsActors;

public:

	AWaterPhysics_Oceanology();

	void BeginPlay() override;
	void EndPlay(EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	FGetWaterInfoResult CalculateWaterInfo(const UActorComponent* Component, const FVector& Location) override;

	void PreWaterPhysicsSceneTick() override;

private:
	
	UFUNCTION()
	void OnActorBeginOverlapBoundsActor(AActor* OverlappedActor, AActor* OtherActor);

	UFUNCTION()
	void OnActorEndOverlapBoundsActor(AActor* OverlappedActor, AActor* OtherActor);


};