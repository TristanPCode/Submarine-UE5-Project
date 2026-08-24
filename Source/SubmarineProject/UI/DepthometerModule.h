#pragma once

#include "CoreMinimal.h"
#include "NumericDisplayModule.h"
#include "DepthometerModule.generated.h"

/**
 * UDepthModule
 *
 * Displays current submarine depth (cm below surface) via the digit atlas.
 * Uses AtlasRowIndex = 1 (blue row).
 *
 * Inherits all config, tick, and material logic from UNumericDisplayModule.
 * Only overrides GetDisplayValue() to return depth.
 *
 * Depth is in cm internally. If you want to display in meters, add
 * "ScaleFactor" = 0.01 to FHUDModuleConfig::Floats and multiply here.
 * Not applied by default -- add when display units are decided.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UDepthModule : public UNumericDisplayModule
{
    GENERATED_BODY()

protected:
    virtual float GetDisplayValue() const override
    {
        if (!IsValid(DataSource.GetObject())) return 0.f;
        // Clamp to 0: depth is always positive (above surface = 0, not negative)
        return DataSource->GetDisplayDepth();
    }
};