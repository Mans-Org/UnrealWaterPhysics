// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsScene.h"
#include "WaterPhysicsModule.h"
#include "WaterPhysicsMath.h"
#include "WaterPhysicsCompatibilityLayer.h"
#include "WaterPhysicsCollisionInterface.h"
#include "WaterPhysicsDebug/WaterPhysicsDebugHelpers.h"
#include "WaterPhysicsDebug/WaterPhysicsDataProfiler.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/ShapeElem.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/SphereElem.h"
#include "PhysicsEngine/SphylElem.h"
#include "PhysicsEngine/BodySetup.h"

#include "Async/ParallelFor.h"

namespace WaterPhysics
{
	// Optimized structure for edge lookups with pre-computed hash
	struct FEdgeKey
	{
		uint64 Key;
		int32 Hash;

		FEdgeKey() = default;

		explicit FEdgeKey(uint32 A, uint32 B)
			: Key(((uint64)FMath::Max(A, B) << 32) | (uint64)FMath::Min(A, B))
			, Hash(::GetTypeHash(Key))
		{}

		FORCEINLINE friend uint32 GetTypeHash(const FEdgeKey& O) { return O.Hash; }
		FORCEINLINE bool operator==(const FEdgeKey& O) const { return Key == O.Key; }
	};

	template<typename TTriangleArray> int32 NumTriangles(TTriangleArray& TriangleArray);
	template<typename TTriangleArray> int32* GetIndices(TTriangleArray& TriangleArray, int32 TriangleIndex);
	template<typename TTriangleArray> FVector& GetVertex(TTriangleArray& TriangleArray, int32 TriangleIndex, int32 VertexIndex);
	template<typename TTriangleArray> int32 SplitEdge(TTriangleArray& TriangleArray, int32 IndexA, int32 IndexB);
	template<typename TTriangleArray> int32 AddTriangle(TTriangleArray& TriangleArray, int32 TriangleIndex, int32 IndexA, int32 IndexB, int32 IndexC);

	template<> FORCEINLINE int32 NumTriangles<FSubmergedTriangleArray>(FSubmergedTriangleArray& TriangleArray) { return TriangleArray.TriangleList.Num(); }
	template<> FORCEINLINE int32 NumTriangles<FIndexedTriangleMesh>(FIndexedTriangleMesh& TriangleArray) { return TriangleArray.IndexList.Num() / 3; }

	template<> FORCEINLINE int32* GetIndices<FSubmergedTriangleArray>(FSubmergedTriangleArray& TriangleArray, int32 TriangleIndex) 
	{
		return &TriangleArray.TriangleList[TriangleIndex].Indices[0];
	}
	template<> FORCEINLINE int32* GetIndices<FIndexedTriangleMesh>(FIndexedTriangleMesh& TriangleArray, int32 TriangleIndex) 
	{ 
		return &TriangleArray.IndexList[TriangleIndex * 3];
	}

	template<> FORCEINLINE FVector& GetVertex<FSubmergedTriangleArray>(FSubmergedTriangleArray& TriangleArray, int32 TriangleIndex, int32 VertexIndex)
	{
		return TriangleArray.VertexList[TriangleArray.TriangleList[TriangleIndex].Indices[VertexIndex]].Position;
	}
	template<> FORCEINLINE FVector& GetVertex<FIndexedTriangleMesh>(FIndexedTriangleMesh& TriangleArray, int32 TriangleIndex, int32 VertexIndex)
	{
		return TriangleArray.VertexList[TriangleArray.IndexList[(TriangleIndex * 3) + VertexIndex]];
	}

	template<> FORCEINLINE int32 SplitEdge<FSubmergedTriangleArray>(FSubmergedTriangleArray& TriangleArray, int32 IndexA, int32 IndexB)
	{
		return TriangleArray.VertexList.Add(FSubmergedTriangleArray::FVertex
		{
			(TriangleArray.VertexList[IndexA].Position + TriangleArray.VertexList[IndexB].Position) / 2.f,
			(TriangleArray.VertexList[IndexA].WaterVelocity + TriangleArray.VertexList[IndexB].WaterVelocity) / 2.f,
			(TriangleArray.VertexList[IndexA].Depth + TriangleArray.VertexList[IndexB].Depth) / 2.f
		});
	}
	template<> FORCEINLINE int32 SplitEdge<FIndexedTriangleMesh>(FIndexedTriangleMesh& TriangleArray, int32 IndexA, int32 IndexB)
	{
		return TriangleArray.VertexList.Add((TriangleArray.VertexList[IndexA] + TriangleArray.VertexList[IndexB]) / 2.f);
	}

	template<> FORCEINLINE int32 AddTriangle<FSubmergedTriangleArray>(FSubmergedTriangleArray& TriangleArray, int32 TriangleIndex, int32 IndexA, int32 IndexB, int32 IndexC)
	{
		return TriangleArray.EmplaceTriangle(FSubmergedTriangleArray::FTriangle
		{
			{ IndexA, IndexB, IndexC }, 
			TriangleArray.TriangleList[TriangleIndex].OriginalTriangleIndex 
		});
	}
	template<> FORCEINLINE int32 AddTriangle<FIndexedTriangleMesh>(FIndexedTriangleMesh& TriangleArray, int32 TriangleIndex, int32 IndexA, int32 IndexB, int32 IndexC)
	{
		TriangleArray.IndexList.Append({ IndexA, IndexB, IndexC });
		return (TriangleArray.IndexList.Num() - 1) / 3;
	}

