// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsSimulations/WaterPhysics_WaterVolume.h"
#include "WaterPhysicsSettingsComponent.h"
#include "WaterPhysicsSceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

AWaterPhysics_WaterVolume::AWaterPhysics_WaterVolume()
{
	PrimaryActorTick.bCanEverTick = true;

	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxComponent"));
	BoxComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxComponent->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);
	BoxComponent->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
	BoxComponent->SetGenerateOverlapEvents(false);
	BoxComponent->OnComponentBeginOverlap.AddUniqueDynamic(this, &AWaterPhysics_WaterVolume::OnVolumeBeginOverlap);
	BoxComponent->OnComponentEndOverlap.AddUniqueDynamic(this, &AWaterPhysics_WaterVolume::OnVolumeEndOverlap);

	SetRootComponent(BoxComponent);

#if WITH_EDITORONLY_DATA
	ConstructorHelpers::FObjectFinder<UTexture2D> BillboardIconFinder(TEXT("/WaterPhysics/Icons/WaterPhysics"));
	if (UBillboardComponent* BillboardComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("BillboardComponent"), true))
	{
		BillboardComponent->SetSprite(BillboardIconFinder.Object);
		BillboardComponent->bIsScreenSizeScaled = true;
		BillboardComponent->SetupAttachment(BoxComponent);
	}
	SpriteScale = 2.f;
#endif

	OverlapDelegate = FOverlapDelegate::CreateUObject(this, &AWaterPhysics_WaterVolume::OnFinishAsyncOverlap);

	WaterPhysicsSceneComponent->SetWaterInfoGetterThreadSafe(true);
}

void AWaterPhysics_WaterVolume::SetOverlapMethod(EWaterVolumeOverlapMethod NewOverlapMethod, bool ResetOverlaps)
{
	OverlapMethod = NewOverlapMethod;
	BoxComponent->SetGenerateOverlapEvents(OverlapMethod == EWaterVolumeOverlapMethod::Overlap);
	SetActorTickEnabled(OverlapMethod == EWaterVolumeOverlapMethod::Trace);

	if (ResetOverlaps)
	{
		NewOverlappingActors.Empty(NewOverlappingActors.Num());
		UpdateOverlappedActors();
	}
}

void AWaterPhysics_WaterVolume::BeginPlay()
{
	Super::BeginPlay();

	SetOverlapMethod(OverlapMethod);

	// Initialize any already overlapping actors, since unreal does not call OnComponentBeginOverlap on already overlapping components/actors
	if (OverlapMethod == EWaterVolumeOverlapMethod::Overlap)
	{
		BoxComponent->UpdateOverlaps();
		BoxComponent->GetOverlappingActors(NewOverlappingActors);
		UpdateOverlappedActors();
	}
}

void AWaterPhysics_WaterVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TRACE_CPUPROFILER_EVENT_SCOPE(TickWaterPhysics);

	if (OverlapMethod == EWaterVolumeOverlapMethod::Overlap)
		return;

	FComponentQueryParams QueryParams = FComponentQueryParams::DefaultComponentQueryParams;
	{
		QueryParams.OwnerTag = GetFName();
		QueryParams.AddIgnoredActor(this);
		QueryParams.bTraceComplex = false;
	}

	FCollisionObjectQueryParams ObjectQueryParams = FCollisionObjectQueryParams::DefaultObjectQueryParam;

	GetWorld()->AsyncOverlapByObjectType(
		BoxComponent->GetComponentLocation(),
		BoxComponent->GetComponentQuat(),
		ObjectQueryParams,
		FCollisionShape::MakeBox(BoxComponent->GetScaledBoxExtent()),
		QueryParams,
		&OverlapDelegate
	);
}

void AWaterPhysics_WaterVolume::OnFinishAsyncOverlap(const FTraceHandle& TraceHandle, FOverlapDatum& OverlapDatum)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(OnFinishAsyncOverlap)

	NewOverlappingActors.Empty(OverlapDatum.OutOverlaps.Num());
	for (const FOverlapResult& OverlapResult : OverlapDatum.OutOverlaps)
	{
		AActor* OverlappedActor = OverlapResult.GetActor();
		if (OverlappedActor)
			NewOverlappingActors.Add(OverlappedActor);
	}

	UpdateOverlappedActors();
}

void AWaterPhysics_WaterVolume::OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OverlapMethod == EWaterVolumeOverlapMethod::Overlap)
	{
		NewOverlappingActors.Add(OtherActor);
		UpdateOverlappedActors();
	}
}

void AWaterPhysics_WaterVolume::OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OverlapMethod == EWaterVolumeOverlapMethod::Overlap)
	{
		NewOverlappingActors.Remove(OtherActor);
		UpdateOverlappedActors();
	}
}

void AWaterPhysics_WaterVolume::UpdateOverlappedActors()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateOverlappedActors)

	const TSet<AActor*> AddedActors   = NewOverlappingActors.Difference(OverlappingActors);
	const TSet<AActor*> RemovedActors = OverlappingActors.Difference(NewOverlappingActors);

	// Add new primitives to water physics scene
	for (AActor* Actor : AddedActors)
		AddActorToWater(Actor);

	for (AActor* Actor : RemovedActors)
		RemoveActorFromWater(Actor, 1.f); // Remove delay to avoid the body getting removed when "skipping" on the surface

	OverlappingActors = NewOverlappingActors;
}

FGetWaterInfoResult AWaterPhysics_WaterVolume::CalculateWaterInfo(const UActorComponent* Component, const FVector& Location)
{
	FVector RelativeLocation = BoxComponent->GetComponentTransform().InverseTransformPosition(Location);
	RelativeLocation.Z = BoxComponent->GetUnscaledBoxExtent().Z;
	return FGetWaterInfoResult
	{
		BoxComponent->GetComponentTransform().TransformPosition(RelativeLocation),
		BoxComponent->GetComponentQuat().GetUpVector(),
		FVector::ZeroVector
	};
}