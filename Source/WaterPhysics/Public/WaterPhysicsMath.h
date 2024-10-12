// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "WaterPhysicsTypes.h"

template<typename TTriElemArray, typename TReturn = TTriElemArray>
FORCEINLINE TReturn CalcTriangleElemAvg(const TTriElemArray& TriElemArray) { return (TriElemArray[0] + TriElemArray[1] + TriElemArray[2]) / 3.f; }

template<typename TTriElemVertex, typename TReturn = TTriElemVertex>
FORCEINLINE TReturn CalcTriangleElemAvg(const TTriElemVertex TriElemArray[3]) { return (TriElemArray[0] + TriElemArray[1] + TriElemArray[2]) / 3.f; }

template<typename TTriElemVertex, typename TReturn = TTriElemVertex>
FORCEINLINE TReturn CalcTriangleElemAvg(const TTriElemVertex* TriElemArray[3]) { return (*TriElemArray[0] + *TriElemArray[1] + *TriElemArray[2]) / 3.f; }

template<typename TTriangle>
FORCEINLINE FVector CalcTriangleCentroid(const TTriangle& Triangle) { return (Triangle[0] + Triangle[1] + Triangle[2]) / 3.f; }

template<typename TVertex>
FORCEINLINE FVector CalcTriangleCentroid(const TVertex* Triangle[3]) { return (*Triangle[0] + *Triangle[1] + *Triangle[2]) / 3.f; }

template<typename TTriangle>
FORCEINLINE FVector CalcTriangleNormal(const TTriangle& Triangle) { return FVector::CrossProduct((Triangle[1] - Triangle[0]), (Triangle[2] - Triangle[0])).GetSafeNormal(); }

template<typename TVertex>
FORCEINLINE FVector CalcTriangleNormal(const TVertex* Triangle[3]) { return FVector::CrossProduct((*Triangle[1] - *Triangle[0]), (*Triangle[2] - *Triangle[0])).GetSafeNormal(); }

template<typename TTriangle>
FORCEINLINE float CalcTriangleArea(const TTriangle& Triangle) { return ((Triangle[1] - Triangle[2]) ^ (Triangle[0] - Triangle[2])).Size() * 0.5f; }

template<typename TVertex>
FORCEINLINE float CalcTriangleArea(const TVertex* Triangle[3]) { return ((*Triangle[1] - *Triangle[2]) ^ (*Triangle[0] - *Triangle[2])).Size() * 0.5f; }

template<typename TTriangle>
FORCEINLINE float CalcTriangleAreaM2(const TTriangle& Triangle) { return CalcTriangleArea(Triangle) * 0.0001f /* cm2 -> m2 */; }

template<typename TVertex>
FORCEINLINE float CalcTriangleAreaM2(const TVertex* Triangle[3]) { return CalcTriangleArea(Triangle) * 0.0001f /* cm2 -> m2 */; }

FORCEINLINE FVector CalcVertexVelocity(const FVector& Vertex, const FVector& BodyCenterOfMass, const FVector& BodyLinearVelocity, const FVector& BodyAngularVelocity)
{
	return BodyLinearVelocity + FVector::CrossProduct(BodyAngularVelocity, Vertex - BodyCenterOfMass);
}

// Meeter per second version of CalcVertexVelocity 
FORCEINLINE FVector CalcVertexVelocityMS(const FVector& Vertex, const FVector& BodyCenterOfMass, const FVector& BodyLinearVelocity, const FVector& BodyAngularVelocity)
{
	return CalcVertexVelocity(Vertex, BodyCenterOfMass, BodyLinearVelocity, BodyAngularVelocity) * 0.01f /* cm/s -> m/s */;
}

template<typename TTriangle>
FORCEINLINE FVector CalcTriangleVelocity(const TTriangle& Triangle, const FVector& BodyCenterOfMass, const FVector& BodyLinearVelocity, const FVector& BodyAngularVelocity)
{
	return CalcVertexVelocity(CalcTriangleCentroid(Triangle), BodyCenterOfMass, BodyLinearVelocity, BodyAngularVelocity);
}

template<typename TVertex>
FORCEINLINE FVector CalcTriangleVelocity(const TVertex Triangle[3], const FVector& BodyCenterOfMass, const FVector& BodyLinearVelocity, const FVector& BodyAngularVelocity)
{
	return CalcVertexVelocity(CalcTriangleCentroid(Triangle), BodyCenterOfMass, BodyLinearVelocity, BodyAngularVelocity);
}

// Meeter per second version of CalcTriangleVelocity 
template<typename TTriangle>
FORCEINLINE FVector CalcTriangleVelocityMS(const TTriangle& Triangle, const FVector& BodyCenterOfMass, const FVector& BodyLinearVelocity, const FVector& BodyAngularVelocity)
{
	return CalcTriangleVelocity(Triangle, BodyCenterOfMass, BodyLinearVelocity, BodyAngularVelocity)  * 0.01f /* cm/s -> m/s */;;
}

// Meeter per second version of CalcTriangleVelocity 
template<typename TVertex>
FORCEINLINE FVector CalcTriangleVelocityMS(const TVertex Triangle[3], const FVector& BodyCenterOfMass, const FVector& BodyLinearVelocity, const FVector& BodyAngularVelocity)
{
	return CalcTriangleVelocity(Triangle, BodyCenterOfMass, BodyLinearVelocity, BodyAngularVelocity)  * 0.01f /* cm/s -> m/s */;;
}

struct FForce
{
	FVector Force;
	FVector Torque;
	#if WITH_WATER_PHYS_DEBUG
	FVector AvgLocation;
	#endif

	explicit FORCEINLINE FForce(EForceInit) { FMemory::Memzero(*this); }

	FORCEINLINE_DEBUGGABLE void AddForce(const FVector& InForce, const FVector& InLocation, const FVector& InCOM)
	{
		#if WITH_WATER_PHYS_DEBUG
		const float ForceSize	= Force.Size();
		const float InForceSize = InForce.Size();
		const float TotalSize   = ForceSize + InForceSize;
		if (TotalSize > 0.f)
			AvgLocation = (AvgLocation * (ForceSize / TotalSize)) + (InLocation * (InForceSize / TotalSize));
		#endif

		Force  += InForce;
		Torque += FVector::CrossProduct(InLocation - InCOM, InForce);

		checkf(this->IsValid(), TEXT("Invalid force: Force: %s, Torque: %s"), *Force.ToString(), *Torque.ToString());
	}

	FORCEINLINE FString ToString() const { return FString::Printf(TEXT("{ Force: %s, Torque: %s }"), *Force.ToString(), *Torque.ToString()); }

	FORCEINLINE bool IsValid() const { return !Force.ContainsNaN() && !Torque.ContainsNaN(); }

	FORCEINLINE void operator+=(const FForce& Other) { Force += Other.Force; Torque += Other.Torque; }
};

WATERPHYSICS_API void TransformSphereElem(FWaterPhysicsCollisionSetup::FSphereElem& SphereElem, const FTransform& Transform);

WATERPHYSICS_API void TransformBoxElem(FWaterPhysicsCollisionSetup::FBoxElem& BoxElem, const FTransform& Transform);

WATERPHYSICS_API void TransformSphylElem(FWaterPhysicsCollisionSetup::FSphylElem& SphylElem, const FTransform& Transform);

WATERPHYSICS_API void TransformMeshElem(FWaterPhysicsCollisionSetup::FMeshElem& MeshElem, const FTransform& Transform);

WATERPHYSICS_API WaterPhysics::FIndexedTriangleMesh ExtractConvexElemTriangles(const struct FKConvexElem& ConvexElem, bool bMirrorX);