	template<typename TTriangleArray>
	void TessellateTriangles(TTriangleArray& TriangleArray, const FTessellationSettings& TessellationSettings)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TessellateTriangles);

		struct Local
		{
			static TArray<int32, TInlineAllocator<3>> TesselateTriangle(TTriangleArray& TriangleArray, int32 Index, TMap<FEdgeKey, int32>& EdgeSplitVertices)
			{
				// Algorithm
				// a
				// |\
				// | \ d
				// |__\
				// c    b
				// Sort edges by length
				// take largest one, create vertex in the middle (d)
				// split the triangle from a to d (a = corner adjacent to line b-c)

				const FVector Vertices[3] = { 
					GetVertex(TriangleArray, Index, 0), 
					GetVertex(TriangleArray, Index, 1), 
					GetVertex(TriangleArray, Index, 2) 
				};

				struct FTriangleLine { int32 A; int32 B; };
				TArray<FTriangleLine, TInlineAllocator<3>> Lines = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
				Lines.Sort([&](const FTriangleLine& X, const FTriangleLine& Y) { return (Vertices[X.A] - Vertices[X.B]).SizeSquared() > (Vertices[Y.A] - Vertices[Y.B]).SizeSquared(); });

				const auto& H = Lines[0]; // Hypotenuse

				const int32 AI = GetIndices(TriangleArray, Index)[H.A];
				const int32 BI = GetIndices(TriangleArray, Index)[H.B];
				const int32 CI = GetIndices(TriangleArray, Index)[(H.B + 1) % 3];

				const FEdgeKey EdgeIndex(AI, BI);

				int32* ExistingVertexIndex = EdgeSplitVertices.Find(EdgeIndex);
				if (!ExistingVertexIndex)
					ExistingVertexIndex = &EdgeSplitVertices.Add(EdgeIndex, SplitEdge(TriangleArray, AI, BI));

				const int32 DI = *ExistingVertexIndex;

				// Overwrite current with one of the split triangles
				GetIndices(TriangleArray, Index)[0] = DI;
				GetIndices(TriangleArray, Index)[1] = BI;
				GetIndices(TriangleArray, Index)[2] = CI;

				// Add the other triangle at the back of the list
				const int32 NewTriangle = AddTriangle(TriangleArray, Index, AI, DI, CI);

				return { Index, NewTriangle };
			}

			static void TesselateTriangle_Recursive(TTriangleArray& TriangleArray, const FTessellationSettings& TessellationSettings, int32 Index, TMap<FEdgeKey, int32>& AreaSplitMap)
			{
				const FVector Vertices[3] = { GetVertex(TriangleArray, Index, 0), GetVertex(TriangleArray, Index, 1), GetVertex(TriangleArray, Index, 2) };
				const float TriangleArea = CalcTriangleAreaM2(Vertices);
				if (TriangleArea > TessellationSettings.MaxArea)
				{
					const auto NewTriangles = TesselateTriangle(TriangleArray, Index, AreaSplitMap);
					for (int32 i = 0; i < NewTriangles.Num(); i++)
					{
						TesselateTriangle_Recursive(TriangleArray, TessellationSettings, NewTriangles[i], AreaSplitMap);
					}
				}
			}
		};

		TMap<FEdgeKey, int32> EdgeSplitVertices;

		switch (TessellationSettings.TessellationMode)
		{
		case EWaterPhysicsTessellationMode::Levels:
		{
			for (int32 i = 0; i < TessellationSettings.Levels; i++)
			{
				const int32 NrTriangles = NumTriangles(TriangleArray);
				for (int32 j = 0; j < NrTriangles; j++)
					Local::TesselateTriangle(TriangleArray, j, EdgeSplitVertices);
			}
			break;
		}
		case EWaterPhysicsTessellationMode::Area:
		{
			const int32 NrTriangles = NumTriangles(TriangleArray);
			for (int32 i = 0; i < NrTriangles; i++)
				Local::TesselateTriangle_Recursive(TriangleArray, TessellationSettings, i, EdgeSplitVertices);
			break;
		}
		default:
			checkf(0, TEXT("Unknown tesselation mode"))
			break;
		}
	}

	FIndexedTriangleMesh TriangulateBoxElem(const FVector& BoxHalfExtent, const FVector& BoxCenter, const FRotator& BoxRotation, int32 Subdivisions)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TriangulateBoxElem);

		const FTransform BoxTransform = FTransform(BoxRotation, BoxCenter);
		const FVector BoxMin = -FVector(BoxHalfExtent.X, BoxHalfExtent.Y, BoxHalfExtent.Z);
		const FVector BoxMax =  FVector(BoxHalfExtent.X, BoxHalfExtent.Y, BoxHalfExtent.Z);
		
		FIndexedTriangleMesh BoxTriangleMesh =
		{
			FVertexList
			{
				BoxTransform.TransformPosition({ BoxMin.X, BoxMin.Y, BoxMin.Z }),
				BoxTransform.TransformPosition({ BoxMax.X, BoxMin.Y, BoxMin.Z }),
				BoxTransform.TransformPosition({ BoxMax.X, BoxMax.Y, BoxMin.Z }),
				BoxTransform.TransformPosition({ BoxMin.X, BoxMax.Y, BoxMin.Z }),

				BoxTransform.TransformPosition({ BoxMin.X, BoxMin.Y, BoxMax.Z }),
				BoxTransform.TransformPosition({ BoxMax.X, BoxMin.Y, BoxMax.Z }),
				BoxTransform.TransformPosition({ BoxMax.X, BoxMax.Y, BoxMax.Z }),
				BoxTransform.TransformPosition({ BoxMin.X, BoxMax.Y, BoxMax.Z })
			},
			FIndexList
			{
				0,2,1, 0,3,2, 0,1,5, 0,5,4,
				1,2,6, 1,6,5, 2,3,7, 2,7,6,
				3,0,4, 3,4,7, 5,6,7, 5,7,4
			}
		};

		FTessellationSettings TessellationSettings;
		TessellationSettings.TessellationMode = EWaterPhysicsTessellationMode::Levels;
		TessellationSettings.Levels = Subdivisions;
		TessellateTriangles(BoxTriangleMesh, TessellationSettings);

		return BoxTriangleMesh;
	}

	FIndexedTriangleMesh TriangulateSphereElem(float SphereRadius, const FVector& SphereCenter, int32 Subdivisions)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TriangulateSphereElem);

		const static auto SubdivideIcoSphere = [](FIndexedTriangleMesh& InIcoSphere)
		{
			TMap<FEdgeKey, int32> EdgeLookup;
			EdgeLookup.Reserve(InIcoSphere.IndexList.Num());

			FIndexList NewIndexList;
			NewIndexList.Reserve(InIcoSphere.IndexList.Num() * 3);

			const auto VertexForEdge = [&](int32 First, int32 Second)->int32
			{
				const FEdgeKey Key(First, Second);
				if (const auto* Index = EdgeLookup.Find(Key))
					return *Index;

				return EdgeLookup.Add(Key, InIcoSphere.VertexList.Add((InIcoSphere.VertexList[First] + InIcoSphere.VertexList[Second]).GetSafeNormal()));
			};

			for (int32 i = 0; i < InIcoSphere.IndexList.Num(); i += 3)
			{
				const int32* Indices = &InIcoSphere.IndexList[i];
				const int32  Mid[3] = {
					VertexForEdge(Indices[0], Indices[1]),
					VertexForEdge(Indices[1], Indices[2]),
					VertexForEdge(Indices[2], Indices[0])
				};

				NewIndexList.Append(
				{
					Indices[0], Mid[0], Mid[2],
					Indices[1], Mid[1], Mid[0],
					Indices[2], Mid[2], Mid[1],
					Mid[0], Mid[1], Mid[2]
				});
			}

			InIcoSphere.IndexList = NewIndexList;
		};

		const static FVertexList UnitIcoSphereVertices = 
		{
			FVector( 0.000000,  0.000000, -1.000000),
			FVector(-0.525720, -0.723600, -0.447215),
			FVector(-0.850640,  0.276385, -0.447215),
			FVector( 0.000000,  0.894425, -0.447215),
			FVector( 0.850640,  0.276385, -0.447215),
			FVector( 0.525720, -0.723600, -0.447215),
			FVector(-0.850640, -0.276385,  0.447215),
			FVector(-0.525720,  0.723600,  0.447215),
			FVector( 0.525720,  0.723600,  0.447215),
			FVector( 0.850640, -0.276385,  0.447215),
			FVector( 0.000000, -0.894425,  0.447215),
			FVector( 0.000000,  0.000000,  1.000000),
		};

		const static FIndexList UnitIcoSphereIndices =
		{
			0,1,2,  1,0,5,  0,2,3,  0,3,4,   
			0,4,5,  1,5,10, 2,1,6,  3,2,7,  
			4,3,8,  5,4,9,  1,10,6, 2,6,7, 
			3,7,8,  4,8,9,  5,9,10, 6,10,11, 
			7,6,11, 8,7,11, 9,8,11, 10,9,11
		};

		FIndexedTriangleMesh IcoSphere = { UnitIcoSphereVertices, UnitIcoSphereIndices };

		for (int32 i = 0; i < Subdivisions; ++i)
			SubdivideIcoSphere(IcoSphere);

		for (FVector& Vertex : IcoSphere.VertexList)
			Vertex = SphereCenter + (SphereRadius * Vertex);

		return IcoSphere;
	}

	FIndexedTriangleMesh TriangulateSphylElem(float HalfHeight, float Radius, const FVector& CapsuleCenter, const FRotator& CapsuleRotation, int32 Subdivisions)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TriangulateSphylElem);

		const auto SubdivideIcoCapsule = [&](FIndexedTriangleMesh& InIcoSphere)
		{
			TMap<FEdgeKey, int32> EdgeLookup;
			EdgeLookup.Reserve(InIcoSphere.IndexList.Num());

			FIndexList NewIndexList;
			NewIndexList.Reserve(InIcoSphere.IndexList.Num() * 3);

			const auto VertexForEdge = [&](int32 First, int32 Second)->int32
			{
				const FEdgeKey Key(First, Second);
				if (const auto* Index = EdgeLookup.Find(Key))
					return *Index;

				const auto GetPointClampedToCapsule = [&](const FVector& Point)->FVector
				{
					const FVector PointOnLine = FVector(0.f, 0.f, FMath::Clamp(Point.Z, -HalfHeight, HalfHeight));
					return PointOnLine + (Point - PointOnLine).GetSafeNormal() * Radius;
				};

				return EdgeLookup.Add(Key, InIcoSphere.VertexList.Add(GetPointClampedToCapsule((InIcoSphere.VertexList[First] + InIcoSphere.VertexList[Second]) / 2.f)));
			};

			for (int32 i = 0; i < InIcoSphere.IndexList.Num(); i += 3)
			{
				const int32* Indices = &InIcoSphere.IndexList[i];
				const int32  Mid[3] = {
					VertexForEdge(Indices[0], Indices[1]),
					VertexForEdge(Indices[1], Indices[2]),
					VertexForEdge(Indices[2], Indices[0])
				};

				NewIndexList.Append(
				{
					Indices[0], Mid[0], Mid[2],
					Indices[1], Mid[1], Mid[0],
					Indices[2], Mid[2], Mid[1],
					Mid[0], Mid[1], Mid[2]
				});
			}

			InIcoSphere.IndexList = NewIndexList;
		};

		// This is an icosphere with two extra ege-lopps added close to the center of the sphere
		const static FVertexList UnitIcoCapsuleVertices = 
		{
			FVector( 0.000000,  0.000000, -1.000000),
			FVector(-0.298397, -0.950923,  0.010073),
			FVector(-0.928606,  0.354351,  0.010073),
			FVector(-0.096003,  0.990428,  0.010073),
			FVector( 0.985126,  0.141899,  0.010073),
			FVector( 0.596667, -0.794547,  0.010073),
			FVector(-0.523123,  0.721003,  0.447216),
			FVector( 0.846867, -0.272612,  0.447216),
			FVector(-0.005630, -0.888794,  0.447216),
			FVector( 0.000000,  0.000000,  1.000000),
			FVector(-0.551103, -0.698217, -0.447216),
			FVector(-0.846022,  0.271767, -0.447216),
			FVector( 0.844210,  0.282815, -0.447216),
			FVector(-0.840977, -0.286048,  0.447216),
			FVector( 0.555094,  0.694226,  0.447216),
			FVector( 0.005171,  0.889254, -0.447216),
			FVector( 0.521323, -0.719203, -0.447216),
			FVector( 0.095119, -0.989544, -0.010073),
			FVector(-0.983501, -0.143524, -0.010073),
			FVector(-0.598766,  0.796646, -0.010073),
			FVector( 0.302931,  0.946389, -0.010073),
			FVector( 0.929726, -0.355471, -0.010073),
		};

		const static FIndexList UnitIcoCapsuleIndices =
		{
			0,10,11,  10,0,16,  0,11,15,  0,15,12,
			0,12,16,  1,5,8,    2,1,13,   3,2,6,
			4,3,14,   5,4,7,    1,8,13,   2,13,6,
			3,6,14,   4,14,7,   5,7,8,    13,8,9,
			6,13,9,   14,6,9,   7,14,9,   8,7,9,
			10,16,17, 11,10,18, 15,11,19, 12,15,20,
			16,12,21, 10,17,18,	11,18,19, 15,19,20,
			12,20,21, 16,21,17,	2,18,1,   19,18,2,
			20,19,3,  21,20,4,	21,5,17,  18,17,1,
			3,19,2,   4,20,3,	5,21,4,   5,1,17
		};

		FIndexedTriangleMesh IcoCapsule = { UnitIcoCapsuleVertices, UnitIcoCapsuleIndices };

		for (int32 i = 0; i < IcoCapsule.VertexList.Num(); i++)
		{
			IcoCapsule.VertexList[i] *= Radius;

			// Take all the top vertices+middle verices and move them up, then move all the bottom vertices+new vertices down
			if (!FMath::IsNearlyZero(IcoCapsule.VertexList[i].Z))
				IcoCapsule.VertexList[i].Z += (IcoCapsule.VertexList[i].Z > 0 ? 1 : -1) * HalfHeight;
		}

		for (int32 i = 0; i < Subdivisions; ++i)
			SubdivideIcoCapsule(IcoCapsule);

		const FTransform CapsuleTransform = FTransform(CapsuleRotation, CapsuleCenter);
		for (FVector& Vertex : IcoCapsule.VertexList)
			Vertex = CapsuleTransform.TransformPosition(Vertex);

		return IcoCapsule;
	}

	FIndexedTriangleMesh TriangulateWaterPhysicsCollisionSetup(const FWaterPhysicsCollisionSetup& CollisionSetup, const FTriangleSubdivisionSettings& SubdivisionSettings)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TriangulateWaterPhysicsCollisionSetup);

		// Triangulate each collision setup
		TArray<FIndexedTriangleMesh> TriangleMeshes;
		TriangleMeshes.Reserve(CollisionSetup.NumCollisionElems());
		{
			for (const FWaterPhysicsCollisionSetup::FSphereElem& SphereElem : CollisionSetup.SphereElems)
				TriangleMeshes.Add(TriangulateSphereElem(SphereElem.Radius, SphereElem.Center, SubdivisionSettings.Sphere));

			for (const FWaterPhysicsCollisionSetup::FBoxElem& BoxElem : CollisionSetup.BoxElems)
				TriangleMeshes.Add(TriangulateBoxElem(BoxElem.Extent, BoxElem.Center, BoxElem.Rotation, SubdivisionSettings.Box));

			for (const FWaterPhysicsCollisionSetup::FSphylElem& SphylElem : CollisionSetup.SphylElems)
				TriangleMeshes.Add(TriangulateSphylElem(SphylElem.HalfHeight, SphylElem.Radius, SphylElem.Center, SphylElem.Rotation, SubdivisionSettings.Capsule));

			for (const FWaterPhysicsCollisionSetup::FMeshElem& MeshElem : CollisionSetup.MeshElems)
			{
				FIndexedTriangleMesh TriangulatedMesh;
				TriangulatedMesh.VertexList = MeshElem.VertexList;
				TriangulatedMesh.IndexList = MeshElem.IndexList;

				FTessellationSettings TessellationSettings;
				TessellationSettings.TessellationMode = EWaterPhysicsTessellationMode::Levels;
				TessellationSettings.Levels = SubdivisionSettings.Convex;
				TessellateTriangles(TriangulatedMesh, TessellationSettings);

				TriangleMeshes.Add(TriangulatedMesh);
			}
		}

		// TODO: Perform boolean operations to remove internal triangles
		FIndexedTriangleMesh OutTriangulatedMesh;
		for (const auto& TriangleMesh : TriangleMeshes)
		{
			const int32 IndexOffset      = OutTriangulatedMesh.VertexList.Num();
			const int32 IndexOffsetStart = OutTriangulatedMesh.IndexList.Num();

			OutTriangulatedMesh.VertexList.Append(TriangleMesh.VertexList);
			OutTriangulatedMesh.IndexList.Append(TriangleMesh.IndexList);

			if (IndexOffset > 0)
			{
				for (int32 i = IndexOffsetStart; i < OutTriangulatedMesh.IndexList.Num(); ++i)
				{
					OutTriangulatedMesh.IndexList[i] += IndexOffset;
					check(OutTriangulatedMesh.IndexList[i] >= 0 && OutTriangulatedMesh.IndexList[i] < OutTriangulatedMesh.VertexList.Num());
				}
			}
		}

		return OutTriangulatedMesh;
	}

	FWaterPhysicsCollisionSetup GenerateBodyInstanceWaterPhysicsCollisionSetup(FBodyInstance* BodyInstance, bool bIncludeWeldedBodies)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GenerateBodyInstanceWaterPhysicsCollisionSetup);

		FWaterPhysicsCollisionSetup OutCollisionSetup;

		// NOTE: AddSphereElem, AddBoxElem, AddSphylElem and AddConvexElem mimics the scaling behaviour of UE4. This scaling is buggy
		// and has therefore been created to intentionally follow this buggy behaviour.
		// TODO: This is pretty much copy-pasted in WaterPhysicsMath.cpp, refactor to used shared scaling logic

		const auto AddSphereElem = [&](const FKSphereElem& SphereElem, const FTransform& ParentInstanceWorldTransform, 
			const FTransform& WeldedRelativeTransform, const FVector& ParentBodyScale3D, const FVector& BodyScale3D)
		{
			const FVector BodyScale3DAbs = BodyScale3D.GetAbs();
			const float SphereRadius = FMath::Max(SphereElem.Radius * FMath::Min3(BodyScale3DAbs.X, BodyScale3DAbs.Y, BodyScale3DAbs.Z), FCollisionShape::MinSphereRadius());
			const FTransform SphereWorldTransform = FTransform(FRotator::ZeroRotator, WeldedRelativeTransform.TransformPosition(SphereElem.Center) * ParentBodyScale3D) 
				* ParentInstanceWorldTransform;
			OutCollisionSetup.SphereElems.Add({ SphereWorldTransform.GetLocation(), SphereRadius });
		};

		const auto AddBoxElem = [&](const FKBoxElem& BoxElem, const FTransform& ParentInstanceWorldTransform, 
			const FTransform& WeldedRelativeTransform, const FVector& ParentBodyScale3D, const FVector& BodyScale3D)
		{
			const FVector BodyScale3DAbs = BodyScale3D.GetAbs();
			const FVector BoxHalfExtents(
				FMath::Max(0.5f * BoxElem.X * BodyScale3DAbs.X, FCollisionShape::MinBoxExtent()),
				FMath::Max(0.5f * BoxElem.Y * BodyScale3DAbs.Y, FCollisionShape::MinBoxExtent()),
				FMath::Max(0.5f * BoxElem.Z * BodyScale3DAbs.Z, FCollisionShape::MinBoxExtent())
			);
			const FTransform BoxWorldTransform = FTransform(
				WeldedRelativeTransform.TransformRotation(BoxElem.Rotation.Quaternion()),
				WeldedRelativeTransform.TransformPosition(BoxElem.Center) * ParentBodyScale3D) * ParentInstanceWorldTransform;

			OutCollisionSetup.BoxElems.Add({ BoxWorldTransform.GetLocation(), BoxWorldTransform.Rotator(), BoxHalfExtents });
		};

		const auto AddSphylElem = [&](const FKSphylElem& SphylElem, const FTransform& ParentInstanceWorldTransform, 
			const FTransform& WeldedRelativeTransform, const FVector& ParentBodyScale3D, const FVector& BodyScale3D)
		{
			const FVector BodyScale3DAbs   = BodyScale3D.GetAbs();
			const float   ScaledHalfHeight = (SphylElem.Length + SphylElem.Radius * 2.f) * BodyScale3DAbs.Z * 0.5f;
			const float   ScaledRadius     = FMath::Max(SphylElem.Radius * FMath::Max(BodyScale3DAbs.X, BodyScale3DAbs.Y), 0.1f);
			const float   FinalRadius      = FMath::Max(FMath::Min(ScaledRadius, ScaledHalfHeight), FCollisionShape::MinCapsuleRadius());
			const float   FinalHalfHeight  = FMath::Max(FCollisionShape::MinCapsuleAxisHalfHeight(), ScaledHalfHeight - FinalRadius);
			const FTransform SphylWorldTransform = FTransform(
				WeldedRelativeTransform.TransformRotation(SphylElem.Rotation.Quaternion()),
				WeldedRelativeTransform.TransformPosition(SphylElem.Center) * ParentBodyScale3D) * ParentInstanceWorldTransform;
			OutCollisionSetup.SphylElems.Add({ SphylWorldTransform.GetLocation(), SphylWorldTransform.Rotator(), FinalRadius, FinalHalfHeight });
		};

		const auto AddConvexElem = [&](const FKConvexElem& ConvexElem, const FTransform& ParentInstanceWorldTransform, 
			const FTransform& WeldedRelativeTransform, const FVector& ParentBodyScale3D, const FVector& BodyScale3D)
		{
			FTransform LocalConvexElemTransform = ConvexElem.GetTransform();
			const bool bUseNegX = CalcMeshNegScaleCompensation(BodyScale3D, LocalConvexElemTransform);
			const FTransform ConvexWorldTransform = FTransform(
				WeldedRelativeTransform.TransformRotation(LocalConvexElemTransform.GetRotation()),
				WeldedRelativeTransform.TransformPosition(LocalConvexElemTransform.GetLocation())  * ParentBodyScale3D,
				BodyScale3D.GetAbs()
			) * ParentInstanceWorldTransform;

			FWaterPhysicsCollisionSetup::FMeshElem MeshElem = ExtractConvexElemTriangles(ConvexElem, bUseNegX);

			for (FVector& Vertex : MeshElem.VertexList)
				Vertex = ConvexWorldTransform.TransformPosition(Vertex);

			OutCollisionSetup.MeshElems.Add(MeshElem);
		};

		FBodyInstance* OriginalBodyInstance = BodyInstance;
		BodyInstance = BodyInstance->WeldParent ? BodyInstance->WeldParent : BodyInstance;

		TArray<FPhysicsShapeHandle> Shapes;
		FTransform BodyInstanceTransform(NoInit);
		FPhysicsCommand::ExecuteRead(BodyInstance->GetPhysicsActorHandle(), [&](const FPhysicsActorHandle& ActorHandle)
		{
			BodyInstance->GetAllShapes_AssumesLocked(Shapes);
			BodyInstanceTransform = BodyInstance->GetUnrealWorldTransform_AssumesLocked();
		});

		for (const FPhysicsShapeHandle& Shape : Shapes)
		{
			const FBodyInstance* ShapeBodyInstance = BodyInstance->GetOriginalBodyInstance(Shape);

			if (!bIncludeWeldedBodies && ShapeBodyInstance != OriginalBodyInstance)
				continue;

			const ECollisionShapeType GeomType  = FPhysicsInterface::GetShapeType(Shape);
			FKShapeElem* const        ShapeElem = FUserData::Get<FKShapeElem>(FPhysicsInterface::GetUserData(Shape));

			if (ShapeElem && !ShapeElem->GetContributeToMass()) // Ignore bodies which are not contributing to mass
				continue;

			// GetRelativeBodyTransform checks that we are in game thread for no reason, we therefore inline its contents without that check
			const static auto ThreadedGetRelativeBodyTransform = [](const FBodyInstance* InBodyInstance, const FPhysicsShapeHandle& InShape)->FTransform
			{
				const FBodyInstance* BI = InBodyInstance->WeldParent ? InBodyInstance->WeldParent : InBodyInstance;
				const TMap<FPhysicsShapeHandle, FBodyInstance::FWeldInfo>* ShapeToBodiesMap = BI->GetCurrentWeldInfo();
				const FBodyInstance::FWeldInfo* Result = ShapeToBodiesMap ? ShapeToBodiesMap->Find(InShape) : nullptr;
				return Result ? Result->RelativeTM : FTransform::Identity;
			};

			const FVector    ParentBodyScale3D = BodyInstance->Scale3D;
			const FVector    ShapeBodyScale3D  = ShapeBodyInstance->Scale3D;
			const FTransform RelativeBodyTM    = ThreadedGetRelativeBodyTransform(BodyInstance, Shape); // The relative transform between the body and its welded parent (identity if not welded)

			switch (GeomType)
			{
			case ECollisionShapeType::Sphere:
				AddSphereElem(*ShapeElem->GetShapeCheck<FKSphereElem>(), BodyInstanceTransform, RelativeBodyTM, ParentBodyScale3D, ShapeBodyScale3D);
				break;
			case ECollisionShapeType::Box:
				AddBoxElem(*ShapeElem->GetShapeCheck<FKBoxElem>(), BodyInstanceTransform, RelativeBodyTM, ParentBodyScale3D, ShapeBodyScale3D);
				break;
			case ECollisionShapeType::Capsule:
				AddSphylElem(*ShapeElem->GetShapeCheck<FKSphylElem>(), BodyInstanceTransform, RelativeBodyTM, ParentBodyScale3D, ShapeBodyScale3D);
				break;
			case ECollisionShapeType::Convex:
				AddConvexElem(*ShapeElem->GetShapeCheck<FKConvexElem>(), BodyInstanceTransform, RelativeBodyTM, ParentBodyScale3D, ShapeBodyScale3D);
				break;
			case ECollisionShapeType::Trimesh:
				break; // Trimesh cannot simulate physics, cannot support
			case ECollisionShapeType::Heightfield:
				break; // Not supported by water physics
			default:
				UE_LOG(LogWaterPhysics, Error, TEXT("Triangulate body instance - Unknown geom type."));
			}
		}

		return OutCollisionSetup;
	}

	FWaterPhysicsCollisionSetup GenerateWaterPhysicsCollisionSetup(const IWaterPhysicsCollisionInterface* CollisionInterface, const FName& BodyName)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GenerateWaterPhysicsCollisionSetup);

		FWaterPhysicsCollisionSetup CollisionSetup = CollisionInterface->GenerateWaterPhysicsCollisionSetup(BodyName);
		const FTransform CollisionTransform = CollisionInterface->GetWaterPhysicsCollisionWorldTransform(BodyName);

		// Transform the Collision Setup to correct world transform
		for (FWaterPhysicsCollisionSetup::FSphereElem& SphereElem : CollisionSetup.SphereElems)
			TransformSphereElem(SphereElem, CollisionTransform);

		for (FWaterPhysicsCollisionSetup::FBoxElem& BoxElem : CollisionSetup.BoxElems)
			TransformBoxElem(BoxElem, CollisionTransform);

		for (FWaterPhysicsCollisionSetup::FSphylElem& SphylElem : CollisionSetup.SphylElems)
			TransformSphylElem(SphylElem, CollisionTransform);

		for (FWaterPhysicsCollisionSetup::FMeshElem& MeshElem : CollisionSetup.MeshElems)
			TransformMeshElem(MeshElem, CollisionTransform);

		return CollisionSetup;
	}

	FSubmergedTriangleArray PerformTriangleMeshWaterIntersection(const FWaterSurfaceProvider::FVertexWaterInfoArray& VertexWaterInfo, const FIndexedTriangleMesh& TriangleMesh)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PerformTriangleMeshWaterIntersection);

		FSubmergedTriangleArray Result;

		TArray<float> VertexDepths;
		VertexDepths.SetNum(TriangleMesh.VertexList.Num());

		TArray<int32> VertexSubmergedIndex;
		VertexSubmergedIndex.SetNum(TriangleMesh.VertexList.Num());

		// Calculate the depth of each vertex
		for (int32 i = 0; i < TriangleMesh.VertexList.Num(); ++i)
		{
			VertexDepths[i] = FPlane(VertexWaterInfo[i].WaterSurfaceLocation, VertexWaterInfo[i].WaterSurfaceNormal).PlaneDot(TriangleMesh.VertexList[i]);

			if (VertexDepths[i] < 0.f) {
				VertexSubmergedIndex[i] = Result.VertexList.Emplace(FSubmergedTriangleArray::FVertex{ TriangleMesh.VertexList[i], VertexWaterInfo[i].WaterVelocity, -VertexDepths[i] });
			}
		}

		TMap<FEdgeKey, int32> EdgeSplitVertices;
		EdgeSplitVertices.Reserve(TriangleMesh.VertexList.Num() * 2); // Some extra slack for reduced hash collision

		struct FVertexIndex
		{
			int32 Index;
			int32 VertexOrderIndex; // Order on triangle (0/1/2)
		};
		TArray<FVertexIndex, TInlineAllocator<3>> VerticesOverSurface;
		TArray<FVertexIndex, TInlineAllocator<3>> VerticesUnderSurface;
		for (int32 i = 0; i < TriangleMesh.IndexList.Num(); i+=3)
		{
			VerticesOverSurface.Empty(3);
			VerticesUnderSurface.Empty(3);

			for (int32 j = 0; j < 3; j++)
			{
				const int32 VertexIndex = TriangleMesh.IndexList[i + j];
				(VertexDepths[VertexIndex] < 0.f ? VerticesUnderSurface : VerticesOverSurface).Add({ VertexIndex, j });
			}

			if (VerticesUnderSurface.Num() == 3)
			{   // Entire triangle is submerged
				Result.EmplaceTriangle(FSubmergedTriangleArray::FTriangle{
					{ VertexSubmergedIndex[TriangleMesh.IndexList[i + 0]],
						VertexSubmergedIndex[TriangleMesh.IndexList[i + 1]],
						VertexSubmergedIndex[TriangleMesh.IndexList[i + 2]] },
					i / 3
				});
			}
			else if (VerticesOverSurface.Num() != 3) // Split partially submerged triangle
			{
				const FVertexIndex& A = VerticesOverSurface.Num() == 2 ? VerticesUnderSurface[0] : VerticesOverSurface[0];
				const FVertexIndex& B = VerticesOverSurface.Num() == 2 ? VerticesOverSurface[0] : VerticesUnderSurface[0];
				const FVertexIndex& C = VerticesOverSurface.Num() == 2 ? VerticesOverSurface[1] : VerticesUnderSurface[1];

				const float AAbsVertextDepth = FMath::Abs(VertexDepths[A.Index]);
				const float BAbsVertextDepth = FMath::Abs(VertexDepths[B.Index]);
				const float CAbsVertextDepth = FMath::Abs(VertexDepths[C.Index]);

				const float ABSplitAlpha = AAbsVertextDepth / (AAbsVertextDepth + BAbsVertextDepth);
				const float ACSplitAlpha = AAbsVertextDepth / (AAbsVertextDepth + CAbsVertextDepth);

				const FEdgeKey ABEdgeIndex(A.Index, B.Index);
				const FEdgeKey ACEdgeIndex(A.Index, C.Index);

				const int32* ExistingABIndex = EdgeSplitVertices.Find(ABEdgeIndex);
				const int32* ExistingACIndex = EdgeSplitVertices.Find(ACEdgeIndex);

				int32 ABIndex;
				if (ExistingABIndex != nullptr)
				{
					ABIndex = *ExistingABIndex;
				}
				else
				{
					ABIndex = Result.VertexList.Emplace(FSubmergedTriangleArray::FVertex{
						FMath::Lerp(TriangleMesh.VertexList[A.Index], TriangleMesh.VertexList[B.Index], ABSplitAlpha),
						FMath::Lerp(VertexWaterInfo[A.Index].WaterVelocity, VertexWaterInfo[B.Index].WaterVelocity, ABSplitAlpha),
						0.f
					});
					EdgeSplitVertices.Add(ABEdgeIndex, ABIndex);
				}

				int32 ACIndex;
				if (ExistingACIndex != nullptr)
				{
					ACIndex = *ExistingACIndex;
				}
				else
				{
					ACIndex = Result.VertexList.Emplace(FSubmergedTriangleArray::FVertex{
						FMath::Lerp(TriangleMesh.VertexList[A.Index], TriangleMesh.VertexList[C.Index], ACSplitAlpha),
						FMath::Lerp(VertexWaterInfo[A.Index].WaterVelocity, VertexWaterInfo[C.Index].WaterVelocity, ACSplitAlpha),
						0.f
					});
					EdgeSplitVertices.Add(ACEdgeIndex, ACIndex);
				}

				if (VerticesOverSurface.Num() == 2)
				{
					const int32 Indices[3] = { VertexSubmergedIndex[A.Index], ABIndex, ACIndex };
					Result.EmplaceTriangle(FSubmergedTriangleArray::FTriangle{
						{ Indices[A.VertexOrderIndex], Indices[B.VertexOrderIndex], Indices[C.VertexOrderIndex] }, 
						i / 3
					});
				}
				else
				{
					const int32 Indices1[3] = { ABIndex, VertexSubmergedIndex[B.Index], VertexSubmergedIndex[C.Index] }; 
					Result.EmplaceTriangle(FSubmergedTriangleArray::FTriangle{
						{ Indices1[A.VertexOrderIndex], Indices1[B.VertexOrderIndex], Indices1[C.VertexOrderIndex] }, 
						i / 3
					});

					const int32 Indices2[3] = { ABIndex, ACIndex, VertexSubmergedIndex[C.Index] };
					Result.EmplaceTriangle(FSubmergedTriangleArray::FTriangle{
						{ Indices2[B.VertexOrderIndex], Indices2[A.VertexOrderIndex], Indices2[C.VertexOrderIndex] }, 
						i / 3
					});
				}
			}
		}

		return Result;
	}

	FWaterSurfaceProvider::FVertexWaterInfoArray FetchVerticesWaterInfo(const UActorComponent* Component, const FVertexList& VertexList, 
		EWaterInfoFetchingMethod WaterInfoFetchingMethod, const FGetWaterInfoAtLocation& SurfaceGetter, FWaterSurfaceProvider* WaterSurfaceProvider)
	{
		FWaterSurfaceProvider::FVertexWaterInfoArray VertexWaterInfo;

		switch (WaterInfoFetchingMethod)
		{
		case EWaterInfoFetchingMethod::WaterSurfaceProvider:
		{
			checkf(WaterSurfaceProvider != nullptr, TEXT("A WaterSurfaceProvider is requred to use this water info fetching mode"));
			VertexWaterInfo = WaterSurfaceProvider->CalculateVerticesWaterInfo(VertexList, Component, SurfaceGetter);
			break;
		}
		case EWaterInfoFetchingMethod::PerVertex:
		{
			VertexWaterInfo.SetNum(VertexList.Num());
			for (int32 i = 0; i < VertexList.Num(); ++i)
				VertexWaterInfo[i] = SurfaceGetter.Execute(Component, VertexList[i]);
			break;
		}
		case EWaterInfoFetchingMethod::PerObject:
		{
			// Right now we can assume FetchVerticesWaterInfo is only called with a PrimitiveComponent
			// or a component which implements WaterPhysicsCollisionInterface.
			const FVector ComponentLocation = Component->Implements<UWaterPhysicsCollisionInterface>()
				? dynamic_cast<const IWaterPhysicsCollisionInterface*>(Component)->GetWaterPhysicsCollisionWorldTransform(NAME_None).GetLocation()
				: Cast<USceneComponent>(Component)->GetComponentLocation();

			VertexWaterInfo.SetNum(VertexList.Num());
			const FGetWaterInfoResult WaterSurface = SurfaceGetter.Execute(Component, ComponentLocation);
			for (FGetWaterInfoResult &WaterInfo : VertexWaterInfo)
				WaterInfo = WaterSurface;
			break;
		}
		default:
			checkf(0, TEXT("Water Surface Mode Not Implemented!"));
			break;
		}

		return VertexWaterInfo;
	}
};

