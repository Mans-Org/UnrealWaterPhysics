// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsCompatibilityLayer.h"
#include "Engine/StaticMesh.h"
#include "Runtime/Launch/Resources/Version.h"



UBodySetup* WaterPhysicsCompat::GetStaticMeshBodySetup(UStaticMesh* Mesh)
{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	return Mesh->GetBodySetup();
#else
	return Mesh->BodySetup;
#endif
}

FStaticMeshRenderData* WaterPhysicsCompat::GetStaticMeshRenderData(UStaticMesh* Mesh)
{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	return Mesh->GetRenderData();
#else
	return Mesh->RenderData.Get();
#endif
}