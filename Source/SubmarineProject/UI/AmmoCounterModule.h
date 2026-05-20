#pragma once

#include "CoreMinimal.h"
#include "NumericDisplayModule.h"
#include "AmmoCounterModule.generated.h"

/**
 * UAmmoCounterModule
 *
 * Displays current normal torpedo count via the digit atlas.
 * Child module of UNormalAmmoModule -- instantiated by it as the counter slot.
 *
 * Inherits all config, tick, and material logic from UNumericDisplayModule.
 * Only overrides GetDisplayValue() to return normal ammo count.
 *
 * Blueprint setup:
 *   Create BP_AmmoCounterModule inheriting from this class.
 *   Add a CanvasPanel as root, then a UCanvasPanel named exactly "DigitCanvas".
 *   Optionally add a UImage named "BackgroundImage" for a background texture.
 *   Digit images are created dynamically in C++ -- do not add them in BP.
 *
 * DA setup (in the NormalAmmo DA config):
 *   Set CounterModuleClass = BP_AmmoCounterModule_C
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UAmmoCounterModule : public UNumericDisplayModule
{
    GENERATED_BODY()

protected:
    virtual float GetDisplayValue() const override
    {
        if (!IsValid(DataSource.GetObject())) return 0.f;
        return static_cast<float>(DataSource->GetNormalAmmoCount());
    }
};
