// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsCollisionComponent.h"
#include "WaterPhysicsCompatibilityLayer.h"
#include "WaterPhysicsMath.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/ShapeElem.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/SphereElem.h"
#include "PhysicsEngine/SphylElem.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/UObjectToken.h"
#include "StaticMeshResources.h"

namespace WaterPhysicsCollision
{
	FBodyInstance* FindParentBodyInstance(const USceneComponent* SceneComponent, bool bGetWelded)
	{
		const USceneComponent* CurrentSceneComponent = SceneComponent;
		while (CurrentSceneComponent->GetAttachParent() != nullptr)
		{
			const USceneComponent* ParentComponent = CurrentSceneComponent->GetAttachParent();

			if (const UPrimitiveComponent* ParentPrimitive = Cast<const UPrimitiveComponent>(ParentComponent))
			{
				if (FBodyInstance* BodyInstance = ParentPrimitive->GetBodyInstance(CurrentSceneComponent->GetAttachSocketName(), bGetWelded))
					return BodyInstance;
			}

			CurrentSceneComponent = ParentComponent;
		}

		return nullptr;
	}

	FTransform GetSubstepComponentWorldTransform(const USceneComponent* SceneComponent, FBodyInstance* ParentBodyInstance)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetSubstepComponentWorldTransform);

		UPrimitiveComponent* ParentPrimitiveComponent = ParentBodyInstance->OwnerComponent.Get();
		if (!IsValid(ParentPrimitiveComponent))
			return SceneComponent->GetComponentTransform();

		const FTransform ParentBodyInstanceTransform = ParentBodyInstance->GetUnrealWorldTransform();

		// We might not be directly attached to the body instance (For example, being attached to a welded component). 
		// In this case, walk the component chain until we find the root primitive component which owns this body instance.
		FTransform FinalTransform = SceneComponent->GetSocketTransform(SceneComponent->GetAttachSocketName(), RTS_Component) 
			* SceneComponent->GetRelativeTransform();
		bool bAttachedToBodyInstance = true;

		// Walk the attachment chain and add together the relative transform of each component.
		// This ensures that we take into account the transform of welded components.
		const USceneComponent* CurrentSceneComponent = SceneComponent;
		while (CurrentSceneComponent->GetAttachParent() != nullptr
			&& CurrentSceneComponent->GetAttachParent() != ParentPrimitiveComponent)
		{
			CurrentSceneComponent = CurrentSceneComponent->GetAttachParent();
			FinalTransform *= CurrentSceneComponent->GetSocketTransform(CurrentSceneComponent->GetAttachSocketName(), RTS_Component) 
				* CurrentSceneComponent->GetRelativeTransform();

			if (CurrentSceneComponent->GetAttachParent() == nullptr)
			{
				bAttachedToBodyInstance = false;
				break;
			}
		}

		if (bAttachedToBodyInstance)
		{
			USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ParentPrimitiveComponent);
			if (SkeletalMeshComponent && SceneComponent->GetAttachSocketName() == NAME_None)
			{
				// If we are attached to a skel mesh, and the socket is None, we are attached to the skel mesh component, and not one of its bones.
				// If this is the case, then we need to adjust for the fact that bodyInstance->GetUnrealWorldTransform() is going to give us the transform
				// of the root bone, and NOT the transform of the skeletal mesh component.
				//
				// This can be done by getting the reference transform of the root bone in component space, and then "un-rotate" 
				// the relative component transform with this ref transform.
				const FTransform RootBoneRefTransform = FTransform(WaterPhysicsCompat::GetSkeletalMeshAsset(SkeletalMeshComponent)->GetRefPoseMatrix(0));
				FinalTransform *= RootBoneRefTransform.Inverse();
			}

			FinalTransform *= ParentBodyInstanceTransform;
		}

		// Take into account Absolute transforms
		if (SceneComponent->IsUsingAbsoluteLocation())
			FinalTransform.SetLocation(SceneComponent->GetComponentLocation());
		if (SceneComponent->IsUsingAbsoluteRotation())
			FinalTransform.SetRotation(SceneComponent->GetComponentQuat());
		if (SceneComponent->IsUsingAbsoluteScale())
			FinalTransform.SetScale3D(SceneComponent->GetComponentScale());

		return FinalTransform;
	}
};

