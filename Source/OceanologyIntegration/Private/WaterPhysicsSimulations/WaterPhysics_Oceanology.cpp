// Copyright Mans Isaksson 2021. All Rights Reserved.

#include "WaterPhysicsSimulations/WaterPhysics_Oceanology.h"
#include "WaterPhysicsSettingsComponent.h"
#include "WaterPhysicsSceneComponent.h"
#include "WaterPhysicsCompatibilityLayer.h"
#include "OceanologyIntegrationModule.h"
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/UObjectToken.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "UObject/Package.h"

namespace Oceanology
{
	const static TCHAR* OceanologyWaterSurfaceClassName = TEXT("/Oceanology_Plugin/Advanced/Blueprints/Oceanology/Mode/BP_Oceanology_Infinity.BP_Oceanology_Infinity_C");
	const static TCHAR* OceanologyWaterSurfaceClassName_5_1 = TEXT("/Oceanology_Plugin/Design/Ocean/Blueprints/Ocean/Oceanology.Oceanology_C");
	const static TCHAR* OceanologyWaterSurfaceClassName_5_1_7 = TEXT("/Script/Oceanology_Plugin.OceanologyWaterParent");
	const static TCHAR* OceanologyGetWaveHeightFunctionName_4 = TEXT("Get Wave Height");
	const static TCHAR* OceanologyGetWaveHeightFunctionName_5_1_7 = TEXT("GetWaveHeightAtLocation");
	
	const TCHAR* GetOceanologyWaterSurfaceClassName()
	{
		if (OceanologyIntegrationModule::OceanologyMarjorVersion <= 4 
			|| (OceanologyIntegrationModule::OceanologyMarjorVersion == 5 && OceanologyIntegrationModule::OceanologyMinorVersion < 1))
		{
			return OceanologyWaterSurfaceClassName;
		}

		if (OceanologyIntegrationModule::OceanologyMarjorVersion == 5 
			&& OceanologyIntegrationModule::OceanologyMinorVersion == 1 
			&& OceanologyIntegrationModule::OceanologyPatchVersion < 7)
		{
			return OceanologyWaterSurfaceClassName_5_1;
		}

		return OceanologyWaterSurfaceClassName_5_1_7;
	}

	const TCHAR* OceanologyGetWaveHeightFunctionName()
	{
		if (OceanologyIntegrationModule::OceanologyMarjorVersion <= 4 
			|| (OceanologyIntegrationModule::OceanologyMarjorVersion == 5 && OceanologyIntegrationModule::OceanologyMinorVersion < 1))
		{
			return OceanologyGetWaveHeightFunctionName_4;
		}

		if (OceanologyIntegrationModule::OceanologyMarjorVersion == 5 
			&& OceanologyIntegrationModule::OceanologyMinorVersion == 1 
			&& OceanologyIntegrationModule::OceanologyPatchVersion < 7)
		{
			return OceanologyGetWaveHeightFunctionName_4;
		}

		return OceanologyGetWaveHeightFunctionName_5_1_7;
	}

	UClass* GetOceanologyWaterSurfaceClass()
	{
		return LoadClass<UObject>(nullptr, GetOceanologyWaterSurfaceClassName());
	}

	UFunction* GetWaveHeightFunction()
	{
		if (UClass* OceanologyMasterClass = GetOceanologyWaterSurfaceClass())
			return OceanologyMasterClass->FindFunctionByName(OceanologyGetWaveHeightFunctionName(), EIncludeSuperFlag::IncludeSuper);

		return nullptr;
	}

	bool GetSupportsParallelWaterHeightFetching()
	{
		return OceanologyIntegrationModule::OceanologyMarjorVersion > 5
			|| (OceanologyIntegrationModule::OceanologyMarjorVersion == 5 && OceanologyIntegrationModule::OceanologyMinorVersion > 1)
			|| (OceanologyIntegrationModule::OceanologyMarjorVersion == 5 && OceanologyIntegrationModule::OceanologyMinorVersion == 1 && OceanologyIntegrationModule::OceanologyPatchVersion > 6);
	}
};

FOceanologyThreadCopy::~FOceanologyThreadCopy()
{
	DestroyThreadCopy();
}

