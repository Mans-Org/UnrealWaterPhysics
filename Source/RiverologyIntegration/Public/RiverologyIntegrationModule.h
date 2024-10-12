// Copyright Mans Isaksson 2021. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRiverologyIntegration, Log, All);

namespace RiverologyIntegrationModule
{
	extern RIVEROLOGYINTEGRATION_API int32 RiverologyMarjorVersion;
	extern RIVEROLOGYINTEGRATION_API int32 RiverologyMinorVersion;
	extern RIVEROLOGYINTEGRATION_API int32 RiverologyPatchVersion;
};

class FRiverologyIntegrationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};