#include "SubmarineGameInstance.h"
#include "OceanSubsystem.h"

void USubmarineGameInstance::Init()
{
    Super::Init();

    // Forward the editor-assigned Ocean assets to UOceanSubsystem.
    // The subsystem is already instantiated by the time Init() runs,
    // so GetSubsystem() is safe to call here.
    UOceanSubsystem* OceanSys = GetSubsystem<UOceanSubsystem>();
    if (OceanSys)
    {
        OceanSys->DefaultRegionData = DefaultOceanRegionData;
        OceanSys->OceanMPC = OceanMPC;
        OceanSys->DebugSettings = OceanDebugSettings;
    }
}