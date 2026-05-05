#pragma once

#include "CoreMinimal.h"
#include "PositionalIndicatorModule.h"
#include "PitchModule.generated.h"

/**
 * UPitchModule
 *
 * Tick-driven continuous pitch indicator.
 * Interpolates crank and metal positions between MinPitchY and MaxPitchY
 * based on the current pitch angle (degrees).
 *
 * Config keys (inherits from UPositionalIndicatorModule):
 *   Textures: Background, Crank, Metal
 *   Floats:
 *     TextureHeight  -- original texture height in pixels
 *     CrankOffsetX   -- horizontal offset of crank (pixels)
 *     MinCrankY      -- crank Y at full-down pitch (pixels from top)
 *     MaxCrankY      -- crank Y at full-up pitch (pixels from top)
 *     MinMetalY      -- metal Y at full-down pitch
 *     MaxMetalY      -- metal Y at full-up pitch
 *     MaxPitchAngle  -- maximum pitch angle (degrees), e.g. 30.0
 *     InvertCrank    -- 1.0 = measure from bottom, 0.0 = from top
 *     InvertMetal    -- same for metal
 *
 * Blueprint setup:
 *   Inherit from UPositionalIndicatorModule's Blueprint setup.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UPitchModule : public UPositionalIndicatorModule
{
    GENERATED_BODY()

protected:

    virtual void BindToDataSource()     override;
    virtual void UnbindFromDataSource() override;
    virtual void RefreshVisuals_Implementation() override;

    /**
     * NativeTick: reads current pitch and updates positions.
     * Only active when DataSource is valid (enabled in BindToDataSource).
     */
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:

    /**
     * Compute and apply positions from a pitch value in degrees.
     * Clamps to [-MaxPitchAngle, +MaxPitchAngle] then normalizes to 0..1.
     */
    void ApplyPitch(float PitchDegrees);

    /** Last applied pitch -- avoids redundant UpdateElementPositions calls. */
    float LastPitch = FLT_MAX;

    /** Tolerance for pitch change detection (degrees). */
    static constexpr float PitchChangeTolerance = 0.05f;
};