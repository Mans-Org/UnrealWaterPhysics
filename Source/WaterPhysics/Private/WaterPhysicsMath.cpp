// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsMath.h"
#include "WaterPhysicsCompatibilityLayer.h"

#include "PhysicsEngine/ConvexElem.h"
#include "CollisionShape.h"

#if WPC_PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"
#endif

// NOTE: For now we follow the scaling behaviour of UE4 collision. However, UE4 collision scaling is a bit buggy so it's not optimal.

void TransformSphereElem(FWaterPhysicsCollisionSetup::FSphereElem& SphereElem, const FTransform& Transform)
{
	const FVector BodyScale3DAbs = Transform.GetScale3D().GetAbs();
	SphereElem.Center = Transform.TransformPosition(SphereElem.Center);
	SphereElem.Radius = FMath::Max(SphereElem.Radius * FMath::Min3(BodyScale3DAbs.X, BodyScale3DAbs.Y, BodyScale3DAbs.Z), FCollisionShape::MinSphereRadius());
}

void TransformBoxElem(FWaterPhysicsCollisionSetup::FBoxElem& BoxElem, const FTransform& Transform)
{
	const FVector BodyScale3DAbs = Transform.GetScale3D().GetAbs();
	BoxElem.Extent = FVector(
		FMath::Max(0.5f * BoxElem.Extent.X * BodyScale3DAbs.X, FCollisionShape::MinBoxExtent()),
		FMath::Max(0.5f * BoxElem.Extent.Y * BodyScale3DAbs.Y, FCollisionShape::MinBoxExtent()),
		FMath::Max(0.5f * BoxElem.Extent.Z * BodyScale3DAbs.Z, FCollisionShape::MinBoxExtent())
	);
	BoxElem.Rotation = Transform.TransformRotation(BoxElem.Rotation.Quaternion()).Rotator();
	BoxElem.Center = Transform.TransformPosition(BoxElem.Center);
}

void TransformSphylElem(FWaterPhysicsCollisionSetup::FSphylElem& SphylElem, const FTransform& Transform)
{
	const FVector BodyScale3DAbs   = Transform.GetScale3D().GetAbs();
	const float   ScaledHalfHeight = (SphylElem.HalfHeight + SphylElem.Radius * 2.f) * BodyScale3DAbs.Z * 0.5f;
	const float   ScaledRadius     = FMath::Max(SphylElem.Radius * FMath::Max(BodyScale3DAbs.X, BodyScale3DAbs.Y), 0.1f);

	SphylElem.Radius     = FMath::Max(FMath::Min(ScaledRadius, ScaledHalfHeight), FCollisionShape::MinCapsuleRadius());
	SphylElem.HalfHeight = FMath::Max(FCollisionShape::MinCapsuleAxisHalfHeight(), ScaledHalfHeight - SphylElem.Radius);
	SphylElem.Rotation   = Transform.TransformRotation(SphylElem.Rotation.Quaternion()).Rotator();
	SphylElem.Center     = Transform.TransformPosition(SphylElem.Center);
}

void TransformMeshElem(FWaterPhysicsCollisionSetup::FMeshElem& MeshElem, const FTransform& Transform)
{
	for (FVector& Vertex : MeshElem.VertexList)
		Vertex = Transform.TransformPosition(Vertex);
}

WaterPhysics::FIndexedTriangleMesh ExtractConvexElemTriangles(const struct FKConvexElem& ConvexElem, bool bMirrorX)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ExtractConvexElemTriangles);

	WaterPhysics::FIndexedTriangleMesh OutMesh;

	#if WPC_PHYSICS_INTERFACE_PHYSX // UE4 does not use the FKConvexElem index list for PhysX
	physx::PxConvexMesh* ConvexMesh = bMirrorX ? ConvexElem.GetMirroredConvexMesh() : ConvexElem.GetConvexMesh();
	if (ConvexMesh != NULL)
	{
		const PxU8*   Indices  = ConvexMesh->getIndexBuffer();
		const int32   NumPolys = ConvexMesh->getNbPolygons();

		const PxVec3* Vertices    = ConvexMesh->getVertices();
		const int32   NumVertices = ConvexMesh->getNbVertices();

		for (int32 VertIdx = 0; VertIdx < NumVertices; ++VertIdx)
			OutMesh.VertexList.Add(P2UVector(Vertices[VertIdx]));

		PxHullPolygon PolyData;
		for (int32 PolyIdx = 0; PolyIdx < NumPolys; ++PolyIdx)
		{
			if (ConvexMesh->getPolygonData(PolyIdx, PolyData))
			{
				for (int32 VertIdx = 2; VertIdx < PolyData.mNbVerts; ++VertIdx)
				{
					// Grab triangle indices that we hit
					const int32 I0 = Indices[PolyData.mIndexBase];
					const int32 I1 = Indices[PolyData.mIndexBase + (VertIdx - 1)];
					const int32 I2 = Indices[PolyData.mIndexBase + VertIdx];
					OutMesh.IndexList.Append({ I0, I1, I2 });
				}
			}
		}
	}
	#elif WPC_WITH_CHAOS
	OutMesh.VertexList = ConvexElem.VertexData;
	OutMesh.IndexList = ConvexElem.IndexData;

	if (bMirrorX)
	{
		for (FVector& Vertex : OutMesh.VertexList)
			Vertex.X *= -1.f;

		// Flip normals
		for (int32 i = 0; i < OutMesh.IndexList.Num(); i+=3)
		{
			int32 Tmp = OutMesh.IndexList[i];
			OutMesh.IndexList[i] = OutMesh.IndexList[i+2];
			OutMesh.IndexList[i+2] = Tmp;
		}
	}
	#endif

	return OutMesh;
}