void FOceanologyThreadCopy::SyncWithMaster(AActor* MasterOceanologyActor)
{
	if (!IsValid(MasterOceanologyActor))
	{
		DestroyThreadCopy();
		return;
	}

	if (!IsValid(ThreadCopy) || MasterOceanologyActor->GetClass() != ThreadCopy->GetClass())
	{
		DestroyThreadCopy();

		const FString ActorCopyName = FString::Printf(TEXT("%s_ThreadCopy_%d"), *MasterOceanologyActor->GetName(), FPlatformTLS::GetCurrentThreadId());
		ThreadCopy = NewObject<AActor>(MasterOceanologyActor->GetLevel(), MasterOceanologyActor->GetClass(), *ActorCopyName, EObjectFlags::RF_Transient);
	}

	if (PropertiesToCopy.Num() == 0)
	{
		const static TSet<FName> PropertyNamesToCopy =
		{
			FName(TEXT("Max_Waves")),
			FName(TEXT("BaseOffset")),
			FName(TEXT("GlobalDisplacement")),
			FName(TEXT("\u03A31")),
			FName(TEXT("\u03A32")),
			FName(TEXT("\u03A33")),
			FName(TEXT("\u03A34"))
		};

		for (FProperty* Property = ThreadCopy->GetClass()->PropertyLink; Property; Property = Property->PropertyLinkNext)
		{
			if (PropertyNamesToCopy.Contains(Property->GetFName()))
			{
				PropertiesToCopy.Add(Property);
				if (PropertiesToCopy.Num() == PropertyNamesToCopy.Num())
					break;
			}
		}

		ensure(PropertiesToCopy.Num() == PropertyNamesToCopy.Num());
	}

	// Copy relevant properties
	{
		for (FProperty* Property : PropertiesToCopy)
			Property->CopyCompleteValue_InContainer(ThreadCopy, MasterOceanologyActor);

		// Needed for GetActorLocation to work.
		ThreadCopy->GetRootComponent()->SetWorldTransform(MasterOceanologyActor->GetRootComponent()->GetComponentTransform());
	}
}

void FOceanologyThreadCopy::DestroyThreadCopy()
{
	if (ThreadCopy)
	{
		// Remove the level as our outer, required for post PIE cleanup not to break.
		// NOTE: We call the UObject::Rename, bypassing the AActor::Rename as that interacts with the level, not knowing our actor is not
		// actually placed in the level.
		((*ThreadCopy).*(&UObject::Rename))(nullptr, GetTransientPackage(), REN_None); 

		WaterPhysicsCompat::MarkObjectPendingKill(ThreadCopy->GetRootComponent()); // Destroy the component generated by AQuadTree
		WaterPhysicsCompat::MarkObjectPendingKill(ThreadCopy); // Destroy the actor object
	}

	ThreadCopy = nullptr;

	PropertiesToCopy.Empty();
}

AWaterPhysics_Oceanology::AWaterPhysics_Oceanology()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(FName(TEXT("Root Component"))));

#if WITH_EDITORONLY_DATA
	ConstructorHelpers::FObjectFinder<UTexture2D> BillboardIconFinder(TEXT("/WaterPhysics/Icons/WaterPhysics"));
	if (UBillboardComponent* BillboardComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("BillboardComponent"), true))
	{
		BillboardComponent->SetSprite(BillboardIconFinder.Object);
		BillboardComponent->SetupAttachment(RootComponent);
		BillboardComponent->bIsScreenSizeScaled = true;
	}
	SpriteScale = 2.f;
#endif

	WaterPhysicsSceneComponent->SetWaterInfoGetterThreadSafe(true);
}

void AWaterPhysics_Oceanology::BeginPlay()
{
	Super::BeginPlay();

	for (AActor* Actor : InitiallySimulatedActors)
		AddActorToWater(Actor);

	for (AActor* OceanBoundsActor : OceanBoundsActors)
	{
		if (IsValid(OceanBoundsActor))
		{
			OceanBoundsActor->OnActorBeginOverlap.AddDynamic(this, &AWaterPhysics_Oceanology::OnActorBeginOverlapBoundsActor);
			OceanBoundsActor->OnActorEndOverlap.AddDynamic(this, &AWaterPhysics_Oceanology::OnActorEndOverlapBoundsActor);

			TWeakObjectPtr<AActor> WeakOceanBoundsActor = OceanBoundsActor;
			FTimerHandle TimerHandle;
			GetWorld()->GetTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakOceanBoundsActor]()
			{
				AActor* OceanBoundsActor = WeakOceanBoundsActor.Get();
				if (!IsValid(OceanBoundsActor))
					return;

				TArray<AActor*> OverlappingActors;
				OceanBoundsActor->UpdateOverlaps(false);
				OceanBoundsActor->GetOverlappingActors(OverlappingActors);
				for (AActor* Actor : OverlappingActors)
					OnActorBeginOverlapBoundsActor(OceanBoundsActor, Actor);
			}), 0.01f, false);
		}
	}

	GetWaveHightFunction = Oceanology::GetWaveHeightFunction();
	ensureMsgf(IsValid(GetWaveHightFunction), TEXT("Unable to find the Get Wave Hight function on the oceanology actor"));

	bSupportsParallelWaterHeightFetching = Oceanology::GetSupportsParallelWaterHeightFetching();
}

