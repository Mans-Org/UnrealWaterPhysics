// Copyright Mans Isaksson 2021. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogOceanologyIntegration, Log, All);

namespace OceanologyIntegrationModule
{
	extern OCEANOLOGYINTEGRATION_API int32 OceanologyMarjorVersion;
	extern OCEANOLOGYINTEGRATION_API int32 OceanologyMinorVersion;
	extern OCEANOLOGYINTEGRATION_API int32 OceanologyPatchVersion;
};

class FOceanologyIntegrationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};