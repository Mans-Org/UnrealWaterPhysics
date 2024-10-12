// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FActorComponentsSelectionCustomization : public IStructCustomization
{
private:
	TSharedPtr<class SActorComponentSelectionCombo> ActorComponentSelectionCombo;

public:
	virtual void CustomizeStructHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IStructCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeStructChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IStructCustomizationUtils& StructCustomizationUtils) override {}
};