UWaterPhysicsCollisionComponent::UWaterPhysicsCollisionComponent()
{
	CollisionType = EWaterPhysicsCollisionType::Mesh;
	Mesh = nullptr;
	LOD = 0;
	BoxExtent = FVector(32.f);
	SphereRadius = 32.f;
	CapsuleRadius = 22.0f;
	CapsuleHalfHeight = 44.0f;

#if WITH_EDITORONLY_DATA
	LineThickness = 2.f;
	ShapeColor = FColor::Red;
	SetVisibleFlag(true);
	bVisibleOnlyWithShowCollision = false;
#endif
}

#if WITH_EDITOR
void UWaterPhysicsCollisionComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	// Ensure CapsuleHalfHeight > CapsuleRadius
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaterPhysicsCollisionComponent, CapsuleHalfHeight))
	{
		CapsuleHalfHeight = FMath::Max3(0.f, CapsuleHalfHeight, CapsuleRadius);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaterPhysicsCollisionComponent, CapsuleRadius))
	{
		CapsuleRadius = FMath::Clamp(CapsuleRadius, 0.f, CapsuleHalfHeight);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaterPhysicsCollisionComponent, Mesh)
		  || PropertyName == GET_MEMBER_NAME_CHECKED(UWaterPhysicsCollisionComponent, LOD))
	{
		if (CollisionType == EWaterPhysicsCollisionType::Mesh && IsValid(Mesh))
		{
			if (!Mesh->bAllowCPUAccess)
			{
				const static FText ErrMsgPt1 = NSLOCTEXT("WaterPhysicsCollision", "PostEditChangePropertyStart", "Using WaterPhysicsCollision with");
				const static FText ErrMsgPt2 = NSLOCTEXT("WaterPhysicsCollision", "PostEditChangePropertyEnd", "but 'Allow CPU Access' is not enabled. This is required for converting extracting mesh data at runtime.");
				
				FMessageLog("Blueprint").Warning()
					->AddToken(FTextToken::Create(ErrMsgPt1))
					->AddToken(FUObjectToken::Create(Mesh))
					->AddToken(FTextToken::Create(ErrMsgPt2));

				FMessageLog("Blueprint").Notify();
			}

			LOD = FMath::Clamp(LOD, 0, Mesh->GetNumLODs() - 1);
		}
		else
		{
			LOD = 0;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif	// WITH_EDITOR

FTransform UWaterPhysicsCollisionComponent::GetWaterPhysicsCollisionWorldTransform(const FName& BodyName) const
{
	if (UPhysicsSettings::Get()->bSubstepping) // Don't pay for expensive GetSubstepComponentWorldTransform if not needed
	{
		if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(GetAttachParent()))
			if (FBodyInstance* BodyInstance = PrimitiveComponent->GetBodyInstance(GetAttachSocketName()))
				return WaterPhysicsCollision::GetSubstepComponentWorldTransform(this, PrimitiveComponent->GetBodyInstance(GetAttachSocketName()));
	}

	return GetComponentTransform();
}

FWaterPhysicsCollisionSetup UWaterPhysicsCollisionComponent::GenerateWaterPhysicsCollisionSetup(const FName& BodyName) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GenerateWaterPhysicsCollisionSetup);

	FWaterPhysicsCollisionSetup OutCollisionSetup;

	switch (CollisionType)
	{
	case EWaterPhysicsCollisionType::Mesh:
	{
		if (!IsValid(Mesh) || !Mesh->bAllowCPUAccess)
			break;

		FStaticMeshRenderData* RenderData = WaterPhysicsCompat::GetStaticMeshRenderData(Mesh);
		if (!RenderData || !RenderData->LODResources.IsValidIndex(LOD))
			break;

		// We should probably allow this to be cached as fetching this information each frame can get expensive.
		const FStaticMeshLODResources& LODResource = RenderData->LODResources[LOD];
		const FIndexArrayView IndexArray = LODResource.IndexBuffer.GetArrayView();
		const FPositionVertexBuffer& PositionVertexBuffer = LODResource.VertexBuffers.PositionVertexBuffer;

		if (IndexArray.Num() == 0 || PositionVertexBuffer.GetNumVertices() == 0)
			break;

		FWaterPhysicsCollisionSetup::FMeshElem& MeshElem = OutCollisionSetup.MeshElems[OutCollisionSetup.MeshElems.AddDefaulted()];

		MeshElem.IndexList.SetNumUninitialized(IndexArray.Num());
		for (int32 i = 0; i < IndexArray.Num(); i += 3)
		{
			// Flip triangle normal by adding the indices in reverse
			MeshElem.IndexList[i+0] = IndexArray[i+2];
			MeshElem.IndexList[i+1] = IndexArray[i+1];
			MeshElem.IndexList[i+2] = IndexArray[i+0];
		}

		MeshElem.VertexList.SetNumUninitialized(PositionVertexBuffer.GetNumVertices());
		for (uint32 i = 0; i < PositionVertexBuffer.GetNumVertices(); i++)
		{
			auto VertexPosition = PositionVertexBuffer.VertexPosition(i);
			MeshElem.VertexList[i] = FVector(VertexPosition.X, VertexPosition.Y, VertexPosition.Z);
		}

		break;
	}
	case EWaterPhysicsCollisionType::MeshCollision:
	{
		UBodySetup* BodySetup = IsValid(Mesh) ? WaterPhysicsCompat::GetStaticMeshBodySetup(Mesh) : nullptr;
		if (!IsValid(BodySetup))
			break;

		for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
			OutCollisionSetup.SphereElems.Add({ SphereElem.Center, SphereElem.Radius });

		for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
			OutCollisionSetup.BoxElems.Add({ BoxElem.Center, BoxElem.Rotation, FVector(BoxElem.X, BoxElem.Y, BoxElem.Z) });

		for (const FKSphylElem& SphylElem : BodySetup->AggGeom.SphylElems)
			OutCollisionSetup.SphylElems.Add({ SphylElem.Center, SphylElem.Rotation, SphylElem.Radius, SphylElem.Length });

		for (const FKConvexElem& ConvexElem : BodySetup->AggGeom.ConvexElems)
		{
			FTransform LocalConvexElemTransform = ConvexElem.GetTransform();
			const bool bUseNegX = CalcMeshNegScaleCompensation(GetComponentScale(), LocalConvexElemTransform);
			WaterPhysics::FIndexedTriangleMesh ConvexMesh = ExtractConvexElemTriangles(ConvexElem, bUseNegX);

			FWaterPhysicsCollisionSetup::FMeshElem MeshElem;
			MeshElem.VertexList = MoveTemp(ConvexMesh.VertexList);
			MeshElem.IndexList = MoveTemp(ConvexMesh.IndexList);

			OutCollisionSetup.MeshElems.Add(MeshElem);
		}

		break;
	}
	case EWaterPhysicsCollisionType::Box:
		OutCollisionSetup.BoxElems.Add({ FVector::ZeroVector, FRotator::ZeroRotator, BoxExtent });
		break;
	case EWaterPhysicsCollisionType::Sphere:
		OutCollisionSetup.SphereElems.Add({ FVector::ZeroVector, SphereRadius });
		break;
	case EWaterPhysicsCollisionType::Capsule:
		OutCollisionSetup.SphylElems.Add({ FVector::ZeroVector, FRotator::ZeroRotator, CapsuleRadius, CapsuleHalfHeight });
		break;
	default:
		break;
	}

	return OutCollisionSetup;
}

FBodyInstance* UWaterPhysicsCollisionComponent::GetWaterPhysicsCollisionBodyInstance(const FName& BodyName, bool bGetWelded) const
{
	// NOTE: For now WaterPhysicsCollisionComponent only has one body name (NAME_None). This could be extended if the user for example wanted to add
	// an array of water physics collision setups per water physics collision component.
	return WaterPhysicsCollision::FindParentBodyInstance(this, bGetWelded);
}

TArray<FName> UWaterPhysicsCollisionComponent::GetAllBodyNames() const
{
	return { GetAttachSocketName() };
}