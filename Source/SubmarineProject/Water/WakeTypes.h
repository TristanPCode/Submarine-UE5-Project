#pragma once

#include "CoreMinimal.h"
#include "WakeTypes.generated.h"

// ---------------------------------------------------------------------------
//  EWakeLOD
//  Defined in its own header (no UCLASS) so WakeComponent.h and
//  TorpedoWakeComponent.h can include it without UHT ordering problems.
//  OceanWakeRegistry.h also includes this header.
// ---------------------------------------------------------------------------
UENUM(BlueprintType)
enum class EWakeLOD : uint8
{
    Full = 0  UMETA(DisplayName = "Full"),
    Reduced = 1  UMETA(DisplayName = "Reduced"),
    Minimal = 2  UMETA(DisplayName = "Minimal"),
    Off = 3  UMETA(DisplayName = "Off"),
};