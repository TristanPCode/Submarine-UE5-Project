#pragma once

#include "CoreMinimal.h"
#include "NumericDisplayModule.h"
#include "SpeedometerModule.generated.h"

/**
 * USpeedometerModule
 *
 * Displays current submarine speed (cm/s) via the digit atlas.
 * Uses AtlasRowIndex = 0 (green row).
 *
 * Inherits all config, tick, and material logic from UNumericDisplayModule.
 * Only overrides GetDisplayValue() to return speed.
 *
 * Note: speed is in cm/s internally. Convert to display units (m/s or knots)
 * by adding a "ScaleFactor" float to FHUDModuleConfig::Floats if needed.
 * Default scale factor = 0.01 (cm/s -> m/s). Not applied here yet -- add
 * when display units are decided.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API USpeedometerModule : public UNumericDisplayModule
{
    GENERATED_BODY()

protected:
    virtual float GetDisplayValue() const override
    {
        if (!IsValid(DataSource.GetObject())) return 0.f;
        // Absolute value: speed indicator shows magnitude, not direction
        // Direction is shown by the Engine State module
        return FMath::Abs(DataSource->GetCurrentSpeed());
    }
};