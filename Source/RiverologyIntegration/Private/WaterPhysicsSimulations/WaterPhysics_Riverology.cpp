// Copyright Mans Isaksson 2021. All Rights Reserved.

#include "WaterPhysicsSimulations/WaterPhysics_Riverology.h"
#include "RiverologyIntegrationModule.h"
#include "WaterPhysicsSettingsComponent.h"
#include "WaterPhysicsSceneComponent.h"
#include "WaterPhysicsCompatibilityLayer.h"
#include "Components/SplineComponent.h"
#include "Misc/UObjectToken.h"

namespace Riverology
{
	const static TCHAR* RiverologyClassName_Version1 = TEXT("/Riverology_Plugin/Advanced/Blueprints/BP_Riverology.BP_Riverology_C");
	const static TCHAR* RiverologyClassName_Version2 = TEXT("/Script/Riverology_Plugin.Riverology");
	const static TCHAR* RiverologySplineComponentName = TEXT("SplineComponent");
	const static TCHAR* RiverologyEnableBuoyancyPropertyName = TEXT("Enable Buoyancy");

	UClass* GetRiverologyClass()
	{
		if (RiverologyIntegrationModule::RiverologyMarjorVersion > 1)
		{
			return LoadClass<UObject>(nullptr, RiverologyClassName_Version2);
		}
		else
		{
			return LoadClass<UObject>(nullptr, RiverologyClassName_Version1);
		}
	}

	FName GetRiverologySplineComponentPropertyName()
	{
		return RiverologySplineComponentName;
	}

	FName GetRiverologyEnableBuoyancyPropertyName()
	{
		return RiverologyEnableBuoyancyPropertyName;
	}

	UClass* StaticRiverologyClass = nullptr;
	FProperty* StaticRiverologySplineComponentProperty = nullptr;
	FProperty* StaticRiverologyEnableBuoyancyProperty = nullptr;

	class USplineComponent* FindRiverologySplineComponent(AActor* InObject)
	{
		UClass* RiverologyClass = GetRiverologyClass();
		if (RiverologyClass == nullptr || !InObject->IsA(RiverologyClass))
		{
			return nullptr;
		}

		FProperty* SplineComponentProperty = RiverologyClass->FindPropertyByName(GetRiverologySplineComponentPropertyName());
		if (SplineComponentProperty == nullptr)
		{
			return nullptr;
		}

		USplineComponent** SplineComponent = SplineComponentProperty->ContainerPtrToValuePtr<USplineComponent*>((void*)InObject);
		check(SplineComponent != nullptr);

		return *SplineComponent;
	}

	bool* GetEnableBuoyancyPtr(UObject* InObject)
	{
		UClass* RiverologyClass = GetRiverologyClass();
		if (RiverologyClass == nullptr || !InObject->IsA(RiverologyClass))
		{
			return nullptr;
		}

		FProperty* EnableBuoyancyProperty = RiverologyClass->FindPropertyByName(GetRiverologySplineComponentPropertyName());
		if (EnableBuoyancyProperty == nullptr)
		{
			return nullptr;
		}

		bool* EnableBuoyancyBooleanValue = EnableBuoyancyProperty->ContainerPtrToValuePtr<bool>(InObject);
		check(EnableBuoyancyBooleanValue);

		return EnableBuoyancyBooleanValue;
	}
};

void AWaterPhysics_Riverology::BeginPlay()
{
	for (FRiverologyWaterBodySetup& WaterBodySetup : RiverologyWaterBodies)
	{
		if (IsValid(WaterBodySetup.RiverologyWater))
		{
			// Disable default Riverology buoyancy
			if (bool* EnableBuoyancyProperty = Riverology::GetEnableBuoyancyPtr(WaterBodySetup.RiverologyWater))
			{
				*EnableBuoyancyProperty = false;
			}

			WaterBodySetup.SplineComponent = Riverology::FindRiverologySplineComponent(WaterBodySetup.RiverologyWater);
		}
	}

	Super::BeginPlay();
}

#if WITH_EDITOR
void AWaterPhysics_Riverology::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AWaterPhysics_Riverology, RiverologyWaterBodies))
	{
		for (FRiverologyWaterBodySetup& RiverologyWaterBodySetup : RiverologyWaterBodies)
		{
			if (RiverologyWaterBodySetup.RiverologyWater != nullptr && !RiverologyWaterBodySetup.RiverologyWater->IsA(Riverology::GetRiverologyClass()))
			{
				const static FText ErrMsgPt1 = NSLOCTEXT("WaterPhysicsRiverology", "PostEditChangePropertyStart", "Actor");
				const static FText ErrMsgPt2 = NSLOCTEXT("WaterPhysicsRiverology", "PostEditChangePropertyEnd", "is not a Riverology actor.");

				FMessageLog("Blueprint").Warning()
					->AddToken(FTextToken::Create(ErrMsgPt1))
					->AddToken(FUObjectToken::Create(RiverologyWaterBodySetup.RiverologyWater))
					->AddToken(FTextToken::Create(ErrMsgPt2));

				FMessageLog("Blueprint").Notify();

				RiverologyWaterBodySetup.RiverologyWater = nullptr;
			}
		}
		
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

int32 AWaterPhysics_Riverology::GetWaterBodyPriority(AActor* InWaterBody) const
{
	return RiverologyWaterBodies.IndexOfByPredicate([&](const auto& X) { return X.RiverologyWater == InWaterBody; });
}

TArray<AActor*> AWaterPhysics_Riverology::GetWaterBodies() const
{
	TArray<AActor*> OutActors;
	OutActors.Reserve(RiverologyWaterBodies.Num());
	for (const FRiverologyWaterBodySetup& RiverologyWater : RiverologyWaterBodies)
	{
		OutActors.Add(RiverologyWater.RiverologyWater);
	}
	return OutActors;
}

FGetWaterInfoResult AWaterPhysics_Riverology::CalculateWaterBodyWaterInfo(AActor* InWaterBody, const UActorComponent* Component, const FVector& Location) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CalculateRiverologyWaterHeight);

	const FRiverologyWaterBodySetup* RiverologySetup = RiverologyWaterBodies.FindByPredicate([&](const auto& X) { return X.RiverologyWater == InWaterBody; });
	check(RiverologySetup);

	USplineComponent* SplineComponent = RiverologySetup->SplineComponent;
	if (!IsValid(SplineComponent))
	{
		return FGetWaterInfoResult{ FVector::ZeroVector, FVector::OneVector, FVector::ZeroVector };
	}

	const FTransform SplineTransform = SplineComponent->FindTransformClosestToWorldLocation(Location, ESplineCoordinateSpace::World, false);
	const FVector WaterSurfaceNormal = FVector::UpVector; // NOTE: Riverology does not support rotating the spline, if this changes then we should update this
	const FVector WaterSurfaceLocation = FVector::PointPlaneProject(Location, SplineTransform.GetLocation(), WaterSurfaceNormal);

	FVector WaterVelocity = FVector::ZeroVector;

	if (RiverologySetup->bIncludeVelocity)
	{
		const FVector SplineForwardVector = SplineTransform.GetRotation().GetForwardVector();
		WaterVelocity = SplineForwardVector * RiverologySetup->WaterVelocity;
	}

	return FGetWaterInfoResult{ WaterSurfaceLocation, WaterSurfaceNormal, WaterVelocity };
}