using namespace WaterPhysics;

void FWaterPhysicsScene::StepWaterPhysicsScene(float DeltaTime, const FVector& Gravity, const FWaterPhysicsSettings& SceneSettings, 
	const FGetWaterInfoAtLocation& SurfaceGetter, bool bSurfaceGetterThreadSafe, FWaterSurfaceProvider* WaterSurfaceProvider, UObject* DebugContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StepWaterPhysics);
	SCOPED_OBJECT_DATA_CAPTURE(
		FString::Printf(TEXT("%s.%s"), (DebugContext ? *DebugContext->GetOuter()->GetName() : TEXT("None")), (DebugContext ? *DebugContext->GetName() : TEXT("None"))),
		TEXT("WaterPhysicsScene")
	);

	// Step 0: Gather up all the bodies we are about to process
	TArray<TPair<const UActorComponent*, FWaterPhysicsBody*>> BodiesToProcess;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoveInvalidComponents);

		for (auto It = WaterPhysicsBodies.CreateIterator(); It; ++It)
		{
			if (!IsValid(It.Key()))
				It.RemoveCurrent();
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CountBodies);

		for (auto& ComponentWaterPhysicsBodies : WaterPhysicsBodies)
		{
			for (FWaterPhysicsBody& WaterPhysicsBody : ComponentWaterPhysicsBodies.Value)
				BodiesToProcess.Emplace(ComponentWaterPhysicsBodies.Key, &WaterPhysicsBody);
		}
	}

	if (WaterSurfaceProvider)
		WaterSurfaceProvider->BeginStepScene();

	const bool bExecuteInParallel = bSurfaceGetterThreadSafe && (WaterSurfaceProvider ? WaterSurfaceProvider->SupportsParallelExecution() : true);

	if (bExecuteInParallel)
		StepWaterBodies_Parallel(BodiesToProcess, DeltaTime,Gravity, SceneSettings, SurfaceGetter, WaterSurfaceProvider);
	else
		StepWaterBodies_Synchronous(BodiesToProcess, DeltaTime,Gravity, SceneSettings, SurfaceGetter, WaterSurfaceProvider);

	if (WaterSurfaceProvider)
		WaterSurfaceProvider->EndStepScene();

	SwapBuffers();
}

void FWaterPhysicsScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(WaterPhysicsBodies);
}

FWaterPhysicsScene::FFrameInfo FWaterPhysicsScene::InitFrame(FWaterPhysicsBody& WaterPhysicsBody, const FIndexedTriangleMesh& TriangulatedBody, 
	const FSubmergedTriangleArray& SubmergedTriangles, const FVector& BodyCenterOfMass, const FVector& BodyLinearVelocity, const FVector& BodyAngularVelocity)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(InitBodyFrame);

	FFrameInfo FrameInfo(
		WaterPhysicsBody.PersistentTriangleData[GetFrameIndex(Frame_Current)], 
		WaterPhysicsBody.PersistentTriangleData[GetFrameIndex(Frame_Previous)]
	);

	FrameInfo.TriangleData.SetNumUninitialized(SubmergedTriangles.TriangleList.Num());
	FrameInfo.CurrentFrame.SetNumUninitialized(TriangulatedBody.IndexList.Num() / 3);
	FMemory::Memzero(FrameInfo.CurrentFrame.GetData(), sizeof(FrameInfo.CurrentFrame[0]) * FrameInfo.CurrentFrame.Num());

	for (int32 i = 0; i < SubmergedTriangles.TriangleList.Num(); ++i)
	{
		auto& TriangleData = FrameInfo.TriangleData[i];
		const auto& SubmergedTriangle = SubmergedTriangles.TriangleList[i];

		const FVector Vertices[3] = { 
			SubmergedTriangles.VertexList[SubmergedTriangle.Indices[0]].Position,
			SubmergedTriangles.VertexList[SubmergedTriangle.Indices[1]].Position,
			SubmergedTriangles.VertexList[SubmergedTriangle.Indices[2]].Position
		};
		const FVector WaterVelocities[3] = { 
			SubmergedTriangles.VertexList[SubmergedTriangle.Indices[0]].WaterVelocity * 0.01f /* cm/s -> m/s */,
			SubmergedTriangles.VertexList[SubmergedTriangle.Indices[1]].WaterVelocity * 0.01f /* cm/s -> m/s */,
			SubmergedTriangles.VertexList[SubmergedTriangle.Indices[2]].WaterVelocity * 0.01f /* cm/s -> m/s */
		};
		const float Depths[3] = { 
			SubmergedTriangles.VertexList[SubmergedTriangle.Indices[0]].Depth * 0.01f /* cm -> m */,
			SubmergedTriangles.VertexList[SubmergedTriangle.Indices[1]].Depth * 0.01f /* cm -> m */,
			SubmergedTriangles.VertexList[SubmergedTriangle.Indices[2]].Depth * 0.01f /* cm -> m */
		};

		const FVector OriginalTriangleVertices[3] = { 
			TriangulatedBody.VertexList[TriangulatedBody.IndexList[SubmergedTriangle.OriginalTriangleIndex * 3 + 0]],
			TriangulatedBody.VertexList[TriangulatedBody.IndexList[SubmergedTriangle.OriginalTriangleIndex * 3 + 1]],
			TriangulatedBody.VertexList[TriangulatedBody.IndexList[SubmergedTriangle.OriginalTriangleIndex * 3 + 2]],
		};

		TriangleData.Centroid              = CalcTriangleCentroid(Vertices);
		TriangleData.Normal                = CalcTriangleNormal(OriginalTriangleVertices); // Submerged triangle might be too small for accurate calculation, use OriginalTriangleVertices
		TriangleData.Area                  = FMath::Max(CalcTriangleAreaM2(Vertices), 0.001f);
		TriangleData.AvgDepth              = CalcTriangleElemAvg(Depths);
		TriangleData.Velocity              = CalcVertexVelocityMS(TriangleData.Centroid, BodyCenterOfMass, BodyLinearVelocity, BodyAngularVelocity) - CalcTriangleElemAvg(WaterVelocities);
		TriangleData.VelocitySizeSquared   = TriangleData.Velocity.SizeSquared();
		const float VelocityInvSqrtSize    = TriangleData.VelocitySizeSquared > SMALL_NUMBER ? FMath::InvSqrt(TriangleData.VelocitySizeSquared) : 0.f;
		TriangleData.VelocitySize          = TriangleData.VelocitySizeSquared * VelocityInvSqrtSize;
		TriangleData.VelocityNormal        = TriangleData.Velocity * VelocityInvSqrtSize;
		TriangleData.VelocityNormalDot     = FVector::DotProduct(TriangleData.VelocityNormal, TriangleData.Normal);
		TriangleData.OriginalTriangleIndex = SubmergedTriangle.OriginalTriangleIndex;

		FrameInfo.AvgFluidVelocity -= TriangleData.Velocity;
		FrameInfo.TotalSubmergedArea += TriangleData.Area;
	}

	if (FrameInfo.PreviousFrame.Num() != FrameInfo.CurrentFrame.Num())
	{
		FrameInfo.PreviousFrame.SetNumUninitialized(FrameInfo.CurrentFrame.Num());
		FMemory::Memcpy(FrameInfo.PreviousFrame.GetData(), FrameInfo.CurrentFrame.GetData(), sizeof(FrameInfo.CurrentFrame[0]) * FrameInfo.CurrentFrame.Num());
	}

	EXEC_WITH_WATER_PHYS_DEBUG(
	{
		for (const auto& TriangleData : FrameInfo.TriangleData) {
			FrameInfo.bSuccess &= TriangleData.IsValid();
		}
	});

	return FrameInfo;
}

