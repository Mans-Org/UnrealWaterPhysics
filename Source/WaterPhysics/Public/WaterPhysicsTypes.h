// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterPhysicsTypes.generated.h"

#ifndef WITH_WATER_PHYS_DEBUG
#define WITH_WATER_PHYS_DEBUG 0
#endif

USTRUCT(BlueprintType)
struct FGetWaterInfoResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Water Info")
	FVector WaterSurfaceLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category="Water Info")
	FVector WaterSurfaceNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category="Water Info")
	FVector WaterVelocity = FVector::ZeroVector;
};
DECLARE_DELEGATE_RetVal_TwoParams(FGetWaterInfoResult, FGetWaterInfoAtLocation, const UActorComponent*, const FVector&)

UENUM()
enum class EWaterInfoFetchingMethod : uint8
{
	// Uses the Water Surface Provider to fetch water surface information. Uses the WorldAlignedWaterSurfaceProvider by default.
	WaterSurfaceProvider,
	// Force fetch the water surface once per vertex. Might get expensive if object has a high number of vertices.
	PerVertex,
	// Only fetch the water surface once per object. Will greatly improve performance at the cost of wave accuracy.
	PerObject
};

UENUM()
enum class EWaterPhysicsDebugLevel : uint8
{
	None    = 0,
	Normal  = 1,
	Verbose = 2
};

UENUM()
enum class EWaterPhysicsTessellationMode : uint8
{
	Levels = 0,
	Area   = 1
};

USTRUCT(BlueprintType)
struct FTessellationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tessellation Settings")
	EWaterPhysicsTessellationMode TessellationMode = EWaterPhysicsTessellationMode::Levels;

	// Minimum area (m^2) to subdivide the triangle to
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tessellation Settings", meta=(UIMin="0.1", UIMax="5", ClampMin="0.01"))
	float MaxArea = 1;

	// Nr of times to subdived all triangles
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tessellation Settings", meta=(UIMin="0", UIMax="5", ClampMin="0"))
	int32 Levels = 1;
};

USTRUCT(BlueprintType)
struct WATERPHYSICS_API FTriangleSubdivisionSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Subdivision Settings", meta=(UIMin="0", UIMax="5", ClampMin="0"))
	int32 Box     = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Subdivision Settings", meta=(UIMin="0", UIMax="5", ClampMin="0"))
	int32 Convex  = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Subdivision Settings", meta=(UIMin="0", UIMax="5", ClampMin="0"))
	int32 Sphere  = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Subdivision Settings", meta=(UIMin="0", UIMax="5", ClampMin="0"))
	int32 Capsule = 0;
};

