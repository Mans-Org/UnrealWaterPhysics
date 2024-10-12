// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "WaterPhysicsActor.h"
#include "WorldCollision.h"
#include "WaterPhysics_WaterVolume.generated.h"

UENUM(BlueprintType)
enum class EWaterVolumeOverlapMethod : uint8
{
	Overlap  UMETA(DisplayName="On Overlap Event"), // Use unreals built-in overlap events to detect overlapping physics bodes
	Trace    UMETA(DisplayName="Trace")             // Continiusly performs overlap traces to detect overlapping physics bodes
};

UCLASS(BlueprintType, meta=(DisplayName="Water Physics - Water Volume"))
class WATERPHYSICS_API AWaterPhysics_WaterVolume : public AWaterPhysicsActor
{
	GENERATED_BODY()

private:
	UPROPERTY(Transient)
	TSet<AActor*> OverlappingActors;

	UPROPERTY(Transient)
	TSet<AActor*> NewOverlappingActors;

	FOverlapDelegate OverlapDelegate;

private:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Water Volume", meta=(AllowPrivateAccess = "true"))
	class UBoxComponent* BoxComponent;

protected:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water Volume")
	EWaterVolumeOverlapMethod OverlapMethod;

public:

	AWaterPhysics_WaterVolume();

	UFUNCTION(BlueprintPure, Category="Water Volume")
	FORCEINLINE UBoxComponent* GetBoxComponent() const { return BoxComponent; }

	UFUNCTION(BlueprintCallable, Category="Water Volume")
	void SetOverlapMethod(EWaterVolumeOverlapMethod NewOverlapMethod, bool ResetOverlaps = false);

	void BeginPlay() override;

	void Tick(float DeltaTime) override;

	void OnFinishAsyncOverlap(const FTraceHandle& TraceHandle, FOverlapDatum& OverlapDatum);

	UFUNCTION()
	void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void UpdateOverlappedActors();

	FGetWaterInfoResult CalculateWaterInfo(const UActorComponent*, const FVector& Location) override;
};