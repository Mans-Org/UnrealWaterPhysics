// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "ComponentVisualizer.h"

class FWaterPhysicsCollisionComponentVisualizer : public FComponentVisualizer
{
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI);
};