FWaterPhysicsScene::FWaterBodyProcessingResult FWaterPhysicsScene::ProcessWaterPhysicsBody(const UActorComponent* Component, FWaterPhysicsBody& WaterBody, const FWaterPhysicsSettings& SceneSettings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProcessWaterPhysicsBody);

	FWaterBodyProcessingResult Result;

	const UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
	Result.bHasWaterPhysicsCollisionInterface     = Component->Implements<UWaterPhysicsCollisionInterface>();

	checkf(
		Result.bHasWaterPhysicsCollisionInterface || PrimitiveComponent != nullptr, 
		TEXT("Tried to step water physics scene with component %s which does not implement WaterPhysicsCollisionInterface and is not a PrimitiveComponent."),
		*Component->GetName()
	);

	Result.BodyInstance = Result.bHasWaterPhysicsCollisionInterface
		? dynamic_cast<const IWaterPhysicsCollisionInterface*>(Component)->GetWaterPhysicsCollisionBodyInstance(WaterBody.BodyName, false)
		: PrimitiveComponent->GetBodyInstance(WaterBody.BodyName, false);

	const bool bIsWelded = Result.BodyInstance && Result.BodyInstance->WeldParent != nullptr;

	if (!Result.BodyInstance 
		|| (bIsWelded && !Result.BodyInstance->WeldParent->IsInstanceSimulatingPhysics())
		|| (!bIsWelded && !Result.BodyInstance->IsInstanceSimulatingPhysics()))
	{
		WaterBody.ClearTriangleData(); // Clear triangle data in case some body has disabled physics/collison/been destroyed
		Result.BodyInstance = nullptr;
		return Result;
	}

	Result.WaterPhysicsSettings = FWaterPhysicsSettings::MergeWaterPhysicsSettings(SceneSettings, WaterBody.WaterPhysicsSettings);

	return Result;
}

FWaterPhysicsScene::FBodyTriangulationResult FWaterPhysicsScene::TriangulateBody(const UActorComponent* Component, const FWaterPhysicsBody& WaterBody, 
	const FWaterBodyProcessingResult& BodyProcessingResult)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TriangulateBody);

	FBodyTriangulationResult Result;

	Result.BodyProcessingResult = &BodyProcessingResult;

	if (BodyProcessingResult.BodyInstance == nullptr)
		return Result;

	const FWaterPhysicsCollisionSetup CollisionSetup = BodyProcessingResult.bHasWaterPhysicsCollisionInterface 
		? GenerateWaterPhysicsCollisionSetup(dynamic_cast<const IWaterPhysicsCollisionInterface*>(Component), WaterBody.BodyName)
		: GenerateBodyInstanceWaterPhysicsCollisionSetup(BodyProcessingResult.BodyInstance, false);

	Result.TriangulatedBody = TriangulateWaterPhysicsCollisionSetup(CollisionSetup, BodyProcessingResult.WaterPhysicsSettings.SubdivisionSettings);

	return Result;
}

FWaterPhysicsScene::FFetchWaterSurfaceInfoResult FWaterPhysicsScene::FetchWaterSurfaceInfo(const UActorComponent* Component, const FWaterPhysicsBody& WaterBody, 
	const FBodyTriangulationResult& BodyTriangulationResult, EWaterInfoFetchingMethod WaterInfoFetchingMethod, const FGetWaterInfoAtLocation& SurfaceGetter, FWaterSurfaceProvider* WaterSurfaceProvider)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FetchWaterSurfaceInfo);

	FFetchWaterSurfaceInfoResult Result;
	Result.BodyProcessingResult    = BodyTriangulationResult.BodyProcessingResult;
	Result.BodyTriangulationResult = &BodyTriangulationResult;
	Result.VertexWaterInfo = FetchVerticesWaterInfo(Component, BodyTriangulationResult.TriangulatedBody.VertexList, WaterInfoFetchingMethod, SurfaceGetter, WaterSurfaceProvider);
	return Result;
}

