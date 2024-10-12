// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "UObject/Object.h"
#include "UObject/GCObject.h"
#include "WaterPhysicsTypes.h"

class UActorComponent;
class IWaterPhysicsCollisionInterface;
struct FBodyInstance;
struct FWaterSurfaceProvider;
struct FKConvexElem;

namespace WaterPhysics
{
	struct FSubmergedTriangleArray
	{
		struct FVertex
		{
			FVector Position;
			FVector WaterVelocity;
			float   Depth;
		};

		struct FTriangle
		{
			int32 Indices[3];
			int32 OriginalTriangleIndex;
		};

		typedef TArray<FVertex,   TInlineAllocator<InlineAllocSize()>> FVertexList;
		typedef TArray<FTriangle, TInlineAllocator<InlineAllocSize()>> FTriangleList;
		
		FVertexList   VertexList;
		FTriangleList TriangleList;

		template <typename... ArgsType>
		FORCEINLINE int32 EmplaceTriangle(ArgsType&&... Args) 
		{
			const int32 TriangleIndex = TriangleList.Emplace(Args...);
			check(VertexList.IsValidIndex(TriangleList[TriangleIndex].Indices[0])
				&& VertexList.IsValidIndex(TriangleList[TriangleIndex].Indices[1])
				&& VertexList.IsValidIndex(TriangleList[TriangleIndex].Indices[2]));
			return TriangleIndex;
		}
	};

	FIndexedTriangleMesh TriangulateWaterPhysicsCollisionSetup(const FWaterPhysicsCollisionSetup& CollisionSetup, const FTriangleSubdivisionSettings& SubdivisionSettings);

	FWaterPhysicsCollisionSetup GenerateBodyInstanceWaterPhysicsCollisionSetup(FBodyInstance* BodyInstance, bool bIncludeWeldedBodies);

	FWaterPhysicsCollisionSetup GenerateWaterPhysicsCollisionSetup(const IWaterPhysicsCollisionInterface* CollisionInterface, const FName& BodyName);

};

// Generic overridable interface for managing water surface getting.
struct WATERPHYSICS_API FWaterSurfaceProvider
{
	typedef TArray<FGetWaterInfoResult, TInlineAllocator<WaterPhysics::InlineAllocSize()>> FVertexWaterInfoArray;

	virtual ~FWaterSurfaceProvider() = default;

	virtual void BeginStepScene() {}
	virtual void EndStepScene() {}
	virtual void DrawDebugProvider(UWorld* World) {};
	virtual bool SupportsParallelExecution() const { return false; }

	virtual FVertexWaterInfoArray CalculateVerticesWaterInfo(const WaterPhysics::FVertexList& Vertices, 
		const UActorComponent* Component, const FGetWaterInfoAtLocation& SurfaceGetter) = 0;
};

struct WATERPHYSICS_API FWaterPhysicsScene : public FGCObject
{
	enum EFrame
	{
		Frame_Current  = 0,
		Frame_Previous = 1
	};

	struct FTriangleData
	{
		FVector Centroid;
		FVector Normal;
		float   Area;
		float   AvgDepth;
		FVector Velocity; // Velocity relative to water at this triangle location
		FVector VelocityNormal;
		float   VelocityNormalDot;
		float   VelocitySize;
		float   VelocitySizeSquared;
		int32   OriginalTriangleIndex;

		FORCEINLINE bool IsValid() const
		{
			return !Centroid.ContainsNaN()
				&& FMath::IsNearlyEqual(Normal.Size(), 1, 0.001f)
				&& !Velocity.ContainsNaN()
				&& ((Velocity.IsNearlyZero() && VelocityNormal.IsNearlyZero()) || FMath::IsNearlyEqual(VelocityNormal.Size(), 1, 0.001f))
				&& !FMath::IsNaN(VelocityNormalDot)
				&& !FMath::IsNaN(VelocitySize)
				&& !FMath::IsNaN(VelocitySizeSquared)
				&& !FMath::IsNaN(Area) && !FMath::IsNearlyEqual(Area, 0)
				&& !FMath::IsNaN(AvgDepth);
		}
	};

	struct FPersistentTriangleData
	{
		float SweptWaterArea;
	};

	struct FActingForces
	{
		explicit FORCEINLINE FActingForces(EForceInit) { FMemory::Memzero(*this); }

		FVector BuoyancyForce;
		FVector BuoyancyTorque;

		FVector ViscousFluidResistanceForce;
		FVector ViscousFluidResistanceTorque;

		FVector PressureDragForce;
		FVector PressureDragTorque;

		FVector SlammingForce;
		FVector SlammingTorque;
	};

