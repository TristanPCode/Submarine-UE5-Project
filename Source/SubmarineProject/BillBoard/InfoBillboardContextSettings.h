#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InfoBillboardSettings.h"
#include "HUDContextTypes.h"
#include "InfoBillboardContextSettings.generated.h"

/**
 * UInfoBillboardContextSettings
 *
 * DataAsset that defines billboard appearance for each entity relationship
 * category within a given HUD context.
 *
 * One instance per HUD context (Gameplay, Spectator, DeathReplay, etc).
 * Assign instances to UHUDGlobalDefaults::BillboardSettingsPerContext.
 *
 * Usage:
 *   - Gameplay: show allied billboards, hide self and own torpedoes
 *   - Spectator: show all billboards
 *   - DeathReplay: hide all billboards
 *   - Cinematic: hide all billboards
 *
 * Leave any entry null to hide that relationship category entirely.
 */
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UInfoBillboardContextSettings : public UDataAsset
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Submarine billboards
    // -----------------------------------------------------------------------

    /** Billboard for the local player's own submarine. Usually null (hidden). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarines")
    TObjectPtr<UInfoBillboardSettings> Self;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarines")
    TObjectPtr<UInfoBillboardSettings> AlliedPlayer;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarines")
    TObjectPtr<UInfoBillboardSettings> AlliedCPU;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarines")
    TObjectPtr<UInfoBillboardSettings> EnemyPlayer;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarines")
    TObjectPtr<UInfoBillboardSettings> EnemyCPU;

    // -----------------------------------------------------------------------
    //  Torpedo billboards
    // -----------------------------------------------------------------------

    /** Own torpedoes. Usually null (hidden) during gameplay. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedoes")
    TObjectPtr<UInfoBillboardSettings> OwnTorpedo;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedoes")
    TObjectPtr<UInfoBillboardSettings> AlliedPlayerTorpedo;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedoes")
    TObjectPtr<UInfoBillboardSettings> AlliedCPUTorpedo;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedoes")
    TObjectPtr<UInfoBillboardSettings> EnemyPlayerTorpedo;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedoes")
    TObjectPtr<UInfoBillboardSettings> EnemyCPUTorpedo;

    // -----------------------------------------------------------------------
    //  Runtime helper
    // -----------------------------------------------------------------------

    /**
     * Get the settings for a given relationship. Returns nullptr if hidden.
     */
    UFUNCTION(BlueprintPure, Category = "Billboard")
    UInfoBillboardSettings* GetSettings(EBillboardRelationship Relationship) const
    {
        switch (Relationship)
        {
        case EBillboardRelationship::Self:                return Self;
        case EBillboardRelationship::AlliedPlayer:        return AlliedPlayer;
        case EBillboardRelationship::AlliedCPU:           return AlliedCPU;
        case EBillboardRelationship::EnemyPlayer:         return EnemyPlayer;
        case EBillboardRelationship::EnemyCPU:            return EnemyCPU;
        case EBillboardRelationship::OwnTorpedo:          return OwnTorpedo;
        case EBillboardRelationship::AlliedPlayerTorpedo: return AlliedPlayerTorpedo;
        case EBillboardRelationship::AlliedCPUTorpedo:    return AlliedCPUTorpedo;
        case EBillboardRelationship::EnemyPlayerTorpedo:  return EnemyPlayerTorpedo;
        case EBillboardRelationship::EnemyCPUTorpedo:     return EnemyCPUTorpedo;
        default: return nullptr;
        }
    }
};