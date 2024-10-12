// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "Components/SceneComponent.h"
#include "WaterPhysicsCollisionInterface.h"
#include "WaterPhysicsCollisionComponent.generated.h"

UENUM()
enum class EWaterPhysicsCollisionType : uint8
{
	Mesh,
	MeshCollision,
	Box,
	Sphere,
	Capsule
};

/*
* This component will add additional collision for the water physics system (This collision is purely logical and will not create an actual collider).
* Needs to be attached to a PrimitiveComponent which is simulating physics (Or which is welded to a simulating component).
*/
UCLASS(HideCategories=("ComponentReplication","Collision","ComponentTick","Activation","Events","Physics","LOD"), ClassGroup=("Physics"), 
	meta=(DisplayName="Water Physics Collision", BlueprintSpawnableComponent))
class WATERPHYSICS_API UWaterPhysicsCollisionComponent : public USceneComponent, public IWaterPhysicsCollisionInterface
{
	GENERATED_BODY()

public:

	// The type of collision which should be used for this component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water Physics Collision")
	EWaterPhysicsCollisionType CollisionType;

	/**
	 *	The mesh asset used for Mesh/MeshCollision. 
	 *	When using Mesh option, the StaticMesh asset must enable "Allow CPU Access"
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water Physics Collision", meta=(EditCondition="CollisionType==EWaterPhysicsCollisionType::Mesh||CollisionType==EWaterPhysicsCollisionType::MeshCollision"))
	UStaticMesh* Mesh;

	// The LOD to use when sourcing the mesh used for water physics simulation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water Physics Collision", meta=(EditCondition="CollisionType==EWaterPhysicsCollisionType::Mesh"))
	int32 LOD;

	// The extents of the box (Used with collision type Box)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water Physics Collision", meta=(EditCondition="CollisionType==EWaterPhysicsCollisionType::Box"))
	FVector BoxExtent;

	// The radius of the sphere (Used with collision type Sphere)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water Physics Collision", meta=(EditCondition="CollisionType==EWaterPhysicsCollisionType::Sphere", ClampMin="0", UIMin="0"))
	float SphereRadius;

	/** 
	 *	Capsule Half-height, from center of capsule to the end of top or bottom hemisphere.
	 *	This cannot be less than CapsuleRadius.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water Physics Collision", meta=(EditCondition="CollisionType==EWaterPhysicsCollisionType::Capsule", ClampMin="0", UIMin="0"))
	float CapsuleHalfHeight;

	/** 
	 *	Radius of cap hemispheres and center cylinder.
	 *	This cannot be more than CapsuleHalfHeight.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water Physics Collision", meta=(EditCondition="CollisionType==EWaterPhysicsCollisionType::Capsule", ClampMin="0", UIMin="0"))
	float CapsuleRadius;

#if WITH_EDITORONLY_DATA
	// The line thickness to use during component visualization
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category="Rendering")
	float LineThickness;

	// The color to use during component visualization
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category="Rendering")
	FColor ShapeColor;

	// If this component should only be visible during "Show Collision"
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category="Rendering", meta=(EditCondition="bVisible"))
	bool bVisibleOnlyWithShowCollision;
#endif

public:

	UWaterPhysicsCollisionComponent();

#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif
	
	// ~Begin WaterPhysicsCollisionInterface
	virtual FTransform GetWaterPhysicsCollisionWorldTransform(const FName& BodyName) const override;
	virtual FWaterPhysicsCollisionSetup GenerateWaterPhysicsCollisionSetup(const FName& BodyName) const override;
	virtual FBodyInstance* GetWaterPhysicsCollisionBodyInstance(const FName& BodyName, bool bGetWelded) const override;
	virtual TArray<FName> GetAllBodyNames() const;
	// ~End WaterPhysicsCollisionInterface

};