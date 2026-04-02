#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SubmarineCharacteristics.h"
#include "SubmarineCollisionComponent.generated.h"

class UTorpedoCharacteristics;

// Broadcast when the submarine takes damage, so health system can listen
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSubmarineDamaged,
    float, DamageAmount,
    AActor*, DamageCauser);

// Broadcast on any collision bounce
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSubmarineBounced,
    ESubmarineCollisionType, CollisionType,
    FVector, BounceDirection);

UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API USubmarineCollisionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USubmarineCollisionComponent();

    virtual void BeginPlay() override;

    // -- Health --------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Health")
    float CurrentHealth = 1000.f;

    /** Returns current health as a 0..1 ratio */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Health")
    float GetHealthRatio() const;

    /** Apply damage to the submarine (respects DamageResistance from DA) */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Health")
    void ApplyDamage(float RawDamage, AActor* DamageCauser);

    // -- Delegates -----------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Submarine|Events")
    FOnSubmarineDamaged OnDamaged;

    UPROPERTY(BlueprintAssignable, Category = "Submarine|Events")
    FOnSubmarineBounced OnBounced;

    // -- Called by SubmarinePawn on hit --------------------------------------

    /**
     * Main entry point for collision processing.
     * SubmarinePawn calls this from its OnHit callback.
     */
    void ProcessHit(const FHitResult& Hit, AActor* OtherActor);

private:
    // Cached reference to the owning pawn's characteristics
    const USubmarineCharacteristics* GetStats() const;

    // Determine collision type from the other actor
    ESubmarineCollisionType ResolveCollisionType(AActor* OtherActor) const;

    // Apply bounce impulse and speed state penalty to the owning pawn
    void ApplyBounce(const FHitResult& Hit, const FCollisionBounceEntry& BounceData);
};