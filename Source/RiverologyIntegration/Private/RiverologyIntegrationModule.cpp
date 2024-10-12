// Copyright Mans Isaksson 2021. All Rights Reserved.

#include "RiverologyIntegrationModule.h"
#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY(LogRiverologyIntegration);

int32 RiverologyIntegrationModule::RiverologyMarjorVersion = 0;
int32 RiverologyIntegrationModule::RiverologyMinorVersion = 0;
int32 RiverologyIntegrationModule::RiverologyPatchVersion = 0;

void FRiverologyIntegrationModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Riverology_Plugin"));
	if (!ensureAlwaysMsgf(Plugin.IsValid(), TEXT("[RiverologyIntegrationModule] Could not find Riverology module during load, Riverology integration will not work")))
	{
		return;
	}

	const FString VersionName = Plugin->GetDescriptor().VersionName;

	FString Left;
	FString Right;
	if (VersionName.Split(TEXT("."), &Left, &Right))
	{
		RiverologyIntegrationModule::RiverologyMarjorVersion = FCString::Atoi(*Left);
		if (Right.Split(TEXT("."), &Left, &Right))
		{
			RiverologyIntegrationModule::RiverologyMinorVersion = FCString::Atoi(*Left);
			RiverologyIntegrationModule::RiverologyPatchVersion = FCString::Atoi(*Right);
		}
		else
		{
			RiverologyIntegrationModule::RiverologyMinorVersion = FCString::Atoi(*Right);
		}
	}
}

IMPLEMENT_MODULE(FRiverologyIntegrationModule, RiverologyIntegration);