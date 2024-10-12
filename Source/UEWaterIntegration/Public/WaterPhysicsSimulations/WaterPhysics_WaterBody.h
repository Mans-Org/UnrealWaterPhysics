// Copyright Mans Isaksson 2021. All Rights Reserved.

#pragma once
#include "WaterPhysicsSimulations/WaterPhysics_WaterBodyBase.h"
#include "WaterPhysics_WaterBody.generated.h"

class AWaterBody;

USTRUCT(BlueprintType)
struct FWaterBodySetup
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	AWaterBody* WaterBody = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	bool bIncludeWaves = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics",meta=(EditCondition="bIncludeWaves"))
	bool bUseSimpleWaves = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	bool bIncludeVelocity = true;
};

// A class for adding water physics simulation Unreal's built in water system. 
// This class uses overlapping with the water surface to automatically add/remove actors from the water physics simulation.
// To manually add/remove actors to the water physics simulation, use the AddActorToWater and RemoveActorFromWater functions on this actor.
UCLASS(BlueprintType, meta=(DisplayName="Water Physics - UE4 Water Body"))
class UEWATERINTEGRATION_API AWaterPhysics_WaterBody : public AWaterPhysics_WaterBodyBase
{
	GENERATED_BODY()

public:

	// List of UE4 water bodies which should be included in the water physics simulation.
	// If you have overlapping water bodies they will be prioritized in the order they appear in this list (Lower index equals more important).
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	TArray<FWaterBodySetup> WaterBodies;

protected:

	virtual int32 GetWaterBodyPriority(AActor* InWaterBody) const override;
	virtual TArray<AActor*> GetWaterBodies() const override;
	virtual FGetWaterInfoResult CalculateWaterBodyWaterInfo(AActor* InWaterBody, const UActorComponent* Component, const FVector& Location) const override;

};