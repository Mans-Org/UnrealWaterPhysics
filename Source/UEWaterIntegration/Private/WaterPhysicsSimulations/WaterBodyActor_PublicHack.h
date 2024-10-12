#include "WaterSplineMetadata.h"
#include "NavAreas/NavArea.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "WaterWaves.h"
// WaterSubsystem.h Includes
#include "UObject/ObjectMacros.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineTypes.h"
#include "Engine/Public/Tickable.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "WaterBodyManager.h"
#define private friend class FWaterBodyHack; private
#define protected friend class FWaterBodyHack; protected
#include "WaterBodyActor.h"
#include "WaterSubsystem.h"
#undef private
#undef protected