void AWaterPhysics_Oceanology::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	for (auto &ThreadCopy : OceanologyThreadCopies)
		ThreadCopy.Value.DestroyThreadCopy();

	OceanologyThreadCopies.Empty();

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void AWaterPhysics_Oceanology::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AWaterPhysics_Oceanology, OceanologyWater))
	{
		if (OceanologyWater != nullptr && !OceanologyWater->IsA(Oceanology::GetOceanologyWaterSurfaceClass()))
		{
			const static FText ErrMsgPt1 = NSLOCTEXT("WaterPhysicsOceanology", "PostEditChangePropertyStart", "Actor");
			const static FText ErrMsgPt2 = NSLOCTEXT("WaterPhysicsOceanology", "PostEditChangePropertyEnd", "is not an Oceanology water surface.");

			FMessageLog("Blueprint").Warning()
				->AddToken(FTextToken::Create(ErrMsgPt1))
				->AddToken(FUObjectToken::Create(OceanologyWater))
				->AddToken(FTextToken::Create(ErrMsgPt2));

			FMessageLog("Blueprint").Notify();

			OceanologyWater = nullptr;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FGetWaterInfoResult AWaterPhysics_Oceanology::CalculateWaterInfo(const UActorComponent* Component, const FVector& Location)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CalculateOceanologyWaterHeight);

	if (!IsValid(OceanologyWater) || !IsValid(GetWaveHightFunction))
		return FGetWaterInfoResult();

	AActor* ThreadOceanologyWater = OceanologyWater;

	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();
	if (ThreadId != GGameThreadId && !bSupportsParallelWaterHeightFetching)
	{
		FScopeLock ScopedLock(&ThreadCopiesCS);
		FOceanologyThreadCopy& OceanologyThreadCopy = OceanologyThreadCopies.FindOrAdd(ThreadId);

		if (!OceanologyThreadCopy.ThreadCopy)
			OceanologyThreadCopy.SyncWithMaster(OceanologyWater);

		ThreadOceanologyWater = OceanologyThreadCopy.ThreadCopy;
	}

	struct FParams
	{
		FVector InLocation;
		FVector OutHeight = FVector::ZeroVector;
	} Params;

	Params.InLocation = Location;

	ThreadOceanologyWater->ProcessEvent(GetWaveHightFunction, (void*)&Params);

	return FGetWaterInfoResult{ FVector(Location.X, Location.Y, Params.OutHeight.Z), FVector::UpVector, FVector::ZeroVector };
}

void AWaterPhysics_Oceanology::PreWaterPhysicsSceneTick()
{
	Super::PreWaterPhysicsSceneTick();

	// Synchronize the properties of our thread copies with the master oceanology water actor.
	for (auto It = OceanologyThreadCopies.CreateIterator(); It; ++It)
	{
		It.Value().SyncWithMaster(OceanologyWater);
		if (It.Value().ThreadCopy == nullptr)
			It.RemoveCurrent();
	}
}

void AWaterPhysics_Oceanology::OnActorBeginOverlapBoundsActor(AActor* OverlappedActor, AActor* OtherActor)
{
	FActorArray& BoundsActors = ActorOverlapTracker.FindOrAdd(OtherActor, FActorArray());
	const int32 NumActors = BoundsActors->Num();
	BoundsActors->AddUnique(OtherActor);

	if (NumActors != BoundsActors->Num())
	{
		AddActorToWater(OtherActor);
	}
}

void AWaterPhysics_Oceanology::OnActorEndOverlapBoundsActor(AActor* OverlappedActor, AActor* OtherActor)
{
	FActorArray& BoundsActors = ActorOverlapTracker.FindOrAdd(OtherActor, FActorArray());
	BoundsActors->Remove(OtherActor);

	if (BoundsActors->Num() == 0)
	{
		RemoveActorFromWater(OtherActor);
	}
}