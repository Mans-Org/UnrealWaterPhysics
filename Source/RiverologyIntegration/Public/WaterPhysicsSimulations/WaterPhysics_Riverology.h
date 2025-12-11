// Copyright Mans Isaksson 2021. All Rights Reserved.

#pragma once
#include "WaterPhysicsSimulations/WaterPhysics_WaterBodyBase.h"
#include "WaterPhysics_Riverology.generated.h"

USTRUCT(BlueprintType)
struct FRiverologyWaterBodySetup
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	AActor* RiverologyWater = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	bool bIncludeVelocity = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics", meta=(EditCondition="bIncludeVelocity"))
	float WaterVelocity = 100.f;

	UPROPERTY(Transient)
	class USplineComponent* SplineComponent = nullptr;
};

UCLASS(BlueprintType, meta=(DisplayName="Water Physics - Riverology"))
class RIVEROLOGYINTEGRATION_API AWaterPhysics_Riverology : public AWaterPhysics_WaterBodyBase
{
	GENERATED_BODY()

public:

	// List of Riverology water bodies which should be included in the water physics simulation.
	// If you have overlapping rivers they will be prioritized in the order they appear in this list (Lower index equals more important).
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Water Physics")
	TArray<FRiverologyWaterBodySetup> RiverologyWaterBodies;

public:

	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual int32 GetWaterBodyPriority(AActor* InWaterBody) const override;
	virtual TArray<AActor*> GetWaterBodies() const override;
	virtual FGetWaterInfoResult CalculateWaterBodyWaterInfo(AActor* InWaterBody, const UActorComponent* Component, const FVector& Location) const override;
};