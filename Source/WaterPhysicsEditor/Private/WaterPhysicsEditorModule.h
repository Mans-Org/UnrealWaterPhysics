// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogWaterPhysicsEd, Log, All);

class FWaterPhysicsEditorModule : public IModuleInterface
{
private:
	FDelegateHandle OnMapOpenedHandle;

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void RegisterDetailsCustomization();
	void UnregisterDetailsCustomization();

	void RegisterVisualizers();
	void UnregisterVisualizers();
};