#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HUDContextTypes.h"
#include "HUDGlobalDefaults.generated.h"

class USubmarineHUDSettings;
class UInfoBillboardContextSettings;

/**
 * UHUDGlobalDefaults
 *
 * Project-wide default HUD DataAsset registry.
 * Assign ONE instance of this to ASubmarineGameMode in the Blueprint editor.
 *
 * Resolution order for any HUD context:
 *   1. RuntimeMatchSettings->HUDContextOverrides[Context]   (match-specific override)
 *   2. UHUDGlobalDefaults->DefaultsPerContext[Context]       (this asset — global default)
 *   3. Log error — context has no registered HUD
 *
 * Typical setup:
 *   Gameplay            -> DA_HUD_Gameplay_Solo
 *   Gameplay_Splitscreen -> DA_HUD_Gameplay_Splitscreen
 *   Spectator           -> DA_HUD_Spectator
 *   Replay              -> DA_HUD_Replay
 *   DeathReplay         -> DA_HUD_DeathReplay
 *   MainMenu            -> DA_HUD_MainMenu
 *   Cinematic           -> nullptr  (no HUD in cinematics)
 */
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UHUDGlobalDefaults : public UDataAsset
{
    GENERATED_BODY()

public:

    /**
     * Default HUD DataAsset per context.
     * Leave an entry null to display no HUD for that context.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
    TMap<EHUDContext, TObjectPtr<USubmarineHUDSettings>> DefaultsPerContext;

    /**
     * Resolve the HUD settings for a given context.
     * Returns nullptr if no entry exists (valid — means "no HUD").
     */
    USubmarineHUDSettings* Resolve(EHUDContext Context) const
    {
        const TObjectPtr<USubmarineHUDSettings>* Found = DefaultsPerContext.Find(Context);
        return Found ? Found->Get() : nullptr;
    }

    /**
     * Billboard settings per HUD context.
     * Null entry = all billboards hidden for that context.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard")
    TMap<EHUDContext, TObjectPtr<UInfoBillboardContextSettings>> BillboardSettingsPerContext;

    UFUNCTION(BlueprintPure, Category = "Billboard")
    UInfoBillboardContextSettings* ResolveBillboard(EHUDContext Context) const
    {
        const TObjectPtr<UInfoBillboardContextSettings>* Found =
            BillboardSettingsPerContext.Find(Context);
        return Found ? Found->Get() : nullptr;
    }
};