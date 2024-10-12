// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "WaterPhysicsTypes.h"
#include "GameFramework/Actor.h"
#include "WaterPhysicsActor.generated.h"

UCLASS(abstract)
class WATERPHYSICS_API AWaterPhysicsActor : public AActor
{
	GENERATED_BODY()

private:
	struct FActorToRemove
	{
		TWeakObjectPtr<AActor> ActorToRemove;
		float Time;

		friend uint32 GetTypeHash(const FActorToRemove& O) { return GetTypeHash(O.ActorToRemove.GetEvenIfUnreachable()); }
		FORCEINLINE bool operator==(const FActorToRemove& O) const { return O.ActorToRemove.GetEvenIfUnreachable() == ActorToRemove.GetEvenIfUnreachable(); }
	};
	TSet<FActorToRemove> ActorsToRemove;

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Water Volume", meta=(AllowPrivateAccess = "true"))
	class UWaterPhysicsSceneComponent* WaterPhysicsSceneComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TMap<AActor*, UActorComponent*> EditorComponentValidationTable;
#endif

public:

	// Only include actors which satisfies this filter. Leave empty for no filter.
	// For more advanced filtering, look at overriding "Filter Actor From Water Physics" on this actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water Physics Actor")
	TArray<FWaterPhysicsFilter> WaterPhysicsFilter;

public:

	AWaterPhysicsActor();

	UFUNCTION(BlueprintPure, Category="Water Physics Actor")
	FORCEINLINE UWaterPhysicsSceneComponent* GetWaterPhysicsSceneComponent() const { return WaterPhysicsSceneComponent; }

	void Tick(float DeltaTime) override;

	/* Adds the actor to this water physics simulations */
	UFUNCTION(BlueprintCallable, Category="Water Physics Actor")
	void AddActorToWater(AActor* Actor);
	
	/* 
		Removes the actor from this water physics simulations.

		RemoveDelay: Delay before removing the actor. 
			A RemoveDelay of 0 leads to a one frame delay.
			A RemoveDelay of > 0 leads to instant removal.
			Calling AddActorToWater will remove the actor from pending removal.
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics Actor")
	void RemoveActorFromWater(AActor* Actor, float RemoveDelay = -1.f);

	/*
		Notifies the water physics simulation that a setting on the supplied WaterPhysicsSettingsComponent has been changed. 
		Causing the owning actor to have its water physics settings updated, if it exists in the water physics simulation.
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics Actor")
	void NotifyWaterPhysicsSettingsChanged(class UWaterPhysicsSettingsComponent* WaterPhysicsSettingsComponent);

	/*
		Blueprint implementable interface for calculating water surface information.
	*/
	UFUNCTION(BlueprintImplementableEvent, Category="Water Physics Actor", meta=(DisplayName="Calculate Water Info"))
	FGetWaterInfoResult ReceiveCalculateWaterInfo(const UActorComponent* Component, const FVector& Location);

	/*
		Blueprint implementable event for when an actor gets added to the water physics simulation.
	*/
	UFUNCTION(BlueprintImplementableEvent, Category="Water Physics Actor", meta=(DisplayName="On Actor Added To Water"))
	void ReceiveOnActorAddedToWater(AActor* Actor);

	/*
		Blueprint implementable event for when an actor gets removed from the water physics simulation.
	*/
	UFUNCTION(BlueprintImplementableEvent, Category="Water Physics Actor", meta=(DisplayName="On Actor Removed From Water"))
	void ReceiveOnActorRemovedFromWater(AActor* Actor);

	/*
		Blueprint implementable event called before the water physics scene is ticked.
	*/
	UFUNCTION(BlueprintImplementableEvent, Category="Water Physics Actor", meta=(DisplayName="Pre Water Physics Scene Tick"))
	void ReceivePreWaterPhysicsSceneTick();

	/*
		Blueprint implementable event for filtering actors from this water physics simulation. 
		Return true to prevent the actor from being added to the water physics simulation.
	*/
	UFUNCTION(BlueprintImplementableEvent, Category="Water Physics Actor", meta=(DisplayName="Filter Actor From Water Physics"))
	bool ReceiveFilterActorFromWaterPhysics(AActor* Actor);
	bool ReceiveFilterActorFromWaterPhysics_Implementation(AActor* Actor) { return false; }

	/*
		Native interface for calculating water surface information. Calls ReceiveCalculateWaterInfo by default.
	*/
	virtual FGetWaterInfoResult CalculateWaterInfo(const UActorComponent* Component, const FVector& Location);

	/*
		Native event for when an actor gets added to the water physics simulation.
	*/
	virtual void OnActorAddedToWater(AActor* Actor);

	/*
		Native event for when an actor gets removed from the water physics simulation.
	*/
	virtual void OnActorRemovedFromWater(AActor* Actor);

	/*
		Native event called before the water physics scene is ticked.
	*/
	virtual void PreWaterPhysicsSceneTick();

	/*
		Return true to prevent the actor from being added to the water physics simulation.
	*/
	virtual bool FilterActorFromWaterPhysics(AActor* Actor);

#if WITH_EDITORONLY_DATA
	/*
		Gets called when the root component on an actor in our simulation gets recreated. Happens in the editor when properies are edited through the editor details panel.
	*/
	virtual void OnActorComponentsRecreated(AActor* Actor);
#endif

	static bool ShouldComponentSimulateWaterPhysics(UActorComponent* Component);

};