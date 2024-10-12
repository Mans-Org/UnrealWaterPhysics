// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "Components/ActorComponent.h"
#include "WaterPhysicsTypes.h"
#include "WaterPhysicsSettingsComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnWaterPhysicsSettingsChanged, UWaterPhysicsSettingsComponent*);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActorAddedToWaterPhysics, UWaterPhysicsSceneComponent*, WaterPhysicsSceneComponent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActorRemovedFromWaterPhysics, UWaterPhysicsSceneComponent*, WaterPhysicsSceneComponent);

USTRUCT(BlueprintType)
struct FComponentsWaterPhysicsSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", 
		meta=(ShowComponentClasses="PrimitiveComponent,WaterPhysicsCollisionComponent", 
			  HideComponentClasses="ArrowComponent,PaperTerrainComponent,BillboardComponent,DrawFrustumComponent,LineBatchComponent,SplineComponent,TextRenderComponent,VectorFieldComponent,FXSystemComponent,FieldSystemComponent"))
	FActorComponentsSelection ActorComponentsSelection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", meta=(ShowOnlyInnerProperties))
	FWaterPhysicsSettings WaterPhysicsSettings;
};

USTRUCT(BlueprintType)
struct FGatherWaterPhysicsSettingsResult
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Gather Water Physics Settings")
	TMap<UActorComponent*, FWaterPhysicsSettings> ComponentsWaterPhysicsSettings;

	UPROPERTY(BlueprintReadOnly, Category = "Gather Water Physics Settings")
	TArray<UActorComponent*> BlacklistedComponents;

	// Empty if all components whitelisted
	UPROPERTY(BlueprintReadOnly, Category = "Gather Water Physics Settings")
	TArray<UActorComponent*> WhitelistedComponents;

	UPROPERTY(BlueprintReadOnly, Category = "Gather Water Physics Settings")
	UWaterPhysicsSettingsComponent* SettingsComponent = nullptr;
};

/* Use this component to set individual water physics settings for each component on an actor. */
UCLASS(HideCategories=("ComponentReplication","Collision","Activation"), ClassGroup=("Physics"), meta=(DisplayName="Water Physics Settings", BlueprintSpawnableComponent))
class WATERPHYSICS_API UWaterPhysicsSettingsComponent : public UActorComponent
{
	GENERATED_BODY()

protected:

	FOnWaterPhysicsSettingsChanged OnWaterPhysicsSettingsChanged;

public:
	
	/** 
	 * Water Physics Settings Stack
	 * 
	 * Settings are merged from top to bottom, meaning elements with a higher index in the array are given precedence.
	 * Example: 
	 * [0] - Selects All Components, changes FluidDensity
	 * [1] - Select One Component, change multiple settings
	 * 
	 * In this case all components would have changed FluidDensity, with the component selected in elem 1 having its settings layerd on top of the settings in elem 0
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings")
	TArray<FComponentsWaterPhysicsSettings> WaterPhysicsSettings;

	// Never add these components to water physics scene
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", 
		meta=(ShowComponentClasses="PrimitiveComponent,WaterPhysicsCollisionComponent", 
			  HideComponentClasses="ArrowComponent,PaperTerrainComponent,BillboardComponent,DrawFrustumComponent,LineBatchComponent,SplineComponent,TextRenderComponent,VectorFieldComponent,FXSystemComponent,FieldSystemComponent"))
	FActorComponentsSelection BlacklistComponents;

	// Only add these components to water physics scene
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Physics Settings", 
		meta=(ShowComponentClasses="PrimitiveComponent,WaterPhysicsCollisionComponent", 
			  HideComponentClasses="ArrowComponent,PaperTerrainComponent,BillboardComponent,DrawFrustumComponent,LineBatchComponent,SplineComponent,TextRenderComponent,VectorFieldComponent,FXSystemComponent,FieldSystemComponent"))
	FActorComponentsSelection WhitelistComponents;

	UPROPERTY(BlueprintAssignable, Category="Water Physics Events")
	FOnActorAddedToWaterPhysics OnActorAddedToWaterPhysics;

	UPROPERTY(BlueprintAssignable, Category="Water Physics Events")
	FOnActorRemovedFromWaterPhysics OnActorRemovedFromWaterPhysics;

public:

	UWaterPhysicsSettingsComponent();

	UFUNCTION(BlueprintCallable, Category="Water Physics Settings")
	void NotifyWaterPhysicsSettingsChanged();

	UFUNCTION(BlueprintCallable, Category="Water Physics Settings")
	static FGatherWaterPhysicsSettingsResult GatherActorWaterPhysicsSettings(AActor* Actor);

	FOnWaterPhysicsSettingsChanged& GetOnWaterPhysicsSettingsChanged() { return OnWaterPhysicsSettingsChanged; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
};