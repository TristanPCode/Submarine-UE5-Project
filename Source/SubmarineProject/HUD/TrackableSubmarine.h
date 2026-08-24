#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SubmarineCharacteristics.h"    // ELinearSpeedState
#include "SubmarineCollisionComponent.h"
#include "SubmarineTorpedoComponent.h"
#include "Radar/RadarSettings.h"

// Forward declarations
#include "TrackableSubmarine.generated.h"

// ============================================================================
//  FDetectedEntry
//
//  One entry in a submarine's detection list.
//  Owned and updated by URadarComponent.
//  Read-only from the UI side (via ITrackableSubmarine::GetDetectionEntries).
// ============================================================================
USTRUCT(BlueprintType)
struct FDetectedEntry
{
    GENERATED_BODY()

    // -----------------------------------------------------------------------
    //  Identity
    // -----------------------------------------------------------------------

    /** Stable GUID matching GetActorInstanceGuid() on the detected actor. */
    UPROPERTY(BlueprintReadOnly, Category = "Radar")
    FGuid ActorGuid;

    // -----------------------------------------------------------------------
    //  Spatial data (updated every tick by RadarComponent)
    // -----------------------------------------------------------------------

    /** Current world position of the detected entity. */
    UPROPERTY(BlueprintReadOnly, Category = "Radar")
    FVector WorldPosition = FVector::ZeroVector;

    /**
     * For torpedoes: pre-computed icon rotation in radar space (degrees).
     * = TorpedoForwardAngle_RadarSpace + RadarSettings::TorpedoIconAngleOffset
     * For submarines / unknowns: always 0.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Radar")
    float IconRotation = 0.f;

    // -----------------------------------------------------------------------
    //  Detection state
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, Category = "Radar")
    ERadarDetectionState DetectionState = ERadarDetectionState::WeakDetection;

    UPROPERTY(BlueprintReadOnly, Category = "Radar")
    ERadarEntityType EntityType = ERadarEntityType::Unknown;

    // -----------------------------------------------------------------------
    //  Lifetime timers (ticked by RadarComponent)
    // -----------------------------------------------------------------------

    /**
     * Gameplay detection timer (seconds remaining).
     * Continuously reset when detection conditions are met.
     * When 0: entry is removed from the list.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Radar")
    float DetectionTimeRemaining = 0.f;

    /**
     * UI display timer (seconds remaining).
     * Set only when the owning submarine triggers a radar scan.
     * Drives icon opacity via fade system.
     * When 0: icon is fully faded (but entry may still be detected).
     */
    UPROPERTY(BlueprintReadOnly, Category = "Radar")
    float DisplayTimeRemaining = 0.f;

    // -----------------------------------------------------------------------
    //  Behavior flags
    // -----------------------------------------------------------------------

    /** True while detection conditions are currently met this tick. */
    UPROPERTY(BlueprintReadOnly, Category = "Radar")
    bool bIsCurrentlyDetected = false;

    /** True once this entry has been detected at least once. */
    UPROPERTY(BlueprintReadOnly, Category = "Radar")
    bool bWasEverDetected = false;
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

    virtual float GetDisplayDepth() const = 0;

    // -----------------------------------------------------------------------
    //  Ammo getters
    // -----------------------------------------------------------------------

    /** Current normal torpedo count. */
    virtual int32 GetNormalAmmoCount() const = 0;

    /** Maximum normal torpedo capacity. */
    virtual int32 GetNormalAmmoCapacity() const = 0;

    /** Current special torpedo count. */
    virtual int32 GetSpecialAmmoCount() const = 0;

    /** Maximum special torpedo capacity. */
    virtual int32 GetSpecialAmmoCapacity() const = 0;

    /**
     * The torpedo type used for ALL special torpedo slots.
     * Derived from SpecialTorpedoCharacteristics on the torpedo component.
     * Used by USpecialAmmoModule to set slot icons without requiring
     * manual SlotTypes configuration in the DataAsset.
     */
    virtual ETorpedoType GetSpecialTorpedoType() const = 0;

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

    // -----------------------------------------------------------------------
    //  Info getters
    // -----------------------------------------------------------------------

    /** Display name shown in the HUD (e.g. "Player 1", "Alpha Team"). */
    virtual FText GetDisplayName() const = 0;

    /** Level of this submarine (from spawn settings). */
    virtual int32 GetLevel() const = 0;

    // -----------------------------------------------------------------------
    //  Orientation / Camera getters
    // -----------------------------------------------------------------------

    /** Current yaw of the submarine in world space (degrees). */
    virtual float GetCurrentYaw() const = 0;

    /**
     * True when the periscope camera is active.
     * Determined by CameraState == ESubmarineCameraState::Periscope.
     */
    virtual bool GetIsPeriscopeActive() const = 0;

    /**
     * Current periscope zoom level [ZoomMin, ZoomMax].
     * Returns 1.0 when periscope is not active.
     */
    virtual float GetCurrentZoom() const = 0;

    /**
     * Current active camera horizontal FOV in degrees.
     * Used by radar to compute detection cone.
     */
    virtual float GetCameraFOV() const = 0;

    /**
     * Forward vector of the currently active camera.
     * In periscope mode this differs from GetActorForwardVector() because
     * the periscope rotates independently of the submarine hull.
     * Used by RadarComponent for FOV-cone detection.
     */
    virtual FVector GetCameraForwardVector() const = 0;

    // -----------------------------------------------------------------------
    //  Vulnerability getter
    // -----------------------------------------------------------------------

    /**
     * Current vulnerability score [0..1] computed by this submarine's
     * URadarComponent. Used by OTHER submarines' detection logic.
     */
    virtual float GetVulnerabilityScore() const = 0;

    // -----------------------------------------------------------------------
    //  Radar
    // -----------------------------------------------------------------------

    /**
     * Live detection list maintained by URadarComponent.
     * The UI reads this array each frame to render entity icons.
     */
    virtual const TArray<FDetectedEntry>& GetDetectionEntries() const = 0;

    /**
     * Scan cooldown ratio: 0=just scanned, 1=ready to scan again.
     * Used by RadarModule for progress display and pulse trigger detection.
     */
    virtual float GetScanCooldownRatio() const = 0;

    // -----------------------------------------------------------------------
    //  Gameplay notifications  (called from SubmarinePawn input handlers)
    // -----------------------------------------------------------------------

    /**
     * Called when the player triggers a radar scan input.
     * Spikes RadarContribution on this submarine's vulnerability score AND
     * refreshes DisplayTimeRemaining on all currently detected entries.
     */
    virtual void NotifyRadarUsed() = 0;

    /**
     * Called when this submarine fires a torpedo.
     * Spikes TorpedoContribution on the vulnerability score.
     */
    virtual void NotifyTorpedoFired() = 0;

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