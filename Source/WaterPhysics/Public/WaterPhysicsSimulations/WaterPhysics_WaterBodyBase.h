// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "WaterPhysicsActor.h"
#include "WaterPhysics_WaterBodyBase.generated.h"

// UE4 Reflection cannot handle nested containers, so we wrap this array with a struct
USTRUCT(BlueprintType)
struct FWaterBodyArray
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Transient)
	TArray<AActor*> WaterBodies;

	TArray<AActor*>* operator->() { return &WaterBodies; }
	const TArray<AActor*>* operator->() const { return &WaterBodies; }
	const TArray<AActor*>& operator*() const { return WaterBodies; }
};

// Base implementation for adding water physics simulation to any set of actors which can generate overlap events.
UCLASS(abstract)
class WATERPHYSICS_API AWaterPhysics_WaterBodyBase : public AWaterPhysicsActor
{
	GENERATED_BODY()

private:

	UPROPERTY(Transient)
	TMap<AActor*, FWaterBodyArray> ActorCurrentWaterBodies;

	struct FWaterBodyToRemove
	{
		TWeakObjectPtr<AActor> WaterBody;
		TWeakObjectPtr<AActor> Actor;
		float Time;
	};
	TArray<FWaterBodyToRemove> WaterBodiesToRemove;

	bool bBPOverridesCalculateWaterInfoForWaterBody = false;

public:

	AWaterPhysics_WaterBodyBase();

	void BeginPlay() override;
	
	void Tick(float DeltaTime) override;

	UFUNCTION()
	void OnActorBeginOverlapWaterBody(AActor* OverlappedActor, AActor* OtherActor);

	UFUNCTION()
	void OnActorEndOverlapWaterBody(AActor* OverlappedActor, AActor* OtherActor);

	FGetWaterInfoResult CalculateWaterInfo(const UActorComponent* Component, const FVector& Location) override;

protected:

	// If an actor is in multiple water bodies, override this function to specify which one should take priority.
	virtual int32 GetWaterBodyPriority(AActor* InWaterBody) const;

	// Returns a list of all actors which represents this water body. If there are multiple actors, use GetWaterBodyPriority to specify
	// which water body actor takes priority when an actor overlaps multiple at once.
	virtual TArray<AActor*> GetWaterBodies() const;

	// Override this function to calculate the water surface location for a given water body and location.
	virtual FGetWaterInfoResult CalculateWaterBodyWaterInfo(AActor* WaterBody, const UActorComponent* Component, const FVector& Location) const;

	// Blueprint overridable function for calculating the water surface location for a given water body and location.
	UFUNCTION(BlueprintImplementableEvent, Category="Water Physics Actor", meta=(DisplayName="Calculate Water Info For Water Body"))
	FGetWaterInfoResult ReceiveCalculateWaterInfoForWaterBody(AActor* WaterBody, const UActorComponent* Component, const FVector& Location) const;

private:

	void PrioritySortWaterBodyArray(AActor* WaterBodyArrayOwner, FWaterBodyArray& WaterBodyArray);
	
};