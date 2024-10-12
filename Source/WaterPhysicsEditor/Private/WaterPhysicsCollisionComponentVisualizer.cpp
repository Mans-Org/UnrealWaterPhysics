// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsCollisionComponentVisualizer.h"
#include "WaterPhysicsCollisionComponent.h"
#include "WaterPhysicsMath.h"
#include "EngineUtils.h"

void FWaterPhysicsCollisionComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UWaterPhysicsCollisionComponent* WaterPhysicsCollisionComponent = Cast<const UWaterPhysicsCollisionComponent>(Component);
	if (!IsValid(WaterPhysicsCollisionComponent))
	{
		return;
	}

	const float  LineThickness = WaterPhysicsCollisionComponent->LineThickness;
	const FColor ShapeColor    = WaterPhysicsCollisionComponent->ShapeColor;

	UWorld *World = Component->GetWorld();
	const bool bUsesGameHiddenFlags = World && World->UsesGameHiddenFlags();

	if (!WaterPhysicsCollisionComponent->GetVisibleFlag() 
		|| (bUsesGameHiddenFlags && WaterPhysicsCollisionComponent->bHiddenInGame)
		|| (WaterPhysicsCollisionComponent->GetVisibleFlag() && WaterPhysicsCollisionComponent->bVisibleOnlyWithShowCollision && !View->Family->EngineShowFlags.Collision))
		return;

	FWaterPhysicsCollisionSetup CollisionSetup = WaterPhysicsCollisionComponent->GenerateWaterPhysicsCollisionSetup(NAME_None);
	const FTransform CollisionTransform = WaterPhysicsCollisionComponent->GetComponentTransform();

	// In order to allow for component selection in the editor viewport we add an HActor hit proxy.
	// However, that proxy requires us to pass a UPrimitiveComponent, which we do not have. That is fine however since epic never uses the primitive
	// interface and instead uses the component pointer as a look-up for the underlying component on the actor. (See FLevelEditorViewportClient::ProcessClick for implementation details)
	// This is a bit sketchy since Epic could possibly change this behaviour in the future. However, it's a really nice (and easy) way of allowing the user to select
	// our components in the editor viewport.
	if (AActor* OwnerActor = WaterPhysicsCollisionComponent->GetOwner())
		PDI->SetHitProxy(new HActor(OwnerActor, static_cast<const UPrimitiveComponent*>(Component)));
	
	// Transform the Collision Setup to correct world transform
	for (FWaterPhysicsCollisionSetup::FSphereElem& SphereElem : CollisionSetup.SphereElems)
	{
		TransformSphereElem(SphereElem, CollisionTransform);
		DrawWireSphere(PDI, SphereElem.Center, ShapeColor, SphereElem.Radius, 12, SDPG_World, LineThickness, 0.001f, false);
	}

	for (FWaterPhysicsCollisionSetup::FBoxElem& BoxElem : CollisionSetup.BoxElems)
	{
		TransformBoxElem(BoxElem, CollisionTransform);
		DrawWireBox(PDI, FTransform(BoxElem.Rotation, BoxElem.Center).ToMatrixNoScale(), FBox(-BoxElem.Extent, BoxElem.Extent), ShapeColor, SDPG_World, LineThickness, 0.001f, false);
	}

	for (FWaterPhysicsCollisionSetup::FSphylElem& SphylElem : CollisionSetup.SphylElems)
	{
		TransformSphylElem(SphylElem, CollisionTransform);
		
		// Due to some strange clamping in DrawWireCapsule the scale was incorrect, I have inlined the function here with the clamping removed
		{
			const static auto DrawHalfCircle = [](FPrimitiveDrawInterface* PDI, const FVector& Base, const FVector& X, const FVector& Y, 
				const FLinearColor& Color, float Radius, int32 NumSides, float Thickness, float DepthBias, bool bScreenSpace)
			{
				const float	AngleDelta = (float)PI / ((float)NumSides);
				FVector	LastVertex = Base + X * Radius;

				for (int32 SideIndex = 0; SideIndex < NumSides; SideIndex++)
				{
					const FVector	Vertex = Base + (X * FMath::Cos(AngleDelta * (SideIndex + 1)) + Y * FMath::Sin(AngleDelta * (SideIndex + 1))) * Radius;
					PDI->DrawLine(LastVertex, Vertex, Color, SDPG_World, Thickness, DepthBias, bScreenSpace);
					LastVertex = Vertex;
				}
			};

			const FVector XAxis = SphylElem.Rotation.RotateVector(FVector::ForwardVector);
			const FVector YAxis = SphylElem.Rotation.RotateVector(FVector::RightVector);
			const FVector ZAxis = SphylElem.Rotation.RotateVector(FVector::UpVector);

			const int32 NumSides = 12;
			const uint8 DepthPriority = SDPG_World;
			const float DepthBias = 0.001f;
			const bool  bScreenSpace = false;

			const FVector TopEnd = SphylElem.Center + (SphylElem.HalfHeight * ZAxis);
			const FVector BottomEnd = SphylElem.Center - (SphylElem.HalfHeight * ZAxis);

			DrawCircle(PDI, TopEnd, XAxis, YAxis, ShapeColor, SphylElem.Radius, NumSides, DepthPriority, LineThickness, DepthBias, bScreenSpace);
			DrawCircle(PDI, BottomEnd, XAxis, YAxis, ShapeColor, SphylElem.Radius, NumSides, DepthPriority, LineThickness, DepthBias, bScreenSpace);

			// Draw domed caps
			DrawHalfCircle(PDI, TopEnd, YAxis, ZAxis, ShapeColor, SphylElem.Radius, NumSides / 2, LineThickness, DepthBias, bScreenSpace);
			DrawHalfCircle(PDI, TopEnd, XAxis, ZAxis, ShapeColor, SphylElem.Radius, NumSides / 2, LineThickness, DepthBias, bScreenSpace);

			DrawHalfCircle(PDI, BottomEnd, YAxis, -ZAxis, ShapeColor, SphylElem.Radius, NumSides / 2, LineThickness, DepthBias, bScreenSpace);
			DrawHalfCircle(PDI, BottomEnd, XAxis, -ZAxis, ShapeColor, SphylElem.Radius, NumSides / 2, LineThickness, DepthBias, bScreenSpace);

			// we set NumSides to 4 as it makes a nicer looking capsule as we only draw 2 HalfCircles above
			const int32 NumCylinderLines = 4;

			// Draw lines for the cylinder portion 
			const float	AngleDelta = 2.0f * PI / NumCylinderLines;
			FVector	LastVertex = SphylElem.Center + XAxis * SphylElem.Radius;

			for (int32 SideIndex = 0; SideIndex < NumCylinderLines; SideIndex++)
			{
				const FVector Vertex = SphylElem.Center + (XAxis * FMath::Cos(AngleDelta * (SideIndex + 1)) + YAxis * FMath::Sin(AngleDelta * (SideIndex + 1))) * SphylElem.Radius;
				PDI->DrawLine(LastVertex - ZAxis * SphylElem.HalfHeight, LastVertex + ZAxis * SphylElem.HalfHeight, ShapeColor, DepthPriority, LineThickness, DepthBias, bScreenSpace);
				LastVertex = Vertex;
			}
		}
	}

	for (FWaterPhysicsCollisionSetup::FMeshElem& MeshElem : CollisionSetup.MeshElems)
	{
		TransformMeshElem(MeshElem, CollisionTransform);

		for (int32 i = 0; i < MeshElem.IndexList.Num(); i+=3)
		{
			const FVector V0 = MeshElem.VertexList[MeshElem.IndexList[i+0]];
			const FVector V1 = MeshElem.VertexList[MeshElem.IndexList[i+1]];
			const FVector V2 = MeshElem.VertexList[MeshElem.IndexList[i+2]];
			
			PDI->DrawLine(V0, V1, ShapeColor, SDPG_World, LineThickness, 0.001f, false);
			PDI->DrawLine(V1, V2, ShapeColor, SDPG_World, LineThickness, 0.001f, false);
			PDI->DrawLine(V2, V0, ShapeColor, SDPG_World, LineThickness, 0.001f, false);
		}
	}

	PDI->SetHitProxy(nullptr);
}