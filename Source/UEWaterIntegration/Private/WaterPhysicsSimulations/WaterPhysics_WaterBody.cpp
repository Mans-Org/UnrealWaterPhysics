// Copyright Mans Isaksson 2021. All Rights Reserved.

#include "WaterPhysicsSimulations/WaterPhysics_WaterBody.h"
#include "WaterPhysicsSettingsComponent.h"
#include "WaterPhysicsSceneComponent.h"
#include "WaterPhysicsSimulations/WaterPhysics_WaterVolume.h"
#include "Components/BillboardComponent.h"
#include "WaterBodyActor.h"
#include "UObject/ConstructorHelpers.h"

int32 AWaterPhysics_WaterBody::GetWaterBodyPriority(AActor* InWaterBody) const
{
	return WaterBodies.IndexOfByPredicate([&](const FWaterBodySetup& X) { return X.WaterBody == InWaterBody; });
}

TArray<AActor*> AWaterPhysics_WaterBody::GetWaterBodies() const
{
	TArray<AActor*> OutActors;
	OutActors.Reserve(WaterBodies.Num());
	for (const FWaterBodySetup& WaterBodySetup : WaterBodies)
	{
		OutActors.Add(WaterBodySetup.WaterBody);
	}
	return OutActors;
}

FGetWaterInfoResult AWaterPhysics_WaterBody::CalculateWaterBodyWaterInfo(AActor* InWaterBody, const UActorComponent* Component, const FVector& Location) const
{
	check(IsValid(Component));
	AWaterBody* WaterBody = static_cast<AWaterBody*>(InWaterBody);

	EWaterBodyQueryFlags QueryFlags = EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::ComputeNormal;

	const FWaterBodySetup* WaterBodySetup = WaterBodies.FindByPredicate([&](const auto& X) { return X.WaterBody == WaterBody; });
	check(WaterBodySetup);

	if (WaterBodySetup->bIncludeWaves)
		QueryFlags |= EWaterBodyQueryFlags::IncludeWaves;
	if (WaterBodySetup->bIncludeWaves && WaterBodySetup->bUseSimpleWaves)
		QueryFlags |= EWaterBodyQueryFlags::SimpleWaves;
	if (WaterBodySetup->bIncludeVelocity)
		QueryFlags |= EWaterBodyQueryFlags::ComputeVelocity;

	const FWaterBodyQueryResult QueryResult = WaterBody->GetWaterBodyComponent()->QueryWaterInfoClosestToWorldLocation(Location, QueryFlags, TOptional<float>());

	return FGetWaterInfoResult{
		QueryResult.GetWaterSurfaceLocation(),
		QueryResult.GetWaterSurfaceNormal(),
		(int32)(QueryResult.GetQueryFlags() & EWaterBodyQueryFlags::ComputeVelocity) ? QueryResult.GetVelocity() : FVector::ZeroVector
	};
}