USTRUCT(BlueprintType)
struct WATERPHYSICS_API FWaterPhysicsSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_FluidDensity:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_FluidKinematicViscocity:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_WaterInfoFetchingMethod:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SubdivisionSettings:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SubmergedTessellationSettings:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_PressureCoefficientOfLinearSpeed:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_PressureCoefficientOfExponentialSpeed:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_PressureAngularDependence:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SuctionCoefficientOfLinearSpeed:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SuctionCoefficientOfExponentialSpeed:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SuctionAngularDependence:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DragReferenceSpeed:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_MaxSlammingForceAtAcceleration:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SlammingForceExponent:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bEnableBuoyancyForce:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bEnableViscousFluidResistance:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bEnablePressureDragForce:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bEnableSlammingForce:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bEnableForceClamping:1;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DebugSubmersion:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DebugTriangleData:1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DebugBuoyancyForce:1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DebugViscousFluidResistance:1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DebugPressureDragForce:1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DebugSlammingForce:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DebugFluidVelocity:1;

	FWaterPhysicsSettings()
		: bOverride_FluidDensity(0)
		, bOverride_FluidKinematicViscocity(0)
		, bOverride_WaterInfoFetchingMethod(0)
		, bOverride_SubdivisionSettings(0)
		, bOverride_SubmergedTessellationSettings(0)
		, bOverride_PressureCoefficientOfLinearSpeed(0)
		, bOverride_PressureCoefficientOfExponentialSpeed(0)
		, bOverride_PressureAngularDependence(0)
		, bOverride_SuctionCoefficientOfLinearSpeed(0)
		, bOverride_SuctionCoefficientOfExponentialSpeed(0)
		, bOverride_SuctionAngularDependence(0)
		, bOverride_DragReferenceSpeed(0)
		, bOverride_MaxSlammingForceAtAcceleration(0)
		, bOverride_SlammingForceExponent(0)
		, bOverride_bEnableBuoyancyForce(0)
		, bOverride_bEnableViscousFluidResistance(0)
		, bOverride_bEnablePressureDragForce(0)
		, bOverride_bEnableSlammingForce(0)
		, bOverride_bEnableForceClamping(0)
		, bOverride_DebugSubmersion(0)
		, bOverride_DebugTriangleData(0)
		, bOverride_DebugBuoyancyForce(0)
		, bOverride_DebugViscousFluidResistance(0)
		, bOverride_DebugPressureDragForce(0)
		, bOverride_DebugSlammingForce(0)
		, bOverride_DebugFluidVelocity(0)
	{

	}

	/* 
		Fluid Density measured in kg/m3

		Primarily affects strength of buoyancy.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", meta=(EditCondition = "bOverride_FluidDensity", UIMin="0.001", UIMax="10000", ClampMin="0.001"))
	float FluidDensity = 997.f;

	/* 
		Kinematic Viscocity measured in centistokes (cSt)

		Affects the strength of the viscous fluid resistance. Think honey (2000-3000 cSt) vs water (~1 cSt).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", meta=(EditCondition = "bOverride_FluidKinematicViscocity", UIMin="0.00001", UIMax="10", ClampMin="0.00001"))
	float FluidKinematicViscocity = 1.0023f;

	/*
		Water Info Fetching Method

		Determines how the water surface information will be fetched. By default this uses a water surface fetching algorithm which is 
		optimized for precision and speed. If your objects do not require an accurate water surface you could the "PerObject" option 
		which only fetch the water surface once per object. This will greatly improve performance at the cost of wave accuracy.	The per vertex 
		option is not recommended unless you know what you are doing.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", meta=(EditCondition = "bOverride_WaterInfoFetchingMethod"))
	EWaterInfoFetchingMethod WaterInfoFetchingMethod = EWaterInfoFetchingMethod::WaterSurfaceProvider;

	/*
		Subdivision Settings

		When generating the underlying triangulated mesh used for water physics calculations, how many times should the triangles for each
		collider primitive type be split. A higher value can improve simulation stability at the cost of some performance.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", meta=(EditCondition = "bOverride_SubdivisionSettings"))
	FTriangleSubdivisionSettings SubdivisionSettings = { 0, 0, 1, 1 };

	/*
		Submerged Tessellation Settings

		How to tessellate the submerged triangles. Increasing this number will improve the accuracy of the calculations at the cost of some performance.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", meta=(EditCondition = "bOverride_SubmergedTessellationSettings"))
	FTessellationSettings SubmergedTessellationSettings;

	/* 
		Pressure Coefficient Of Linear Speed

		Controls the linear drag component of the pressure-drag equation.
		Example: PressureCoefficientOfLinearSpeed * Speed + PressureCoefficientOfExponentialSpeed * Pow(Speed, 2)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Drag", meta=(EditCondition = "bOverride_PressureCoefficientOfLinearSpeed"))
	float PressureCoefficientOfLinearSpeed = 2000.f;

	/* 
		Pressure Coefficient Of Exponential Speed

		Controls the exponential drag component of the pressure-drag equation.
		Example: PressureCoefficientOfLinearSpeed * Speed + PressureCoefficientOfExponentialSpeed * Pow(Speed, 2)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Drag", meta=(EditCondition = "bOverride_PressureCoefficientOfExponentialSpeed"))
	float PressureCoefficientOfExponentialSpeed = 100.f;

	/* 
		Pressure Angular Dependence

		The falloff rate of the pressure force in relation to force angle.
		Example: Force *= Pow(ForceAngle, PressureAngularDependence)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Drag", meta=(EditCondition = "bOverride_PressureAngularDependence", UIMin="0.0", UIMax="1.0", ClampMin="0.0", ClampMax="1.0"))
	float PressureAngularDependence = 0.5f;

	
	/* 
		Suction Coefficient Of Linear Speed

		Controls the linear drag component of the suction-drag equation.
		Example: SuctionCoefficientOfLinearSpeed * Speed + SuctionCoefficientOfExponentialSpeed * Pow(Speed, 2)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Drag", meta=(EditCondition = "bOverride_SuctionCoefficientOfLinearSpeed"))
	float SuctionCoefficientOfLinearSpeed = 2000.f;

	/* 
		Suction Coefficient Of Exponential Speed

		Controls the exponential drag component of the suction-drag equation.
		Example: SuctionCoefficientOfLinearSpeed * Speed + SuctionCoefficientOfExponentialSpeed * Pow(Speed, 2)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Drag", meta=(EditCondition = "bOverride_SuctionCoefficientOfExponentialSpeed"))
	float SuctionCoefficientOfExponentialSpeed = 100.f;

	/* 
		Suction Angular Dependence

		The falloff rate of the suction force in relation to force angle.
		Example: Force *= Pow(ForceAngle, SuctionAngularDependence)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Drag", meta=(EditCondition = "bOverride_SuctionAngularDependence", UIMin="0.0", UIMax="1.0", ClampMin="0.0", ClampMax="1.0"))
	float SuctionAngularDependence = 0.5f;

	/* 
		Drag Reference Speed

		The speed at which exponential component of the pressure/suction drag equation will begin to increase faster than the linear component.
		In layman terms: The speed at which the drag will start to increase exponentially.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Drag", meta=(EditCondition = "bOverride_DragReferenceSpeed", Units="m/s", UIMin="0.001", ClampMin="0.001", UIMax="1000.0"))
	float DragReferenceSpeed = 5.f;

	/* 
		Max Slamming Force At Acceleration

		The acceleration at which the body would stop instantly if accelerating at, or faster, than this value into the water.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Slamming", meta=(EditCondition = "bOverride_MaxSlammingForceAtAcceleration", Units="m/s", UIMin="0.001", ClampMin="0.001", UIMax="1000.0"))
	float MaxSlammingForceAtAcceleration = 20.f;

	/* 
		Slamming Force Exponent

		Exponent of the slamming force gradient e.g. Pow(amount of slamming force(0 - 1), SlammingForceExponent).
		A lower exponent will increase the "stiffness" of the slamming force, making it skipp more on the surface of the water.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Slamming", meta=(EditCondition = "bOverride_SlammingForceExponent", UIMin="0.001", ClampMin="0.001", UIMax="10.0"))
	float SlammingForceExponent = 2.f;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Forces", meta=(EditCondition = "bOverride_bEnableBuoyancyForce"))
	bool bEnableBuoyancyForce = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Forces", meta=(EditCondition = "bOverride_bEnableViscousFluidResistance"))
	bool bEnableViscousFluidResistance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Forces", meta=(EditCondition = "bOverride_bEnablePressureDragForce"))
	bool bEnablePressureDragForce = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Forces", meta=(EditCondition = "bOverride_bEnableSlammingForce"))
	bool bEnableSlammingForce = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Forces", meta=(EditCondition = "bOverride_bEnableForceClamping"))
	bool bEnableForceClamping = false;

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", AdvancedDisplay, meta=(EditCondition = "bOverride_DebugSubmersion"))
	EWaterPhysicsDebugLevel DebugSubmersion = EWaterPhysicsDebugLevel::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", AdvancedDisplay, meta=(EditCondition = "bOverride_DebugTriangleData"))
	EWaterPhysicsDebugLevel DebugTriangleData = EWaterPhysicsDebugLevel::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", AdvancedDisplay, meta=(EditCondition = "bOverride_DebugBuoyancyForce"))
	EWaterPhysicsDebugLevel DebugBuoyancyForce = EWaterPhysicsDebugLevel::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", AdvancedDisplay, meta=(EditCondition = "bOverride_DebugViscousFluidResistance"))
	EWaterPhysicsDebugLevel DebugViscousFluidResistance = EWaterPhysicsDebugLevel::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", AdvancedDisplay, meta=(EditCondition = "bOverride_DebugPressureDragForce"))
	EWaterPhysicsDebugLevel DebugPressureDragForce = EWaterPhysicsDebugLevel::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", AdvancedDisplay, meta=(EditCondition = "bOverride_DebugSlammingForce"))
	EWaterPhysicsDebugLevel DebugSlammingForce = EWaterPhysicsDebugLevel::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", AdvancedDisplay, meta=(EditCondition = "bOverride_DebugFluidVelocity"))
	EWaterPhysicsDebugLevel DebugFluidVelocity = EWaterPhysicsDebugLevel::None;

	static FWaterPhysicsSettings MergeWaterPhysicsSettings(const FWaterPhysicsSettings& DefaultSettings, const FWaterPhysicsSettings& OverrideSettings);
};

USTRUCT(BlueprintType)
struct WATERPHYSICS_API FActorComponentsSelection
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	bool bSelectAll = true;

	UPROPERTY()
	TArray<FName> ComponentNames;

	TArray<UActorComponent*> GetComponents(AActor* SearchActor, const TArray<UClass*>& IncludeComponentClasses, const TArray<UClass*>& ExcludeComponentClasses) const;
};

UENUM(BlueprintType)
enum class EWaterPhysicsFilterOperation : uint8
{
	And,
	Or
};

UENUM(BlueprintType)
enum class EWaterPhysicsFilterType : uint8
{
	Tag            = 0, // Filter based on actor tag
	ActorClass     = 1, // Filter based on actor class
	ComponentClass = 2  // Filter based on components attached to the actor
};

USTRUCT(BlueprintType)
struct FWaterPhysicsFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters")
	bool Not = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters")
	EWaterPhysicsFilterOperation FilterOperation = EWaterPhysicsFilterOperation::And;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters")
	EWaterPhysicsFilterType FilterType = EWaterPhysicsFilterType::Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters")
	FName Tag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters")
	TSubclassOf<AActor> ActorsClass = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters")
	TSubclassOf<UActorComponent> ComponentClass = nullptr;

	// Returns true if the actor satisfies this filter
	bool ProcessFilter(AActor* Actor) const;

	// Evaluates a list of filters, grouping AND/OR as follows: (A & B & C) | (D & E)
	// Returns true if actor satisfies any group of AND filters.
	static bool ProcessFilterList(AActor* Actor, const TArray<FWaterPhysicsFilter>& FilterList);
};

namespace WaterPhysics
{
	static constexpr int32 InlineAllocSize() { return 64; }

	typedef TArray<FVector, TInlineAllocator<InlineAllocSize()>>   FVertexList;
	typedef TArray<int32,   TInlineAllocator<InlineAllocSize()*3>> FIndexList;

	struct FIndexedTriangleMesh
	{
		FVertexList VertexList;
		FIndexList  IndexList;
	};
};

struct FWaterPhysicsCollisionSetup
{
	struct FSphereElem
	{
		FVector Center;
		float   Radius;
	};
	TArray<FSphereElem> SphereElems;

	struct FBoxElem
	{
		FVector  Center;
		FRotator Rotation;
		FVector  Extent;
	};
	TArray<FBoxElem> BoxElems;

	struct FSphylElem
	{
		FVector  Center;
		FRotator Rotation;
		float    Radius;
		float    HalfHeight;
	};
	TArray<FSphylElem> SphylElems;

	typedef WaterPhysics::FIndexedTriangleMesh FMeshElem;
	TArray<FMeshElem> MeshElems;

	FORCEINLINE int32 NumCollisionElems() const { return SphereElems.Num() + BoxElems.Num() + SphylElems.Num() + MeshElems.Num(); }
};