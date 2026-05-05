#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SubmarineCharacteristics.h"    // ELinearSpeedState
#include "SubmarineCollisionComponent.h"
#include "SubmarineTorpedoComponent.h"

// Forward declarations
#include "TrackableSubmarine.generated.h"

USTRUCT(BlueprintType)
struct FDetectedEntry
{
    GENERATED_BODY()
};

// -----------------------------------------------------------------------
//  UTrackableSubmarine  --  boilerplate UInterface object (not used directly)
// -----------------------------------------------------------------------
UINTERFACE(MinimalAPI, Blueprintable)
class UTrackableSubmarine : public UInterface
{
    GENERATED_BODY()
};

/**
 * ITrackableSubmarine
 *
 * Abstraction layer between the HUD system and gameplay actors.
 * Implemented by ASubmarinePawn.
 *
 * Rules:
 *   - Read-only: no setters exposed through this interface
 *   - Safe: all getters must be safe to call even under stress
 *   - No direct component access from UI: UI only uses this interface
 *
 * Delegate getters (Option B binding pattern):
 *   Modules bind directly to the real delegates on the gameplay components.
 *   Getters return references to the actual delegate instances so bindings
 *   are live -- not copies.
 *
 *   IMPORTANT: always validate the underlying UObject before binding:
 *     UObject* Obj = DataSource.GetObject();
 *     if (!IsValid(Obj)) return;
 *     DataSource->GetOnDamagedDelegate().AddDynamic(...);
 */
class SUBMARINEPROJECT_API ITrackableSubmarine
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  State getters  (read-only, safe to call any time)
    // -----------------------------------------------------------------------

    /** Current health as a 0..1 ratio. */
    virtual float GetHealthRatio() const = 0;

    /** Signed speed along the submarine forward axis (cm/s). */
    virtual float GetCurrentSpeed() const = 0;

    /** Current depth below water surface (cm, positive = submerged). */
    virtual float GetCurrentDepth() const = 0;

    /** Current pitch of the submarine in degrees. */
    virtual float GetCurrentPitch() const = 0;

    /** Current vertical state index (0 = full down, mid = flat, max = full up). */
    virtual int32 GetVerticalStateIndex() const = 0;

    /** Total number of vertical states (safe, odd, >= 3). */
    virtual int32 GetVerticalStateCount() const = 0;

    /** Discrete linear speed state (Stand, ForwardMIN, BackwardMAX, etc.). */
    virtual ELinearSpeedState GetLinearSpeedState() const = 0;

    /** Current normal torpedo count. */
    virtual int32 GetNormalAmmoCount() const = 0;

    /** Maximum normal torpedo capacity. */
    virtual int32 GetNormalAmmoCapacity() const = 0;

    /** Current special torpedo count. */
    virtual int32 GetSpecialAmmoCount() const = 0;

    /** Maximum special torpedo capacity. */
    virtual int32 GetSpecialAmmoCapacity() const = 0;

    /**
     * Fire cooldown ratio: 0 = just fired (on cooldown), 1 = fully ready.
     * Safe to use directly as a progress bar value.
     */
    virtual float GetFireCooldownRatio() const = 0;

    /**
     * Reload progress ratio: 0 = just started reloading, 1 = complete.
     * Meaning depends on reload mode (Progressive vs Full).
     */
    virtual float GetReloadRatio() const = 0;

    /** True while a reload is in progress. */
    virtual bool GetIsReloading() const = 0;

    /** Display name shown in the HUD (e.g. "Player 1", "Alpha Team"). */
    virtual FText GetDisplayName() const = 0;

    // -----------------------------------------------------------------------
    //  Radar placeholder
    //  Returns the current list of detected entries for this submarine.
    //  Will be populated when RadarComponent is implemented (Phase 4).
    // -----------------------------------------------------------------------

    /**
     * Returns the detected entries for this submarine's radar.
     * Returns an empty array until RadarComponent is implemented.
     * Modules calling this must handle the empty case gracefully.
     */
    virtual const TArray<FDetectedEntry>& GetDetectionEntries() const = 0;

    // -----------------------------------------------------------------------
    //  Delegate getters  (return references to live delegates on components)
    //
    //  BINDING PATTERN for modules:
    //
    //    void UMyModule::BindToDataSource()
    //    {
    //        UObject* Obj = DataSource.GetObject();
    //        if (!IsValid(Obj)) return;
    //        DataSource->GetOnDamagedDelegate().AddDynamic(
    //            this, &UMyModule::HandleDamaged);
    //    }
    //
    //    void UMyModule::UnbindFromDataSource()
    //    {
    //        UObject* Obj = DataSource.GetObject();
    //        if (!IsValid(Obj)) return;
    //        DataSource->GetOnDamagedDelegate().RemoveDynamic(
    //            this, &UMyModule::HandleDamaged);
    //    }
    // -----------------------------------------------------------------------

    virtual FOnSubmarineDamaged& GetOnDamagedDelegate() = 0;
    virtual FOnAmmoChanged& GetOnAmmoChangedDelegate() = 0;
    virtual FOnTorpedoFired& GetOnTorpedoFiredDelegate() = 0;
    virtual FOnReadyToFire& GetOnReadyToFireDelegate() = 0;
    virtual FOnFireCooldownComplete& GetOnFireCooldownDelegate() = 0;
    virtual FOnLinearStateChanged& GetOnLinearStateChangedDelegate() = 0;
};