#pragma once

#include "CoreMinimal.h"
#include "PositionalIndicatorModule.h"
#include "SubmarineCharacteristics.h"
#include "EngineStateModule.generated.h"

/**
 * UEngineStateModule
 *
 * Event-driven discrete indicator showing current engine speed state.
 * Updates ONLY when LinearSpeedState changes -- no tick.
 *
 * Uses FEngineStateEntry array from FHUDModuleConfig::StateEntries.
 * Array index matches (int32)ELinearSpeedState:
 *   0=BackwardMAX, 1=BackwardMED, 2=BackwardMIN, 3=Stand,
 *   4=ForwardMIN, 5=ForwardMED, 6=ForwardMAX
 *
 * Config keys (inherits from UPositionalIndicatorModule):
 *   Textures: Background, Crank, Metal
 *   Floats:   TextureHeight, CrankOffsetX
 *   StateEntries: 7 FEngineStateEntry structs
 *
 * Blueprint setup:
 *   Inherit from UPositionalIndicatorModule's Blueprint setup.
 *   No additional widgets needed.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UEngineStateModule : public UPositionalIndicatorModule
{
    GENERATED_BODY()

protected:

    virtual void BindToDataSource()     override;
    virtual void UnbindFromDataSource() override;
    virtual void RefreshVisuals_Implementation() override;

private:

    // -----------------------------------------------------------------------
    //  Event handler
    // -----------------------------------------------------------------------

    UFUNCTION()
    void OnLinearStateChanged(ELinearSpeedState NewState);

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    /**
     * Resolve positions for a given state and push to UpdateElementPositions.
     * Logs a warning if StateEntries array is too short.
     */
    void ApplyState(ELinearSpeedState State);

    /** Last applied state -- avoids redundant updates. */
    ELinearSpeedState LastState = ELinearSpeedState::Stand;
    bool bFirstUpdate = true;
};