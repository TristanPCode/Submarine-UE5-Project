#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SubmarineCharacteristics.h"
#include "SubmarineCollisionComponent.generated.h"

// Broadcast when the submarine takes damage
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSubmarineDamaged,
    float, DamageAmount,
    AActor*, DamageCauser);

// Broadcast on any collision bounce
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSubmarineBounced,
    ESubmarineCollisionType, CollisionType,
    FVector, BounceDirection);

// Broadcast on Submarine death
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

    /**
     * Apply torpedo splash damage to this submarine.
     * Checks bImmuneToOwnTorpedoSplash and the torpedo's bCanSelfDamage
     * before applying. Pass the firing submarine as FiringSubmarine so the
     * immunity check works correctly.
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Collision")
    void ApplySplashDamage(float RawDamage, AActor* DamageCauser, AActor* FiringSubmarine);

private:
    const USubmarineCharacteristics* GetStats() const;
    ESubmarineCollisionType ResolveCollisionType(AActor* OtherActor) const;
    void ApplyBounce(const FHitResult& Hit, const FCollisionBounceEntry& BounceData);
    void TriggerDeathExplosion();

    bool bDead = false;
};