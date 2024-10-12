// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WaterPhysicsTypes.h"
#include "WaterPhysicsCollisionInterface.generated.h"

struct FBodyInstance;

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UWaterPhysicsCollisionInterface : public UInterface
{
	GENERATED_BODY()
};

class WATERPHYSICS_API IWaterPhysicsCollisionInterface
{
	GENERATED_BODY()
public:
	// Fetch the transform in worldspace of this water physics collision.
	// NOTE: This function can be called during substepping, in which case you might need to call into the physics scene to get up to date transforms.
	virtual FTransform GetWaterPhysicsCollisionWorldTransform(const FName& BodyName) const = 0;

	// Generate the water physics collision setup in local space.
	virtual FWaterPhysicsCollisionSetup GenerateWaterPhysicsCollisionSetup(const FName& BodyName) const = 0;

	// Fetch the body instance used to apply the water physics forces related to this collision setup.
	// NOTE: For welding to work properly, it is important to return the non-welded body when bGetWelded = false
	virtual FBodyInstance* GetWaterPhysicsCollisionBodyInstance(const FName& BodyName, bool bGetWelded) const = 0;

	// Fetch all the physics body names associated with this water physics collision.
	virtual TArray<FName> GetAllBodyNames() const = 0;
};