	struct FWaterPhysicsBody
	{
		FWaterPhysicsBody(const FName& InBodyName, const FWaterPhysicsSettings& InWaterPhysicsSettings)
			: BodyName(InBodyName)
			, WaterPhysicsSettings(InWaterPhysicsSettings)
			, ActingForces(ForceInit)
		{}

		FName                           BodyName;
		FWaterPhysicsSettings           WaterPhysicsSettings;
		TArray<FPersistentTriangleData> PersistentTriangleData[2];
		FActingForces                   ActingForces;
		float                           SubmergedArea;

		void ClearTriangleData() { PersistentTriangleData[0].Empty(); PersistentTriangleData[1].Empty(); }
	};

	typedef TMap<TObjectPtr<const UActorComponent>, TArray<FWaterPhysicsBody>> FWaterPhysicsBodies;

	struct FFrameInfo
	{
		TArray<FPersistentTriangleData>& CurrentFrame;
		TArray<FPersistentTriangleData>& PreviousFrame;
		TArray<FTriangleData>            TriangleData;
		FVector AvgFluidVelocity;
		bool    bSuccess;
		float   TotalSubmergedArea;

		FFrameInfo(TArray<FPersistentTriangleData>& InCurrentFrame, TArray<FPersistentTriangleData>& InPreviousFrame)
			: CurrentFrame(InCurrentFrame)
			, PreviousFrame(InPreviousFrame)
			, AvgFluidVelocity(FVector::ZeroVector)
			, bSuccess(true)
			, TotalSubmergedArea(0.f)
		{}
	};

private:

	int32 CurrentBufferIndex = 0;
	FWaterPhysicsBodies WaterPhysicsBodies;

public:

	FORCEINLINE FWaterPhysicsBody* AddComponentBody(const UActorComponent* Component, const FName& BodyName, const FWaterPhysicsSettings& WaterPhysicsSettings)
	{ 
		if (TArray<FWaterPhysicsBody>* Bodies = WaterPhysicsBodies.Find(Component))
		{
			if (FWaterPhysicsBody* Body = Bodies->FindByPredicate([&](const auto& X) { return X.BodyName == BodyName; }))
			{
				Body->WaterPhysicsSettings = WaterPhysicsSettings;
				Body->ClearTriangleData();
				return Body;
			}
			else
			{
				return &(*Bodies)[Bodies->Emplace(BodyName, WaterPhysicsSettings)];
			}
		}
		else
		{
			TArray<FWaterPhysicsBody>& AddedBodies = WaterPhysicsBodies.Add(Component);
			return &AddedBodies[AddedBodies.Emplace(BodyName, WaterPhysicsSettings)];
		}
	}

	FORCEINLINE bool RemoveComponent(const UActorComponent* Component) 
	{ 
		return WaterPhysicsBodies.Remove(Component) != 0; 
	}

	FORCEINLINE bool RemoveComponentBody(const UActorComponent* Component, const FName& BodyName) 
	{
		if (TArray<FWaterPhysicsBody>* Bodies = WaterPhysicsBodies.Find(Component))
			return Bodies->RemoveAll([&](const auto& X) { return X.BodyName == BodyName; }) != 0;
		return false;
	}

	FORCEINLINE const TArray<FWaterPhysicsBody>* FindComponentBodies(const UActorComponent* Component) const
	{
		return WaterPhysicsBodies.Find(Component);
	}

	FORCEINLINE TArray<FWaterPhysicsBody>* FindComponentBodies(const UActorComponent* Component)
	{
		return WaterPhysicsBodies.Find(Component);
	}

	FORCEINLINE bool ContainsComponent(const UActorComponent* Component) const
	{
		return WaterPhysicsBodies.Contains(Component);
	}

	FORCEINLINE const FWaterPhysicsBody* FindComponentBody(const UActorComponent* Component, const FName& BodyName) const
	{
		if (const TArray<FWaterPhysicsBody>* Bodies = WaterPhysicsBodies.Find(Component))
			if (const FWaterPhysicsBody* PersistentBodyData = Bodies->FindByPredicate([&](const auto& V) { return V.BodyName == BodyName; }))
				return PersistentBodyData;

		return nullptr;
	}

	FORCEINLINE FWaterPhysicsBody* FindComponentBody(const UActorComponent* Component, const FName& BodyName)
	{
		if (TArray<FWaterPhysicsBody>* Bodies = WaterPhysicsBodies.Find(Component))
			if (FWaterPhysicsBody* PersistentBodyData = Bodies->FindByPredicate([&](const auto& V) { return V.BodyName == BodyName; }))
				return PersistentBodyData;

		return nullptr;
	}

