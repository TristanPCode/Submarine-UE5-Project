#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OceanDebugSettings.generated.h"

// ---------------------------------------------------------------------------
//  UOceanDebugSettings
//  DataAsset holding all debug toggles for the ocean system.
//  Follows the exact same pattern as SubmarineHUDDebugSettings.
//  Assign in UOceanSubsystem via the Details panel in the GameInstance BP.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UOceanDebugSettings : public UDataAsset
{
    GENERATED_BODY()

public:

    // Master switch. If false, all ocean debug output is suppressed.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug")
    bool bEnableOceanDebug = false;

    // Log subsystem initialization: region registration, fallback DA validation.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Logs")
    bool bLogInitialization = false;

    // Log per-frame cache computation (very verbose, use sparingly).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Logs")
    bool bLogFrameCache = false;

    // Log every individual water height query (extremely verbose).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Logs")
    bool bLogWaterQueries = false;

    // Log MPC parameter writes each frame.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Logs")
    bool bLogMPCWrites = false;

    // Seconds between repeated log entries. Mirrors PhysicsLogFrequency pattern.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Logs", meta = (ClampMin = "0.1"))
    float LogFrequency = 2.f;

    // Draw each registered region's bounds in the world (box or sphere wireframe).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Draw")
    bool bDrawRegionBounds = false;

    // Draw water height lines for local player submarines (cyan/orange per player index).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Draw|Water Height")
    bool bDrawWaterHeightAtPlayers = false;

    // Draw water height lines for CPU submarines (green).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Draw|Water Height")
    bool bDrawWaterHeightAtCPUs = false;

    // Draw the blended agitation value as a color overlay at actor positions.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Draw")
    bool bDrawAgitationAtActors = false;

    // Thickness of debug draw lines.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Draw", meta = (ClampMin = "0.5"))
    float DebugLineThickness = 2.f;

    // Duration debug draws persist each frame (keep near 0 for live overlay).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Debug|Draw", meta = (ClampMin = "0.0"))
    float DebugDrawDuration = 0.02f;
};