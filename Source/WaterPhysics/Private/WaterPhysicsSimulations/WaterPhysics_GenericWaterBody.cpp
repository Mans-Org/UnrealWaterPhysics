// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsSimulations/WaterPhysics_GenericWaterBody.h"

int32 AWaterPhysics_GenericWaterBody::GetWaterBodyPriority(AActor* InWaterBody) const
{
	return WaterBodies.IndexOfByPredicate([&](AActor* X) { return X == InWaterBody; });
}

TArray<AActor*> AWaterPhysics_GenericWaterBody::GetWaterBodies() const
{
	return WaterBodies;
}

FGetWaterInfoResult AWaterPhysics_GenericWaterBody::CalculateWaterBodyWaterInfo(AActor* WaterBody, const UActorComponent* Component, const FVector& Location) const
{
	FVector RelativeLocation = WaterBody->GetActorTransform().InverseTransformPosition(Location);
	RelativeLocation.Z = 0;

	return FGetWaterInfoResult {
		WaterBody->GetActorTransform().TransformPosition(RelativeLocation),
		WaterBody->GetActorUpVector(),
		FVector::ZeroVector,
	};
}