// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "Runtime/Launch/Resources/Version.h"

#define ENGINE_VERSION_HIGHER_THAN(Major, Minor) (ENGINE_MAJOR_VERSION > Major || (ENGINE_MAJOR_VERSION == Major && ENGINE_MINOR_VERSION > Minor))

#define ENGINE_VERSION_LESS_THAN(Major, Minor) (ENGINE_MAJOR_VERSION < Major || (ENGINE_MAJOR_VERSION == Major && ENGINE_MINOR_VERSION < Minor))

#if ENGINE_VERSION_HIGHER_THAN(5, 0)
#define WPC_PHYSICS_INTERFACE_PHYSX 0
#define WPC_WITH_PHYSX 0
#define WPC_WITH_CHAOS 1
#else
#define WPC_PHYSICS_INTERFACE_PHYSX PHYSICS_INTERFACE_PHYSX
#define WPC_WITH_PHYSX WITH_PHYSX
#define WPC_WITH_CHAOS WITH_CHAOS
#endif

#if ENGINE_VERSION_LESS_THAN(5, 1)
template< class T >
T* FindFirstObject(const TCHAR* Name)
{
	return FindObject<T>(ANY_PACKAGE, Name);
}
#endif

class UStaticMesh;
class USkeletalMesh;
class UBodySetup;
class FStaticMeshRenderData;

namespace WaterPhysicsCompat
{
	WATERPHYSICS_API UBodySetup* GetStaticMeshBodySetup(UStaticMesh* Mesh);
	
	WATERPHYSICS_API FStaticMeshRenderData* GetStaticMeshRenderData(UStaticMesh* Mesh);

	template<typename T>
	void MarkObjectPendingKill(T* Object)
	{
#if ENGINE_VERSION_LESS_THAN(5, 0)
		Object->MarkPendingKill();
#else
		Object->MarkAsGarbage();
#endif
	}

#if ENGINE_VERSION_HIGHER_THAN(5, 0)
	typedef class FAppStyle EditorStyle;
#else
	typedef class FEditorStyle EditorStyle;
#endif

	template<typename T>
	USkeletalMesh* GetSkeletalMeshAsset(T* Object)
	{
#if ENGINE_VERSION_LESS_THAN(5, 1)
		return Object->SkeletalMesh;
#else
		return Object->GetSkeletalMeshAsset();
#endif
	}
};
