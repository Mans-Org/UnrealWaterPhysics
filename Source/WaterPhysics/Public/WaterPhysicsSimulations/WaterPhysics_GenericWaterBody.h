// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "WaterPhysicsSimulations/WaterPhysics_WaterBodyBase.h"
#include "WaterPhysics_GenericWaterBody.generated.h"

// A class for adding water physics simulation to any actor which can generate overlap events.
// The surface of the water is assumed to be at the center of the actor included as a water body.
// 
// To manually specify the location of the water surface, override the CalculateWaterInfoForWaterBody function either in C++ or in Blueprint.
// IMPORTANT: By default parallell fetching of water surface info is enabled, if your water surface calculation is not thread safe then this needs to be disabled!!!
UCLASS(BlueprintType, meta=(DisplayName="Water Physics - Generic Water Body"))
class WATERPHYSICS_API AWaterPhysics_GenericWaterBody : public AWaterPhysics_WaterBodyBase
{
	GENERATED_BODY()

protected:

	// List of actors which should be included as a water body in the water physics simulation.
	// If you have overlapping actors they will be prioritized in the order they appear in this list (Lower index equals more important).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Generic Water Volume")
	TArray<AActor*> WaterBodies;

protected:

	virtual int32 GetWaterBodyPriority(AActor* InWaterBody) const override;
	virtual TArray<AActor*> GetWaterBodies() const override;
	virtual FGetWaterInfoResult CalculateWaterBodyWaterInfo(AActor* WaterBody, const UActorComponent* Component, const FVector& Location) const override;

};