	FORCEINLINE void ClearTriangleData(const UActorComponent* Component, FName BodyName)
	{
		if (FWaterPhysicsBody* Body = FindComponentBody(Component, BodyName))
			Body->ClearTriangleData();
	}

	FORCEINLINE int32 GetFrameIndex(EFrame Frame) const { return FMath::Abs(CurrentBufferIndex - Frame); }

	FORCEINLINE void SwapBuffers() { CurrentBufferIndex = 1 - CurrentBufferIndex; }

	FORCEINLINE void ClearWaterPhysicsScene() { WaterPhysicsBodies.Reset(); }

	void StepWaterPhysicsScene(float DeltaTime, const FVector& Gravity, const FWaterPhysicsSettings& SceneSettings, 
		const FGetWaterInfoAtLocation& SurfaceGetter, bool bSurfaceGetterThreadSafe, FWaterSurfaceProvider* WaterSurfaceProvider, UObject* DebugContext);

	void AddReferencedObjects(FReferenceCollector& Collector) override;
	FString GetReferencerName() const override { return "WaterPhysicsScene"; }

private:

	FFrameInfo InitFrame(FWaterPhysicsBody& WaterPhysicsBody,
		const WaterPhysics::FIndexedTriangleMesh& TriangulatedBody, const WaterPhysics::FSubmergedTriangleArray& SubmergedTriangles,
		const FVector& BodyCenterOfMass, const FVector& BodyLinearVelocity, const FVector& BodyAngularVelocity);

	struct FWaterBodyProcessingResult
	{
		FBodyInstance*         BodyInstance; // Not necessarily the root body instance, could be welded
		FWaterPhysicsSettings  WaterPhysicsSettings;
		bool                   bHasWaterPhysicsCollisionInterface;
	};
	FWaterBodyProcessingResult ProcessWaterPhysicsBody(const UActorComponent* Component, FWaterPhysicsBody& WaterBody, const FWaterPhysicsSettings& SceneSettings);

	struct FBodyTriangulationResult
	{
		const FWaterBodyProcessingResult*  BodyProcessingResult;

		WaterPhysics::FIndexedTriangleMesh TriangulatedBody;
	};
	FBodyTriangulationResult TriangulateBody(const UActorComponent* Component, const FWaterPhysicsBody& WaterBody, const FWaterBodyProcessingResult& BodyProcessingResult);

	struct FFetchWaterSurfaceInfoResult
	{
		const FWaterBodyProcessingResult*  BodyProcessingResult;
		const FBodyTriangulationResult*    BodyTriangulationResult;

		FWaterSurfaceProvider::FVertexWaterInfoArray VertexWaterInfo;
	};
	FFetchWaterSurfaceInfoResult FetchWaterSurfaceInfo(const UActorComponent* Component, const FWaterPhysicsBody& WaterBody, const FBodyTriangulationResult& BodyTriangulationResult,
		EWaterInfoFetchingMethod WaterInfoFetchingMethod, const FGetWaterInfoAtLocation& SurfaceGetter, FWaterSurfaceProvider* WaterSurfaceProvider);

	struct FBodyWaterIntersectionResult
	{
		const FWaterBodyProcessingResult*   BodyProcessingResult;
		const FBodyTriangulationResult*     BodyTriangulationResult;
		const FFetchWaterSurfaceInfoResult* FetchWaterSurfaceInfoResult;

		WaterPhysics::FSubmergedTriangleArray SubmergedTriangleArray;
	};
	FBodyWaterIntersectionResult BodyWaterIntersection(const FFetchWaterSurfaceInfoResult& FetchWaterSurfaceInfoResult);

	void CalculateWaterForces(const UActorComponent* Component, FWaterPhysicsBody& WaterBody, const FBodyWaterIntersectionResult& BodyWaterIntersectionResult, 
		float DeltaTime, const FVector& Gravity);

	void StepWaterBodies_Synchronous(TArray<TPair<const UActorComponent*, FWaterPhysicsBody*>>& WaterBodies, float DeltaTime, const FVector& Gravity, 
		const FWaterPhysicsSettings& SceneSettings, const FGetWaterInfoAtLocation& SurfaceGetter, FWaterSurfaceProvider* WaterSurfaceProvider);

	void StepWaterBodies_Parallel(TArray<TPair<const UActorComponent*, FWaterPhysicsBody*>>& WaterBodies, float DeltaTime, const FVector& Gravity, 
		const FWaterPhysicsSettings& SceneSettings, const FGetWaterInfoAtLocation& SurfaceGetter, FWaterSurfaceProvider* WaterSurfaceProvider);
};