FWaterPhysicsScene::FBodyWaterIntersectionResult FWaterPhysicsScene::BodyWaterIntersection(const FFetchWaterSurfaceInfoResult& FetchWaterSurfaceInfoResult)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BodyWaterIntersection);

	FBodyWaterIntersectionResult Result;
	Result.BodyProcessingResult        = FetchWaterSurfaceInfoResult.BodyProcessingResult;
	Result.BodyTriangulationResult     = FetchWaterSurfaceInfoResult.BodyTriangulationResult;
	Result.FetchWaterSurfaceInfoResult = &FetchWaterSurfaceInfoResult;
	Result.SubmergedTriangleArray = PerformTriangleMeshWaterIntersection(FetchWaterSurfaceInfoResult.VertexWaterInfo, FetchWaterSurfaceInfoResult.BodyTriangulationResult->TriangulatedBody);
	return Result;
}

void FWaterPhysicsScene::CalculateWaterForces(const UActorComponent* Component, FWaterPhysicsBody& WaterBody, 
	const FBodyWaterIntersectionResult& BodyWaterIntersectionResult, float DeltaTime, const FVector& Gravity)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CalculateWaterForces);

	const FIndexedTriangleMesh&    TriangulatedBody   = BodyWaterIntersectionResult.BodyTriangulationResult->TriangulatedBody;
	const FSubmergedTriangleArray& SubmergedTriangles = BodyWaterIntersectionResult.SubmergedTriangleArray;
	const FWaterPhysicsSettings&   Settings           = BodyWaterIntersectionResult.BodyProcessingResult->WaterPhysicsSettings;
	FBodyInstance*                 BodyInstance       = BodyWaterIntersectionResult.BodyProcessingResult->BodyInstance->WeldParent 
															? BodyWaterIntersectionResult.BodyProcessingResult->BodyInstance->WeldParent 
															: BodyWaterIntersectionResult.BodyProcessingResult->BodyInstance;

	check(IsValid(Component) && BodyInstance);

	SCOPED_OBJECT_DATA_CAPTURE(Component->GetOwner()
		? *FString::Printf(TEXT("%s.%s"), *Component->GetOwner()->GetName(), *Component->GetName())
		: *Component->GetName(), TEXT("WaterPhysicsBody"));

	DEBUG_CAPTURE_USTRUCT("Water Physics Settings", Settings);

	// Avoid acquiring expensive PhysicsLock more than we need to
	FVector    BodyLinearVelocity;
	FVector    BodyAngularVelocity;
	FVector    BodyCenterOfMass;
	float      BodyMass;
	FVector    BodyInertiaTensor;
	FTransform BodyTransform(NoInit);
	FPhysicsCommand::ExecuteRead(BodyInstance->GetPhysicsActorHandle(), [&](const FPhysicsActorHandle& ActorHandle)
	{
		BodyLinearVelocity  = FPhysicsInterface::GetLinearVelocity_AssumesLocked(ActorHandle);
		BodyAngularVelocity = FPhysicsInterface::GetAngularVelocity_AssumesLocked(ActorHandle);
		BodyCenterOfMass    = FPhysicsInterface::GetComTransform_AssumesLocked(ActorHandle).GetLocation();
		BodyMass            = FPhysicsInterface::GetMass_AssumesLocked(ActorHandle);
		BodyInertiaTensor   = FPhysicsInterface::GetLocalInertiaTensor_AssumesLocked(ActorHandle);
		BodyTransform       = BodyInstance->GetUnrealWorldTransform_AssumesLocked();
	});

	DEBUG_CAPTURE_STRING("BodyLinearVelocity", BodyLinearVelocity.ToString());
	DEBUG_CAPTURE_STRING("BodyAngularVelocity", BodyAngularVelocity.ToString());
	DEBUG_CAPTURE_STRING("BodyCenterOfMass", BodyCenterOfMass.ToString());
	DEBUG_CAPTURE_STRING("BodyInertiaTensor", BodyInertiaTensor.ToString());
	DEBUG_CAPTURE_STRING("BodyTransform", BodyTransform.ToString());
	DEBUG_CAPTURE_NUMBER("BodyMass", BodyMass);

	const auto PersistantBodyFrame = InitFrame(WaterBody, TriangulatedBody, SubmergedTriangles, BodyCenterOfMass, BodyLinearVelocity, BodyAngularVelocity);

	if (!PersistantBodyFrame.bSuccess)
	{
		UE_LOG(LogWaterPhysics, Error, TEXT("Failed to init frame for component %s"), *Component->GetName());
		return;
	}

	WaterBody.SubmergedArea = PersistantBodyFrame.TotalSubmergedArea;

	UWorld* World = Component->GetWorld();

	// Debug Draw submersion
	EXEC_WITH_WATER_PHYS_DEBUG(([&]()
	{
		if (Settings.DebugSubmersion > EWaterPhysicsDebugLevel::None)
		{
			EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD(([=]()
			{
				for (int32 i = 0; i < SubmergedTriangles.TriangleList.Num(); ++i)
				{
					const FVector Vertices[3] = {
						SubmergedTriangles.VertexList[SubmergedTriangles.TriangleList[i].Indices[0]].Position,
						SubmergedTriangles.VertexList[SubmergedTriangles.TriangleList[i].Indices[1]].Position,
						SubmergedTriangles.VertexList[SubmergedTriangles.TriangleList[i].Indices[2]].Position
					};

					DrawDebugTriangle(World, Vertices, Settings.DebugSubmersion > EWaterPhysicsDebugLevel::Normal, FColor::Red, false, 0.f, -1, 3.f);
				}
			}));
		}

		if (Settings.DebugTriangleData > EWaterPhysicsDebugLevel::None)
		{
			EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD(([=]()
			{
				for (int32 i = 0; i < TriangulatedBody.IndexList.Num(); i += 3)
				{
					const FVector Vertices[3] = {
						TriangulatedBody.VertexList[TriangulatedBody.IndexList[i + 0]],
						TriangulatedBody.VertexList[TriangulatedBody.IndexList[i + 1]],
						TriangulatedBody.VertexList[TriangulatedBody.IndexList[i + 2]]
					};

					DrawDebugTriangle(World, Vertices, Settings.DebugTriangleData > EWaterPhysicsDebugLevel::Normal, FColor::Yellow, false, 0.f, -1, 1.5f);
				}
			}));
		}

		if (Settings.DebugFluidVelocity > EWaterPhysicsDebugLevel::None)
		{
			EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD(([=]()
			{
				DrawDebugLine(World, BodyCenterOfMass, BodyCenterOfMass + PersistantBodyFrame.AvgFluidVelocity * 100.f, FColor::Green, false, 0.f, -1, 4);

				if (Settings.DebugFluidVelocity > EWaterPhysicsDebugLevel::Normal)
				{
					for (int32 i = 0; i < SubmergedTriangles.VertexList.Num(); ++i)
					{
						const auto& Vertex = SubmergedTriangles.VertexList[i];
						DrawDebugLine(World, Vertex.Position, Vertex.Position + Vertex.WaterVelocity, FColor::Green, false, 0.f, -1, 2);
					}
				}
			}));
		}
	}()));

	FForce TotalWaterPhysicsForce = FForce(ForceInit);

	// BuoyancyForce
	FForce TotalBuoyancyForce(ForceInit);
	if (Settings.bEnableBuoyancyForce)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CalcBuoyancy);
		SCOPED_OBJECT_DATA_CAPTURE(TEXT("Buoyancy"), TEXT("Buoyancy"));
		
		for (const auto& TriangleData : PersistantBodyFrame.TriangleData)
		{
			// NOTE: We do not multiply with 100 (N -> cN) since Gravity is supplied in cm/s instead of m/s
			const auto BuoyancyForce = Gravity * TriangleData.AvgDepth * TriangleData.Area * Settings.FluidDensity * TriangleData.Normal;
			TotalBuoyancyForce.AddForce(BuoyancyForce, TriangleData.Centroid, BodyCenterOfMass);
			
			EXEC_WITH_WATER_PHYS_DEBUG(
			{
				SCOPED_OBJECT_DATA_CAPTURE("Triangle Force", TEXT("Buoyancy"), BuoyancyForce.Size() / 2000.f);
				DEBUG_CAPTURE_NUMBER("Area", TriangleData.Area);
				DEBUG_CAPTURE_NUMBER("AvgDepth", TriangleData.AvgDepth);
				DEBUG_CAPTURE_STRING("Force", BuoyancyForce.ToString());
				DEBUG_CAPTURE_STRING("Torque", FVector::CrossProduct(TriangleData.Centroid - BodyCenterOfMass, BuoyancyForce).ToString());

				if (Settings.DebugBuoyancyForce > EWaterPhysicsDebugLevel::Normal)
				{
					EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD([=]()
					{
						DrawDebugLine(World, TriangleData.Centroid, TriangleData.Centroid + BuoyancyForce / 1000.f, FColor::Yellow, false, 0.f, -1, 3);
					});
				}
			});
		}

		EXEC_WITH_WATER_PHYS_DEBUG(
		{
			DEBUG_CAPTURE_STRING("GravityZ", Gravity.ToString());
			DEBUG_CAPTURE_STRING("Force", TotalBuoyancyForce.Force.ToString());
			DEBUG_CAPTURE_STRING("Torque", TotalBuoyancyForce.Torque.ToString());

			if (Settings.DebugBuoyancyForce > EWaterPhysicsDebugLevel::None)
			{
				EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD([=]()
				{
					DrawDebugLine(World, TotalBuoyancyForce.AvgLocation, TotalBuoyancyForce.AvgLocation + TotalBuoyancyForce.Force / 100.f, FColor::Yellow, false, 0.f, -1, 3);
				});
			}
		});

		TotalWaterPhysicsForce += TotalBuoyancyForce;
	}
	WaterBody.ActingForces.BuoyancyForce = TotalBuoyancyForce.Force;
	WaterBody.ActingForces.BuoyancyTorque = TotalBuoyancyForce.Torque;

	// Viscous Fluid Resistance
	FForce TotalResistanceForce(ForceInit);
	if (Settings.bEnableViscousFluidResistance)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CalcViscousFluidResistance);
		SCOPED_OBJECT_DATA_CAPTURE(TEXT("Viscous Fluid Resistance"), TEXT("Viscosity"));

		#define USE_PER_TRIANGLE_CF 0

		#if !USE_PER_TRIANGLE_CF
		// We're using AvgFluidVelocity which is the sum total velocities of the fluid over all the triangles.
		// A more accurate value might be to sum the "relative" velocities between the water and the triangles.
		const FVector RelativeVelocity     = (BodyLinearVelocity  * 0.01f /* cm/s -> m/s */) - PersistantBodyFrame.AvgFluidVelocity;
		const float   RelativeVelocitySize = RelativeVelocity.Size();

		const float FluidTravelLength = [&]()
		{
			const FVector RelativeVelocityNormal = FMath::IsNearlyZero(RelativeVelocitySize) ? FVector::UpVector : RelativeVelocity.GetSafeNormal();
			float MinVertexDistanceToVelocityPlane = BIG_NUMBER;
			float MaxVertexDistanceToVelocityPlane = -BIG_NUMBER;
			for (const auto& Vertex : SubmergedTriangles.VertexList)
			{
				const float DistToVelocityPlane = FVector::PointPlaneDist(Vertex.Position, BodyCenterOfMass, RelativeVelocityNormal);
				MinVertexDistanceToVelocityPlane = FMath::Min(DistToVelocityPlane, MinVertexDistanceToVelocityPlane);
				MaxVertexDistanceToVelocityPlane = FMath::Max(DistToVelocityPlane, MaxVertexDistanceToVelocityPlane);
			}
			return FMath::Max(1.f, MaxVertexDistanceToVelocityPlane - MinVertexDistanceToVelocityPlane) * 0.01f /* cm -> m */;
		}();

		// Slight modification of ITTC 1957 model-ship correlation line for calculating coefficient of drag on a plate draged under water as a function of velocity and plate length.
		// This version shifts the graph 100 to the left to have it tend towards inf at 0, then clamp at 5 to avoid exploding bodies.
		// Approximation of reynolds number using the velocity of the fluid and the travel length of the fluid along the body.
		const float Rn = (RelativeVelocitySize * FluidTravelLength) / (Settings.FluidKinematicViscocity * 0.000001f /* centistokes -> m2/s */);
		const float Denominator = FMath::LogX(10.f, FMath::Max(5.f, Rn) + 100.f) - 2.f;
		const float Cf = 0.075f / (Denominator * Denominator);
		#endif
		
		for (const auto& TriangleData : PersistantBodyFrame.TriangleData)
		{
			const FVector TangentalVelocity            = FVector::VectorPlaneProject(TriangleData.Velocity, TriangleData.Normal);
			const float   TangentalVelocitySizeSquared = TriangleData.Velocity.SizeSquared();
			const float   InverseTangentalVelocitySize = (TangentalVelocitySizeSquared > SMALL_NUMBER ? FMath::InvSqrt(TangentalVelocitySizeSquared) : 0.f);
			const float   TangentalVelocitySize        = TangentalVelocitySizeSquared * InverseTangentalVelocitySize;
			const FVector TangentalVelocityNormal      = InverseTangentalVelocitySize * TangentalVelocity;

			#if USE_PER_TRIANGLE_CF
			float MaxVertexDistanceToVelocityPlane = -BIG_NUMBER;
			for (const auto& Vertex : SubmergedTriangles.VertexList)
			{
				const float DistToVelocityPlane = FVector::PointPlaneDist(Vertex.Position, TriangleData.Centroid, TangentalVelocityNormal);
				MaxVertexDistanceToVelocityPlane = FMath::Max(DistToVelocityPlane, MaxVertexDistanceToVelocityPlane);
			}
			const float FluidTravelLength = FMath::Max(1.f, MaxVertexDistanceToVelocityPlane) * 0.01f /* cm -> m */;
			
			// Slight modification of ITTC 1957 model-ship correlation line for calculating coefficient of drag on a plate draged under water as a function of velocity and plate length.
			// This version shifts the graph 100 to the left to have it tend towards inf at 0, then clamp at 5 to avoid exploding bodies.
			// Approximation of reynolds number using the velocity of the fluid and the travel length of the fluid along the body.
			const float Rn = (TangentalVelocitySize * FluidTravelLength) / (Settings.FluidKinematicViscocity * 0.000001f /* centistokes -> m2/s */);
			const float Denominator = FMath::LogX(10.f, FMath::Max(5.f, Rn) + 100.f) - 2.f;
			const float Cf = 0.075f / (Denominator * Denominator);
			#endif

			const FVector ResistanceForce = 0.5f * Settings.FluidDensity * Cf * TriangleData.Area * -TangentalVelocityNormal * TangentalVelocitySizeSquared * 100.f /* N -> cN */;
			TotalResistanceForce.AddForce(ResistanceForce, TriangleData.Centroid, BodyCenterOfMass);

			EXEC_WITH_WATER_PHYS_DEBUG(
			{
				SCOPED_OBJECT_DATA_CAPTURE("Triangle Force", TEXT("Viscosity"), ResistanceForce.Size() / 2000.f);
				DEBUG_CAPTURE_NUMBER("FluidTravelLength", FluidTravelLength);
				DEBUG_CAPTURE_NUMBER("Rn", Rn);
				DEBUG_CAPTURE_NUMBER("Cf", Cf);
				DEBUG_CAPTURE_NUMBER("Area", TriangleData.Area);
				DEBUG_CAPTURE_NUMBER("TangentalVelocitySize", TangentalVelocitySize);
				DEBUG_CAPTURE_STRING("Force", ResistanceForce.ToString());
				DEBUG_CAPTURE_STRING("Torque", FVector::CrossProduct(TriangleData.Centroid - BodyCenterOfMass, ResistanceForce).ToString());

				if (Settings.DebugViscousFluidResistance > EWaterPhysicsDebugLevel::Normal)
				{
					EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD([=]()
					{
						DrawDebugLine(World, TriangleData.Centroid, TriangleData.Centroid + ResistanceForce / 1000.f, FColor::Yellow, false, 0.f, -1, 3);
					});
				}
			});
		}

		EXEC_WITH_WATER_PHYS_DEBUG(
		{
			DEBUG_CAPTURE_STRING("Force", TotalResistanceForce.Force.ToString());
			DEBUG_CAPTURE_STRING("Torque", TotalResistanceForce.Torque.ToString());

			if (Settings.DebugViscousFluidResistance > EWaterPhysicsDebugLevel::None)
			{
				EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD([=]()
				{
					DrawDebugLine(World, TotalResistanceForce.AvgLocation, TotalResistanceForce.AvgLocation + TotalResistanceForce.Force / 1000.f, FColor::Yellow, false, 0.f, -1, 3);
				});
			}
		});

		TotalWaterPhysicsForce += TotalResistanceForce;
	}
	WaterBody.ActingForces.ViscousFluidResistanceForce = TotalResistanceForce.Force;
	WaterBody.ActingForces.ViscousFluidResistanceTorque = TotalResistanceForce.Torque;

	// Pressure Drag Forces
	FForce TotalPressureDragForce(ForceInit);
	if (Settings.bEnablePressureDragForce)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CalcPressureDrag);
		SCOPED_OBJECT_DATA_CAPTURE(TEXT("Pressure Drag"), TEXT("PressureDrag"));

		struct FPreassureDragParams
		{
			float C1;
			float C2;
			float F;
			int32 Dir;
		};

		const FPreassureDragParams PressureDragParams =
		{ 
			Settings.PressureCoefficientOfLinearSpeed, 
			Settings.PressureCoefficientOfExponentialSpeed, 
			Settings.PressureAngularDependence, 
			-1 
		};

		const FPreassureDragParams SuctionDragParams = 
		{ 
			Settings.SuctionCoefficientOfLinearSpeed,
			Settings.SuctionCoefficientOfExponentialSpeed,
			Settings.SuctionAngularDependence,
			1
		};

		for (const auto& TriangleData : PersistantBodyFrame.TriangleData)
		{
			const float ReferenceVelocityRatio = TriangleData.VelocitySize / Settings.DragReferenceSpeed;
			const FPreassureDragParams& P      = TriangleData.VelocityNormalDot > 0.f ? PressureDragParams : SuctionDragParams;
			const auto DragForce               = P.Dir * (P.C1 * ReferenceVelocityRatio + P.C2 * ReferenceVelocityRatio * ReferenceVelocityRatio) 
				* TriangleData.Area * FMath::Pow(FMath::Abs(TriangleData.VelocityNormalDot), P.F) * TriangleData.Normal * 100.f /* N -> cN */;
			TotalPressureDragForce.AddForce(DragForce, TriangleData.Centroid, BodyCenterOfMass);

			EXEC_WITH_WATER_PHYS_DEBUG(
			{
				SCOPED_OBJECT_DATA_CAPTURE("Triangle Force", TEXT("PressureDrag"), DragForce.Size() / 2000.f);
				DEBUG_CAPTURE_NUMBER("Area", TriangleData.Area);
				DEBUG_CAPTURE_NUMBER("ReferenceVelocityRatio", ReferenceVelocityRatio);
				DEBUG_CAPTURE_STRING("Force", DragForce.ToString());
				DEBUG_CAPTURE_STRING("Torque", FVector::CrossProduct(TriangleData.Centroid - BodyCenterOfMass, DragForce).ToString());

				if (Settings.DebugPressureDragForce > EWaterPhysicsDebugLevel::Normal)
				{
					EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD([=]()
					{
						DrawDebugLine(World, TriangleData.Centroid, TriangleData.Centroid + DragForce / 1000.f, FColor::Yellow, false, 0.f, -1, 3);
					});
				}
			});
		}

		if (Settings.bEnableForceClamping)
		{
			const static auto ClampForce = [](FVector& InForce, const FVector& InMaxForce)
			{
				InForce.X = InForce.X > 0.f 
					? FMath::Min(InForce.X, FMath::Max(0.f, -InMaxForce.X))
					: FMath::Max(InForce.X, FMath::Min(0.f, -InMaxForce.X));
				InForce.Y = InForce.Y > 0.f 
					? FMath::Min(InForce.Y, FMath::Max(0.f, -InMaxForce.Y))
					: FMath::Max(InForce.Y, FMath::Min(0.f, -InMaxForce.Y));
				InForce.Z = InForce.Z > 0.f 
					? FMath::Min(InForce.Z, FMath::Max(0.f, -InMaxForce.Z))
					: FMath::Max(InForce.Z, FMath::Min(0.f, -InMaxForce.Z));
			};

			const FVector BodyLinearMomentum  = BodyMass * BodyLinearVelocity / DeltaTime;
			const FVector WorldSpaceTensor    = [&]()
			{
				const auto TensorMatrix        = FMatrix(FVector(BodyInertiaTensor.X, 0, 0), FVector(0, BodyInertiaTensor.Y, 0), FVector(0, 0, BodyInertiaTensor.Z), FVector(0, 0, 0));
				const auto RotationMatrix      = FRotationMatrix::Make(BodyTransform.GetRotation());
				const auto RotatedTensorMatrix = RotationMatrix * TensorMatrix * RotationMatrix.Inverse();
				return FVector(RotatedTensorMatrix.M[0][0], RotatedTensorMatrix.M[1][1], RotatedTensorMatrix.M[2][2]);
			}();
			const FVector BodyAngularMomentum = WorldSpaceTensor * BodyAngularVelocity / DeltaTime;

			// Clamp Linear and angular forces
			ClampForce(TotalPressureDragForce.Force, BodyLinearMomentum);
			ClampForce(TotalPressureDragForce.Torque, BodyAngularMomentum);
		}

		EXEC_WITH_WATER_PHYS_DEBUG(
		{
			DEBUG_CAPTURE_STRING("Force", TotalPressureDragForce.Force.ToString());
			DEBUG_CAPTURE_STRING("Torque", TotalPressureDragForce.Torque.ToString());

			if (Settings.DebugPressureDragForce > EWaterPhysicsDebugLevel::None)
			{
				EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD([=]()
				{
					DrawDebugLine(World, TotalPressureDragForce.AvgLocation,
						TotalPressureDragForce.AvgLocation + TotalPressureDragForce.Force / 1000.f, FColor::Yellow, false, 0.f, -1, 3);
				});
			}
		});

		TotalWaterPhysicsForce += TotalPressureDragForce;
	}
	WaterBody.ActingForces.PressureDragForce = TotalPressureDragForce.Force;
	WaterBody.ActingForces.PressureDragTorque = TotalPressureDragForce.Torque;

	// Slamming Force
	FForce TotalSlammingForce(ForceInit);
	if (Settings.bEnableSlammingForce)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CalcSlammingForce);
		SCOPED_OBJECT_DATA_CAPTURE(TEXT("Slamming Force"), TEXT("SlammingForce"));

		const float TotalBodyArea = [&]()
		{
			float AggArea = 0.f;
			for (int32 i = 0; i < TriangulatedBody.IndexList.Num(); i+=3)
			{
				const FVector* Vertices[3] = {
					&TriangulatedBody.VertexList[TriangulatedBody.IndexList[i+0]],
					&TriangulatedBody.VertexList[TriangulatedBody.IndexList[i+1]],
					&TriangulatedBody.VertexList[TriangulatedBody.IndexList[i+2]]
				};
				AggArea += CalcTriangleAreaM2(Vertices);
			}
			return AggArea;
		}();

		for (const auto& TriangleData : PersistantBodyFrame.TriangleData)
		{
			PersistantBodyFrame.CurrentFrame[TriangleData.OriginalTriangleIndex].SweptWaterArea += TriangleData.VelocityNormalDot > 0.f 
				? TriangleData.Area * TriangleData.VelocitySize
				: 0.f;
		}

		for (const auto& TriangleData : PersistantBodyFrame.TriangleData)
		{
			if (TriangleData.VelocityNormalDot <= 0.f) // Triangle is receding from the water, no stopping force
				continue;

			const float   CurrSweptWaterVolume = PersistantBodyFrame.CurrentFrame[TriangleData.OriginalTriangleIndex].SweptWaterArea;
			const float   PrevSweptWaterVolume = PersistantBodyFrame.PreviousFrame[TriangleData.OriginalTriangleIndex].SweptWaterArea;
			const float   FlowAcceleration     = (CurrSweptWaterVolume - PrevSweptWaterVolume) / (TriangleData.Area * DeltaTime);
			const FVector StoppingForce        = BodyMass * -TriangleData.Velocity * (2.f * TriangleData.Area / TotalBodyArea);
			const FVector SlammingForce        = FMath::Clamp(FMath::Pow(FlowAcceleration / Settings.MaxSlammingForceAtAcceleration, Settings.SlammingForceExponent), 0.f, 1.f) 
				* TriangleData.VelocityNormalDot * StoppingForce * 100.f /* N -> cN */;
			TotalSlammingForce.AddForce(SlammingForce, TriangleData.Centroid, BodyCenterOfMass);

			EXEC_WITH_WATER_PHYS_DEBUG(
			{
				SCOPED_OBJECT_DATA_CAPTURE("Triangle Force", TEXT("SlammingForce"), SlammingForce.Size() / 2000.f);
				DEBUG_CAPTURE_NUMBER("CurrSweptWaterVolume", CurrSweptWaterVolume);
				DEBUG_CAPTURE_NUMBER("PrevSweptWaterVolume", PrevSweptWaterVolume);
				DEBUG_CAPTURE_NUMBER("FlowAcceleration", FlowAcceleration);
				DEBUG_CAPTURE_STRING("Force", SlammingForce.ToString());
				DEBUG_CAPTURE_STRING("Torque", FVector::CrossProduct(TriangleData.Centroid - BodyCenterOfMass, SlammingForce).ToString());

				if (Settings.DebugSlammingForce > EWaterPhysicsDebugLevel::Normal)
				{
					EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD([=]()
					{
						DrawDebugLine(World, TriangleData.Centroid, TriangleData.Centroid + SlammingForce / 1000.f, FColor::Yellow, false, 0.f, -1, 3);
					});
				}
			});
		}

		EXEC_WITH_WATER_PHYS_DEBUG(
		{
			DEBUG_CAPTURE_STRING("Force", TotalSlammingForce.Force.ToString());
			DEBUG_CAPTURE_STRING("Torque", TotalSlammingForce.Torque.ToString());

			if (Settings.DebugSlammingForce > EWaterPhysicsDebugLevel::None)
			{
				EXEC_WITH_WATER_PHYS_DEBUG_ON_GAME_THREAD([=]()
				{
					DrawDebugLine(World, TotalSlammingForce.AvgLocation,
						TotalSlammingForce.AvgLocation + TotalSlammingForce.Force / 1000.f, FColor::Yellow, false, 0.f, -1, 3);
				});
			}
		});

		TotalWaterPhysicsForce += TotalSlammingForce;
	}
	WaterBody.ActingForces.SlammingForce = TotalSlammingForce.Force;
	WaterBody.ActingForces.SlammingTorque = TotalSlammingForce.Torque;

	BodyInstance->AddForce(TotalWaterPhysicsForce.Force, false);
	BodyInstance->AddTorqueInRadians(TotalWaterPhysicsForce.Torque, false);
}

