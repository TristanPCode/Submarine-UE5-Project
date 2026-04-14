#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TorpedoCharacteristics.h"   // ETorpedoType
#include "SubmarineTorpedoComponent.generated.h"

class ATorpedoPawn;
class USubmarineCharacteristics;

// Fired when a torpedo is launched
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTorpedoFired,
    ETorpedoType, TorpedoType,
    ATorpedoPawn*, TorpedoActor);

// Fired when normal torpedo count changes (for UI)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChanged,
    int32, NormalCount,
    int32, SpecialCount);

// Fired when reload completes
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReloadComplete);

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

    // -- Blueprint class references (set in BP) ----------------------------

    /**
     * Blueprint subclass of ATorpedoPawn used for normal torpedoes.
     * Assign BP_TorpedoNormal (or Light/Heavy variant) here in the submarine Blueprint.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Ammo")
    TSubclassOf<ATorpedoPawn> NormalTorpedoBlueprintClass;

    /**
     * Blueprint subclass of ATorpedoPawn used for special torpedoes.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Ammo")
    TSubclassOf<ATorpedoPawn> SpecialTorpedoBlueprintClass;

    // -- DataAsset references (set in BP) ----------------------------------

    /** Characteristics for normal torpedoes */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Ammo")
    TObjectPtr<UTorpedoCharacteristics> NormalTorpedoCharacteristics;

    /** Characteristics for special torpedoes */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Ammo")
    TObjectPtr<UTorpedoCharacteristics> SpecialTorpedoCharacteristics;

    // -- Ammo state (read-only from Blueprint) -----------------------------

    /** Current number of normal torpedoes available */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Ammo")
    int32 CurrentNormalTorpedoes = 0;

    /** Maximum normal torpedo capacity */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Ammo",
        meta = (ClampMin = "0"))
    int32 NormalTorpedoCapacity = 4;

    /** Current number of special torpedoes available */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Ammo")
    int32 CurrentSpecialTorpedoes = 0;

    /** Maximum special torpedo capacity */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Ammo",
        meta = (ClampMin = "0"))
    int32 SpecialTorpedoCapacity = 2;

    // -- Cooldowns (set in submarine DA or directly here) ------------------

    /**
     * Shared fire cooldown in seconds.
     * Applies after firing ANY torpedo type — you cannot fire normal then
     * immediately fire special during this window.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Cooldowns",
        meta = (ClampMin = "0.0"))
    float FireCooldown = 2.f;

    /**
     * Time in seconds to reload ONE normal torpedo.
     * Reload is continuous: when a slot is empty it starts filling.
     * Special torpedoes never reload.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo|Cooldowns",
        meta = (ClampMin = "0.1"))
    float ReloadNormalTorpedoCooldown = 8.f;

    // -- Timers (read-only, useful for UI progress bars) -------------------

    /** Remaining fire cooldown (0 = ready to fire) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Cooldowns")
    float FireCooldownRemaining = 0.f;

    /** Remaining reload time for the next normal torpedo slot */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Cooldowns")
    float ReloadTimeRemaining = 0.f;

    // -- Firing API --------------------------------------------------------

    /**
     * Fire a normal torpedo. Returns the spawned pawn (or nullptr if failed).
     * Failure reasons: no ammo, cooldown active, no Blueprint class assigned.
     */
    UFUNCTION(BlueprintCallable, Category = "Torpedo")
    ATorpedoPawn* FireNormalTorpedo();

    /**
     * Fire a special torpedo. Returns the spawned pawn (or nullptr if failed).
     */
    UFUNCTION(BlueprintCallable, Category = "Torpedo")
    ATorpedoPawn* FireSpecialTorpedo();

    /** True if the shared fire cooldown has expired */
    UFUNCTION(BlueprintPure, Category = "Torpedo")
    bool CanFire() const { return FireCooldownRemaining <= 0.f; }

    /** True if there are normal torpedoes in stock */
    UFUNCTION(BlueprintPure, Category = "Torpedo")
    bool HasNormalTorpedo() const { return CurrentNormalTorpedoes > 0; }

    /** True if there are special torpedoes in stock */
    UFUNCTION(BlueprintPure, Category = "Torpedo")
    bool HasSpecialTorpedo() const { return CurrentSpecialTorpedoes > 0; }

    // -- Events (bind in Blueprint for HUD / feedback) --------------------

    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnTorpedoFired OnTorpedoFired;

    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnAmmoChanged OnAmmoChanged;

    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnReloadComplete OnReloadComplete;

private:
    // -- Spawn helper -------------------------------------------------------
    ATorpedoPawn* SpawnTorpedo(TSubclassOf<ATorpedoPawn> BlueprintClass,
        UTorpedoCharacteristics* TorpedoDA);

    // -- Submarine stat accessor -------------------------------------------
    const USubmarineCharacteristics* GetSubStats() const;

    // -- Reload internal state ---------------------------------------------
    bool bReloading = false;
};