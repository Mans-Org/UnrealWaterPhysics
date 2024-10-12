// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "Physics/PhysicsInterfaceDeclares.h"
#include "Components/ActorComponent.h"
#include "WaterPhysicsTypes.h"
#include "WaterPhysicsScene.h"
#include "WaterPhysicsSceneComponent.generated.h"

USTRUCT(BlueprintType)
struct FWaterPhysicsActingForces
{
	GENERATED_BODY()

	FWaterPhysicsActingForces() { FMemory::Memzero(*this); }

	explicit FWaterPhysicsActingForces(const FWaterPhysicsScene::FActingForces& InActingForces)
		: BuoyancyForce(InActingForces.BuoyancyForce)
		, BuoyancyTorque(InActingForces.BuoyancyTorque)
		, ViscousFluidResistanceForce(InActingForces.ViscousFluidResistanceForce)
		, ViscousFluidResistanceTorque(InActingForces.ViscousFluidResistanceTorque)
		, PressureDragForce(InActingForces.PressureDragForce)
		, PressureDragTorque(InActingForces.PressureDragTorque)
		, SlammingForce(InActingForces.SlammingForce)
		, SlammingTorque(InActingForces.SlammingTorque)
	{}

	UPROPERTY(BlueprintReadWrite, Category="Water Physics Acting Forces")
	FVector BuoyancyForce;

	UPROPERTY(BlueprintReadWrite, Category="Water Physics Acting Forces")
	FVector BuoyancyTorque;

	UPROPERTY(BlueprintReadWrite, Category="Water Physics Acting Forces")
	FVector ViscousFluidResistanceForce;

	UPROPERTY(BlueprintReadWrite, Category="Water Physics Acting Forces")
	FVector ViscousFluidResistanceTorque;

	UPROPERTY(BlueprintReadWrite, Category="Water Physics Acting Forces")
	FVector PressureDragForce;

	UPROPERTY(BlueprintReadWrite, Category="Water Physics Acting Forces")
	FVector PressureDragTorque;

	UPROPERTY(BlueprintReadWrite, Category="Water Physics Acting Forces")
	FVector SlammingForce;

	UPROPERTY(BlueprintReadWrite, Category="Water Physics Acting Forces")
	FVector SlammingTorque;

	FWaterPhysicsActingForces& operator+=(const FWaterPhysicsActingForces& Rhs)
	{
		BuoyancyForce                += Rhs.BuoyancyForce;
		BuoyancyTorque               += Rhs.BuoyancyTorque;
		ViscousFluidResistanceForce  += Rhs.ViscousFluidResistanceForce;
		ViscousFluidResistanceTorque += Rhs.ViscousFluidResistanceTorque;
		PressureDragForce            += Rhs.PressureDragForce;
		PressureDragTorque           += Rhs.PressureDragTorque;
		SlammingForce                += Rhs.SlammingForce;
		SlammingTorque               += Rhs.SlammingTorque;
		return *this;
	}
};

DECLARE_DYNAMIC_DELEGATE_RetVal_TwoParams(FGetWaterInfoResult, FBlueprintGetWaterInfoAtLocation, const UActorComponent*, Component, const FVector&, Location);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FK2_PreStepWaterPhysicsScene);
DECLARE_MULTICAST_DELEGATE(FPreStepWaterPhysicsScene);

/* Advanced: Use this component to create custom water physics implementations */
UCLASS(HideCategories=("ComponentReplication","Collision"), ClassGroup=("Physics"), meta=(DisplayName="Water Physics Scene", BlueprintSpawnableComponent))
class WATERPHYSICS_API UWaterPhysicsSceneComponent : public UActorComponent
{
	GENERATED_BODY()

protected:

	FWaterPhysicsScene WaterPhysicsScene;

	FGetWaterInfoAtLocation WaterInfoGetter;
	bool bWaterInfoGetterThreadSafe = false;

	TSharedPtr<FWaterSurfaceProvider> WaterSurfaceProvider;

public:
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", meta=(ShowOnlyInnerProperties))
	FWaterPhysicsSettings DefaultWaterPhysicsSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", AdvancedDisplay)
	bool bDrawWaterInfoDebug = false;

public:

	UPROPERTY(BlueprintAssignable, Category = "Water Physics Events", DisplayName="Pre Step Water Physics Scene")
	FK2_PreStepWaterPhysicsScene K2_PreStepWaterPhysicsScene;

	FPreStepWaterPhysicsScene PreStepWaterPhysicsScene;

public:

	UWaterPhysicsSceneComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/*
		Adds all components on the actor which can simulate water physics to the water physics simulation scene.
		WaterPhysicsSettings: Settings used for this actors components.
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	void AddActorToWaterPhysics(AActor* Actor, const FWaterPhysicsSettings& WaterPhysicsSettings);

	/*
		Removes all components on the actor to from water physics simulation scene.
		Returns true if any component was removed.
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	bool RemoveActorFromWaterPhysics(AActor* Actor);
	
	/*
		Overload of AddActorToWaterPhysics which takes an array of actors.
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	void AddActorsToWaterPhysics(const TArray<AActor*>& Actors, const FWaterPhysicsSettings& WaterPhysicsSettings);

	/*
		Overload of RemoveActorFromWaterPhysics which takes an array of actors.
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	bool RemoveActorsFromWaterPhysics(const TArray<AActor*>& Actors);

	/*
		Adds the components physics bodies to the water physics simulation scene.
		bAllBodies: Add all physics bodies on the component to the water physics simulation.
		BodyName: if bAllBodies is false, only add the specified body to the the water physics simulation.
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics", meta=(AdvancedDisplay="bAllBodies,BodyName"))
	void AddComponentToWaterPhysics(UActorComponent* Component, const FWaterPhysicsSettings& WaterPhysicsSettings, bool bAllBodies = true, FName BodyName = NAME_None);

	/*
		Removes the components physics bodies from the water physics simulation scene.
		bAllBodies: Remove all physics bodies on the component from the water physics simulation.
		BodyName: if bAllBodies is false, only remove the specified body from the the water physics simulation.
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics", meta=(AdvancedDisplay="bAllBodies,BodyName"))
	bool RemoveComponentFromWaterPhysics(UActorComponent* Component, bool bAllBodies = true, FName BodyName = NAME_None);

	/*
		Update the water physics settings on a components physics bodies.
		bAllBodies: Update the settings on all the components bodies.
		BodyName: if bAllBodies is false, only updated the settings on the specified body.
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics", meta=(AdvancedDisplay="bAllBodies,BodyName"))
	void SetComponentWaterPhysicsSettings(UActorComponent* Component, const FWaterPhysicsSettings& WaterPhysicsSettings, bool bAllBodies = true, FName BodyName = NAME_None);

	/*
		Checks if the water physics simulation contains any body associated with the component
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	bool ContainsComponent(UActorComponent* Component) const;

	/*
		Checks if the water physics simulation contains a specific body on the component
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	bool ContainsComponentBody(UActorComponent* Component, FName BodyName) const;

	/*
		Returns the current water physics forces currently acting on all the component bodies
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	FWaterPhysicsActingForces GetComponentActingWaterPhysicsForces(UActorComponent* Component) const;

	/*
		Returns the current water physics forces currently acting on the component body
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	FWaterPhysicsActingForces GetComponentBodyActingWaterPhysicsForces(UActorComponent* Component, FName BodyName) const;

	/*
		Returns the total submerged area of this component
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	float GetComponentSubmergedArea(UActorComponent* Component) const;

	/*
		Returns the total submerged area of this component body
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics")
	float GetComponentBodySubmergedArea(UActorComponent* Component, FName BodyName) const;

	/*
		WARNING: Implementing this function in Blueprint is highly discouraged due to the high potential performance impact.

		Set the delegate used to calculate the water-surface.
		bThreadSafe: Is this surface getter safe to call outside of GameThread?
	*/
	UFUNCTION(BlueprintCallable, Category="Water Physics", meta=(DisplayName="Set Water Info Getter"))
	void K2_SetWaterInfoGetter(const FBlueprintGetWaterInfoAtLocation& InWaterInfoGetter, bool bThreadSafe);

	/*
		Set the delegate used to calculate the water-surface.
		bThreadSafe: Is this surface getter safe to call outside of GameThread?
	*/
	void SetWaterInfoGetter(const FGetWaterInfoAtLocation& InWaterInfoGetter, bool bThreadSafe);

	/*
		Set the WaterSurfaceProvided to call when using the WaterSurfaceProvider option to resolve the water surface for this scene.
	*/
	void SetWaterSurfaceProvider(const TSharedPtr<FWaterSurfaceProvider>& NewWaterSurfaceProvider);

	/*
		Sets whether the currently set WaterInfoGetter is safe to call outside of GameThread.
	*/
	void SetWaterInfoGetterThreadSafe(bool bThreadSafe);

protected:

	void PreStepWaterPhysics(FPhysScene* PhysScene, float DeltaTime);
	void StepWaterPhysics(FPhysScene* PhysScene, float DeltaTime);

	virtual TSharedPtr<FWaterSurfaceProvider> MakeWaterSurfaceProvider() const;

};