void FWaterPhysicsScene::StepWaterBodies_Synchronous(TArray<TPair<const UActorComponent*, FWaterPhysicsBody*>>& WaterBodies, float DeltaTime, const FVector& Gravity, 
	const FWaterPhysicsSettings& SceneSettings, const FGetWaterInfoAtLocation& SurfaceGetter, FWaterSurfaceProvider* WaterSurfaceProvider)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StepWaterBodies_Synchronous);

	// This function splits the workload up in segments which can be run in parallel and in to those which has to be run on the GameThread.
	// Right now the only part which has to run on the game thread is the surface information fetching as we cannot know what it does in the SurfaceGetter.

	// Step 1: ProcessWaterPhysicsBody and TriangulateBody - Parallel
	TArray<FWaterBodyProcessingResult> WaterBodyProcessingResults;
	TArray<FBodyTriangulationResult>   BodyTriangulationResult;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProcessBodies);

		WaterBodyProcessingResults.SetNum(WaterBodies.Num());
		BodyTriangulationResult.SetNum(WaterBodies.Num());

		ParallelFor(WaterBodies.Num(), [&](int32 Index)
		{
			WaterBodyProcessingResults[Index] = ProcessWaterPhysicsBody(WaterBodies[Index].Key, *WaterBodies[Index].Value, SceneSettings);
			BodyTriangulationResult[Index] = TriangulateBody(WaterBodies[Index].Key, *WaterBodies[Index].Value, WaterBodyProcessingResults[Index]);
		});

		// Minor optimization, don't continue with bodies which don't have any triangulation
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ClearInvalidResults);

			for (int32 i = 0; i < BodyTriangulationResult.Num(); i++)
			{
				if (BodyTriangulationResult[i].TriangulatedBody.IndexList.Num() == 0)
				{
					BodyTriangulationResult.RemoveAtSwap(i, 1, EAllowShrinking::No);
					WaterBodies.RemoveAtSwap(i, 1, EAllowShrinking::No);
					i--;
				}
			}
		}
	}

	// Step 2: FetchWaterSurfaceInfo - Synchronous
	TArray<FFetchWaterSurfaceInfoResult> WaterSurfaceIntersectionResults;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FetchWaterSurfaceInfo);

		WaterSurfaceIntersectionResults.SetNum(BodyTriangulationResult.Num());
		for (int32 Index = 0; Index < BodyTriangulationResult.Num(); ++Index)
		{
			WaterSurfaceIntersectionResults[Index] = FetchWaterSurfaceInfo(WaterBodies[Index].Key, *WaterBodies[Index].Value, BodyTriangulationResult[Index], 
				WaterBodyProcessingResults[Index].WaterPhysicsSettings.WaterInfoFetchingMethod, SurfaceGetter, WaterSurfaceProvider);
		}
	}

	// Step 3: BodyWaterIntersection and CalculateWaterForces - Parallel
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CalculateWaterForces);

		ParallelFor(WaterSurfaceIntersectionResults.Num(), [&](int32 Index)
		{
			const auto BodyWaterIntersectionResult = BodyWaterIntersection(WaterSurfaceIntersectionResults[Index]);
			CalculateWaterForces(WaterBodies[Index].Key, *WaterBodies[Index].Value, BodyWaterIntersectionResult, DeltaTime, Gravity);
		});
	}
}

