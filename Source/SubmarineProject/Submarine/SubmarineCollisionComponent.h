#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SubmarineCharacteristics.h"
//#include "SubmarineDelegates.h"
#include "SubmarineCollisionComponent.generated.h"


// -----------------------------------------------------------------------
//  Collision delegates
// -----------------------------------------------------------------------

/** Broadcast when the submarine takes damage. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSubmarineDamaged,
    float, DamageAmount,
    AActor*, DamageCauser);

/** Broadcast on any collision bounce. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSubmarineBounced,
    ESubmarineCollisionType, CollisionType,
    FVector, BounceDirection);

/** Broadcast on submarine death. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSubmarineDied,
    ASubmarinePawn*, DeadSubmarine,
    AActor*, Killer);

UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API USubmarineCollisionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USubmarineCollisionComponent();

    virtual void BeginPlay() override;

    // -- Health ------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Health")
    float CurrentHealth = 100.f;

    /** Returns current health as a 0..1 ratio */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Health")
    float GetHealthRatio() const;

    /** Apply damage to the submarine (respects DamageResistance from DA) */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Health")
    void ApplyDamage(float RawDamage, AActor* DamageCauser);

    /**
     * Apply torpedo splash damage to this submarine.
     * Checks bImmuneToOwnTorpedoSplash and the torpedo's bCanSelfDamage
     * before applying. Pass the firing submarine as FiringSubmarine so the
     * immunity check works correctly.
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Collision")
    void ApplySplashDamage(float RawDamage, AActor* DamageCauser, AActor* FiringShooter);

    // -- Delegates ---------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Submarine|Events")
    FOnSubmarineDamaged OnDamaged;

    UPROPERTY(BlueprintAssignable, Category = "Submarine|Events")
    FOnSubmarineBounced OnBounced;

    UPROPERTY(BlueprintAssignable, Category = "Submarine|Events")
    FOnSubmarineDied OnDied;

    // -- Collision processing ----------------------------------------------

    /** Full hit processing with FHitResult (called from C++) */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Collision")
    void ProcessHit(const FHitResult& Hit, AActor* OtherActor);

    /** Simplified overlap processing callable from Blueprint */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Collision")
    void ProcessOverlap(AActor* OtherActor);

    // -----------------------------------------------------------------------
    //  Blueprint hit forwarding (called from SubmarinePawn Blueprint)
    //
    //  These were previously on ASubmarinePawn. They live here because
    //  collision logic belongs in the collision component.
    //  SubmarinePawn now delegates to these via thin wrappers.
    // -----------------------------------------------------------------------

    /**
     * Call from Blueprint Event Hit -> feeds the anti-stuck tracker.
     * Drag the 'Hit' pin from Event Hit directly into this function.
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Collision")
    void RegisterHitFromBlueprint(const FHitResult& Hit);

    /**
     * Call from Blueprint Event Hit -> processes the hit through the
     * collision system (damage, bounce) when bEnableBluePrintCollisions=true.
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Collision")
    void HandleHitFromBlueprint(const FHitResult& Hit);

    /**
     * Two-phase rotation contact detection for in-place yaw/pitch.
     *
     * Phase 1 — Box overlap (fast): uses SubmarineBody's mesh bounds.
     *   Quickly finds candidate actors overlapping the hull AABB.
     *
     * Phase 2 — Mesh overlap (accurate): for each candidate from Phase 1,
     *   runs ComponentOverlapComponent against the actual physics body.
     *   Eliminates false positives from the AABB corners.
     *
     * Ghost actors (tagged "ReplayGhost") are skipped entirely.
     * If a real contact is confirmed, synthesises a FHitResult and
     * forwards it through RegisterHitFromBlueprint + HandleHitFromBlueprint.
     * Damage and bounce subject to per-actor cooldowns.
     *
     * @param RotationAxis  0 = yaw, 1 = pitch (for logging only)
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Collision")
    void CheckRotationContactBP(int32 RotationAxis);

    // -----------------------------------------------------------------------
    //  Anti-stuck & Hit tracker
    //  Tracks continuous contact duration per actor to detect stuck scenarios.
    // -----------------------------------------------------------------------

    void RegisterHit(AActor* OtherActor, const FHitResult& Hit);

    bool bDead = false;

private:
    const USubmarineCharacteristics* GetStats() const;

    ESubmarineCollisionType ResolveCollisionType(AActor* OtherActor) const;

    // -----------------------------------------------------------------------
    //  Cooldown helpers
    // -----------------------------------------------------------------------

    /** Returns true if the actor is exempt from the damage cooldown. */
    bool IsDamageCooldownExempt(AActor* OtherActor) const;

    /** Returns true if the actor is exempt from the bounce cooldown. */
    bool IsBounceCooldownExempt(AActor* OtherActor) const;

    /** Returns true if enough time has passed to apply damage from this actor. */
    bool CanApplyDamageTo(AActor* OtherActor) const;

    /** Returns true if enough time has passed to apply bounce from this actor. */
    bool CanApplyBounceTo(AActor* OtherActor) const;

    /** Records the current time as the last damage application time for this actor. */
    void RecordDamageTime(AActor* OtherActor);

    /** Records the current time as the last bounce application time for this actor. */
    void RecordBounceTime(AActor* OtherActor);

    // -----------------------------------------------------------------------
    //  Collision internals
    // -----------------------------------------------------------------------
    void ApplyBounce(const FHitResult& Hit, const FCollisionBounceEntry& BounceData);
    void TriggerDeathExplosion();

    // -----------------------------------------------------------------------
    //  Anti-stuck internal state
    // -----------------------------------------------------------------------

    struct FContactState
    {
        // Anti-stuck fields
        float FirstHitTime = 0.f;
        float LastHitTime = 0.f;
        float LastExpulsionTime = -999.f;
        FVector NormalSum = FVector::ZeroVector;
        int32   NormalCount = 0;

        // Cooldown fields
        float LastDamageTime = -999.f;
        float LastBounceTime = -999.f;
    };

    TMap<AActor*, FContactState> ContactStates;

    void TickAntiStuck();   // called from CheckRotationContactBP / ProcessHit as needed
};