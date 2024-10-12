// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsEditorModule.h"
#include "WaterPhysicsTypes.h"
#include "WaterPhysicsCollisionComponent.h"
#include "WaterPhysicsCollisionComponentVisualizer.h"
#include "ActorComponentsSelectionCustomization.h"
#include "WaterPhysicsFilterCustomization.h"
#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Misc/MessageDialog.h"

DEFINE_LOG_CATEGORY(LogWaterPhysicsEd);

void FWaterPhysicsEditorModule::StartupModule()
{
	RegisterDetailsCustomization();
	RegisterVisualizers();
}

void FWaterPhysicsEditorModule::ShutdownModule()
{
	UnregisterDetailsCustomization();
	UnregisterVisualizers();
}

void FWaterPhysicsEditorModule::RegisterDetailsCustomization()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FActorComponentsSelection::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([]() { return MakeShareable(new FActorComponentsSelectionCustomization); })
	);

	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FWaterPhysicsFilter::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([]() { return MakeShareable(new FWaterPhysicsFilterCustomization); })
	);
}

void FWaterPhysicsEditorModule::UnregisterDetailsCustomization()
{
	if (FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyEditorModule->UnregisterCustomPropertyTypeLayout(FActorComponentsSelection::StaticStruct()->GetFName());

		PropertyEditorModule->UnregisterCustomPropertyTypeLayout(FWaterPhysicsFilter::StaticStruct()->GetFName());
	}
}

void FWaterPhysicsEditorModule::RegisterVisualizers()
{
	if (GUnrealEd != NULL)
	{
		TSharedPtr<FComponentVisualizer> WaterPhysicsCollisionComponentVisualizer = MakeShared<FWaterPhysicsCollisionComponentVisualizer>();
		GUnrealEd->RegisterComponentVisualizer(UWaterPhysicsCollisionComponent::StaticClass()->GetFName(), WaterPhysicsCollisionComponentVisualizer);
		WaterPhysicsCollisionComponentVisualizer->OnRegister();
	}
}

void FWaterPhysicsEditorModule::UnregisterVisualizers()
{
	if (GUnrealEd != NULL)
	{
		GUnrealEd->UnregisterComponentVisualizer(UWaterPhysicsCollisionComponent::StaticClass()->GetFName());
	}
}

IMPLEMENT_MODULE(FWaterPhysicsEditorModule, WaterPhysicsEditor);