void FWaterPhysicsScene::StepWaterBodies_Parallel(TArray<TPair<const UActorComponent*, FWaterPhysicsBody*>>& WaterBodies, float DeltaTime, const FVector& Gravity, 
	const FWaterPhysicsSettings& SceneSettings, const FGetWaterInfoAtLocation& SurfaceGetter, FWaterSurfaceProvider* WaterSurfaceProvider)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StepWaterBodies_Parallel);

	ParallelFor(WaterBodies.Num(), [&](int32 Index)
	{
		DYNAMIC_CPUPROFILER_EVENT_SCOPE(StepWaterBody, "_%s.%s", *WaterBodies[Index].Key->GetName(), *WaterBodies[Index].Value->BodyName.ToString());

#if ENGINE_VERSION_HIGHER_THAN(4, 27)
		FOptionalTaskTagScope ParallelGameThreadScope(ETaskTag::EParallelGameThread);
#endif

		const auto WaterBodyProcessingResult = ProcessWaterPhysicsBody(WaterBodies[Index].Key, *WaterBodies[Index].Value, SceneSettings);
		if (WaterBodyProcessingResult.BodyInstance == nullptr)
			return;

		const auto BodyTriangulationResult        = TriangulateBody(WaterBodies[Index].Key, *WaterBodies[Index].Value, WaterBodyProcessingResult);
		const auto WaterSurfaceIntersectionResult = FetchWaterSurfaceInfo(WaterBodies[Index].Key, *WaterBodies[Index].Value, BodyTriangulationResult, WaterBodyProcessingResult.WaterPhysicsSettings.WaterInfoFetchingMethod, SurfaceGetter, WaterSurfaceProvider);
		const auto BodyWaterIntersectionResult    = BodyWaterIntersection(WaterSurfaceIntersectionResult);
		CalculateWaterForces(WaterBodies[Index].Key, *WaterBodies[Index].Value, BodyWaterIntersectionResult, DeltaTime, Gravity);
	});
}
