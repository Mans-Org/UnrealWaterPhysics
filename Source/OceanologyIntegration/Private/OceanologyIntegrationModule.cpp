// Copyright Mans Isaksson 2021. All Rights Reserved.

#include "OceanologyIntegrationModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY(LogOceanologyIntegration);

int32 OceanologyIntegrationModule::OceanologyMarjorVersion = 0;
int32 OceanologyIntegrationModule::OceanologyMinorVersion = 0;
int32 OceanologyIntegrationModule::OceanologyPatchVersion = 0;

void FOceanologyIntegrationModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Oceanology_Plugin"));
	if (!ensureAlwaysMsgf(Plugin.IsValid(), TEXT("[OceanologyIntegrationModule] Could not find Oceanology module during load, Oceanology integration will not work")))
	{
		return;
	}

	const FString VersionName = Plugin->GetDescriptor().VersionName;

	FString Left;
	FString Right;
	if (VersionName.Split(TEXT("."), &Left, &Right))
	{
		OceanologyIntegrationModule::OceanologyMarjorVersion = FCString::Atoi(*Left);
		if (Right.Split(TEXT("."), &Left, &Right))
		{
			OceanologyIntegrationModule::OceanologyMinorVersion = FCString::Atoi(*Left);
			OceanologyIntegrationModule::OceanologyPatchVersion = FCString::Atoi(*Right);
		}
		else
		{
			OceanologyIntegrationModule::OceanologyMinorVersion = FCString::Atoi(*Right);
		}
	}
}

IMPLEMENT_MODULE(FOceanologyIntegrationModule, OceanologyIntegration);