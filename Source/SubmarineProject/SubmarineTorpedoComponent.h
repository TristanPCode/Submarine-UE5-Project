#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TorpedoCharacteristics.h"   // ETorpedoType
#include "SubmarineTorpedoComponent.generated.h"

class ATorpedoPawn;
class USubmarineCharacteristics;

// -----------------------------------------------------------------------------
//  Reload mode
// -----------------------------------------------------------------------------
UENUM(BlueprintType)
enum class ETorpedoReloadMode : uint8
{
    /**
     * One torpedo reloads at a time using ProgressiveReloadCooldown.
     * Reload runs continuously whenever below capacity.
     */
    Progressive,

    /**
     * All torpedoes reload at once using FullReloadCooldown.
     * Timer only starts when count reaches 0.
     */
    Full
};

// -----------------------------------------------------------------------------
//  Delegates
// -----------------------------------------------------------------------------

/** A torpedo was fired */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTorpedoFired,
    ETorpedoType, TorpedoType,
    ATorpedoPawn*, TorpedoActor);

/** Ammo count changed (fired or reloaded) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChanged,
    int32, NormalCount,
    int32, SpecialCount);

/** One normal torpedo was reloaded (Progressive mode) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProgressiveReloadComplete);

/** All normal torpedoes reloaded at once (Full mode) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFullReloadComplete);

/** Shared fire cooldown just reached 0 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFireCooldownComplete);

/**
 * Shared fire cooldown reached 0 AND at least one torpedo type is available.
 * Use this for the "ready to fire" HUD indicator.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReadyToFire);

// -----------------------------------------------------------------------------
//  Component
// -----------------------------------------------------------------------------

/**
 * USubmarineTorpedoComponent
 *
 * Attach to ASubmarinePawn. Manages:
 *   - Normal torpedo ammo (count + capacity, auto-reloads)
 *   - Special torpedo ammo (count + capacity, no auto-reload)
 *   - Shared fire cooldown (applies between ALL torpedo types)
 *   - Normal torpedo reload cooldown
 *   - Deferred Blueprint class spawn so each variant can have its own mesh/material
 *
 * Blueprint setup:
 *   1. Add this component to your submarine Blueprint.
 *   2. Set NormalTorpedoBlueprintClass and SpecialTorpedoBlueprintClass to your
 *      BP_TorpedoPawn subclasses.
 *   3. Set NormalTorpedoCharacteristics and SpecialTorpedoCharacteristics to the
 *      matching DataAssets.
 *   4. Call FireNormalTorpedo() / FireSpecialTorpedo() from input or Blueprint.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API USubmarineTorpedoComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USubmarineTorpedoComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -------------------------------------------------------------------------
    //  Blueprint class references
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Ammo")
    TSubclassOf<ATorpedoPawn> NormalTorpedoBlueprintClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Ammo")
    TSubclassOf<ATorpedoPawn> SpecialTorpedoBlueprintClass;

    // -------------------------------------------------------------------------
    //  DataAsset references
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Ammo")
    TObjectPtr<UTorpedoCharacteristics> NormalTorpedoCharacteristics;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Ammo")
    TObjectPtr<UTorpedoCharacteristics> SpecialTorpedoCharacteristics;

    // -------------------------------------------------------------------------
    //  Ammo state
    // -------------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Ammo")
    int32 CurrentNormalTorpedoes = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Ammo",
        meta = (ClampMin = "0"))
    int32 NormalTorpedoCapacity = 6;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Ammo")
    int32 CurrentSpecialTorpedoes = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Ammo",
        meta = (ClampMin = "0"))
    int32 SpecialTorpedoCapacity = 3;

    // -------------------------------------------------------------------------
    //  Reload mode
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Reload")
    ETorpedoReloadMode ReloadMode = ETorpedoReloadMode::Progressive;

    /**
     * [Progressive mode] Time in seconds to reload ONE normal torpedo.
     * Runs continuously while below capacity.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Reload",
        meta = (ClampMin = "0.1", EditCondition = "ReloadMode == ETorpedoReloadMode::Progressive"))
    float ProgressiveReloadCooldown = 8.f;

    /**
     * [Full mode] Time in seconds to reload ALL normal torpedoes at once.
     * Timer starts only when count reaches 0.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Reload",
        meta = (ClampMin = "0.1", EditCondition = "ReloadMode == ETorpedoReloadMode::Full"))
    float FullReloadCooldown = 15.f;

    // -------------------------------------------------------------------------
    //  Fire cooldown
    // -------------------------------------------------------------------------

    /**
     * Shared cooldown between ALL torpedo types.
     * Neither normal nor special can fire during this window.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Cooldowns",
        meta = (ClampMin = "0.0"))
    float FireCooldown = 2.f;

    // -------------------------------------------------------------------------
    //  Timers (read-only, for UI progress bars)
    // -------------------------------------------------------------------------

    /** Remaining shared fire cooldown (0 = can fire) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Cooldowns")
    float FireCooldownRemaining = 0.f;

    /** Remaining reload time (meaning depends on ReloadMode) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Cooldowns")
    float ReloadTimeRemaining = 0.f;

    // -------------------------------------------------------------------------
    //  Active torpedoes (read-only, used by spectator system)
    // -------------------------------------------------------------------------

    /** All currently live torpedoes fired by this submarine */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|State")
    TArray<TObjectPtr<ATorpedoPawn>> ActiveTorpedoes;

    // -------------------------------------------------------------------------
    //  Firing API
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Torpedo")
    ATorpedoPawn* FireNormalTorpedo();

    UFUNCTION(BlueprintCallable, Category = "Torpedo")
    ATorpedoPawn* FireSpecialTorpedo();

    UFUNCTION(BlueprintPure, Category = "Torpedo")
    bool CanFire() const { return FireCooldownRemaining <= 0.f; }

    UFUNCTION(BlueprintPure, Category = "Torpedo")
    bool HasNormalTorpedo() const { return CurrentNormalTorpedoes > 0; }

    UFUNCTION(BlueprintPure, Category = "Torpedo")
    bool HasSpecialTorpedo() const { return CurrentSpecialTorpedoes > 0; }

    UFUNCTION(BlueprintPure, Category = "Torpedo")
    bool IsReadyToFire() const { return CanFire() && (HasNormalTorpedo() || HasSpecialTorpedo()); }

    /**
     * 0..1 ratio of fire cooldown progress (1 = fully ready, 0 = just fired).
     * Safe to use directly as a progress bar value.
     */
    UFUNCTION(BlueprintPure, Category = "Torpedo")
    float GetFireCooldownRatio() const
    {
        return (FireCooldown > 0.f)
            ? FMath::Clamp(1.f - FireCooldownRemaining / FireCooldown, 0.f, 1.f)
            : 1.f;
    }

    /**
     * 0..1 ratio of reload progress (1 = reload complete / not reloading).
     */
    UFUNCTION(BlueprintPure, Category = "Torpedo")
    float GetReloadRatio() const;

    // -------------------------------------------------------------------------
    //  Delegates
    // -------------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnTorpedoFired OnTorpedoFired;

    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnAmmoChanged OnAmmoChanged;

    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnProgressiveReloadComplete OnProgressiveReloadComplete;

    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnFullReloadComplete OnFullReloadComplete;

    /** Fire cooldown just expired */
    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnFireCooldownComplete OnFireCooldownComplete;

    /** Fire cooldown expired AND ammo available — ideal for "ready" HUD flash */
    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnReadyToFire OnReadyToFire;

private:
    // Spawn helper
    ATorpedoPawn* SpawnTorpedo(TSubclassOf<ATorpedoPawn> BlueprintClass,
        UTorpedoCharacteristics* TorpedoDA);

    const USubmarineCharacteristics* GetSubStats() const;

    void TickProgressiveReload(float DeltaTime);
    void TickFullReload(float DeltaTime);

    // Reload internal state
    bool  bReloading = false;
    bool  bWasOnCooldown = false; // edge-detect for fire cooldown delegate
};