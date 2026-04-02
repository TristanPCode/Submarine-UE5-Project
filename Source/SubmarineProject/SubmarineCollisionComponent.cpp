// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineCollisionComponent.h"
#include "SubmarinePawn.h"
#include "TorpedoCharacteristics.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

USubmarineCollisionComponent::USubmarineCollisionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void USubmarineCollisionComponent::BeginPlay()
{
    Super::BeginPlay();

    // Initialize health from characteristics
    if (const USubmarineCharacteristics* Stats = GetStats())
        CurrentHealth = Stats->MaxHealth;
}

// -----------------------------------------------------------------------------
//  Health
// -----------------------------------------------------------------------------

float USubmarineCollisionComponent::GetHealthRatio() const
{
    if (const USubmarineCharacteristics* Stats = GetStats())
        return FMath::Clamp(CurrentHealth / FMath::Max(Stats->MaxHealth, 1.f), 0.f, 1.f);
    return 1.f;
}

void USubmarineCollisionComponent::ApplyDamage(float RawDamage, AActor* DamageCauser)
{
    if (CurrentHealth <= 0.f) return;

    const float Resistance = GetStats() ? GetStats()->DamageResistance : 1.f;
    const float FinalDamage = RawDamage * Resistance;

    CurrentHealth = FMath::Max(0.f, CurrentHealth - FinalDamage);
    OnDamaged.Broadcast(FinalDamage, DamageCauser);
}

// -----------------------------------------------------------------------------
//  Hit processing
// -----------------------------------------------------------------------------

void USubmarineCollisionComponent::ProcessHit(const FHitResult& Hit, AActor* OtherActor)
{
    if (!OtherActor || !IsValid(OtherActor) || OtherActor->IsTemplate())
        return;

    const ESubmarineCollisionType ColType = ResolveCollisionType(OtherActor);

    if (ColType == ESubmarineCollisionType::TriggerZone)
        return;

    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return;

    const FCollisionBounceEntry BounceData = Stats->GetCollisionBounce(ColType);

    // Compute bounce direction from other actor position if normal is invalid
    FHitResult AdjustedHit = Hit;
    if (Hit.ImpactNormal.IsNearlyZero())
    {
        AActor* Owner = GetOwner();
        if (Owner && OtherActor)
        {
            FVector Dir = Owner->GetActorLocation() - OtherActor->GetActorLocation();
            Dir.Normalize();
            AdjustedHit.ImpactNormal = Dir;
        }
    }

    ApplyBounce(AdjustedHit, BounceData);

    if (ColType == ESubmarineCollisionType::Torpedo)
        ApplyDamage(25.f, OtherActor);

    OnBounced.Broadcast(ColType, AdjustedHit.ImpactNormal);
}

// -----------------------------------------------------------------------------
//  Collision type resolution
// -----------------------------------------------------------------------------

ESubmarineCollisionType USubmarineCollisionComponent::ResolveCollisionType(AActor* OtherActor) const
{
    if (!OtherActor)
        return ESubmarineCollisionType::Default;

    // Landscape check by class name (avoids Landscape module dependency issues)
    const FString ClassName = OtherActor->GetClass()->GetName();
    if (ClassName.Contains(TEXT("Landscape")))
        return ESubmarineCollisionType::Landscape;

    // Torpedo check — by actor tag (add "Torpedo" tag to your torpedo Blueprint)
    if (OtherActor->ActorHasTag(FName("Torpedo")))
        return ESubmarineCollisionType::Torpedo;

    // Other submarine check — by actor tag or class
    if (OtherActor->IsA<ASubmarinePawn>() || OtherActor->ActorHasTag(FName("Submarine")))
        return ESubmarineCollisionType::OtherSubmarine;

    // Trigger zone check — by actor tag
    if (OtherActor->ActorHasTag(FName("TriggerZone")))
        return ESubmarineCollisionType::TriggerZone;

    // Everything else is a static obstacle
    return ESubmarineCollisionType::StaticObstacle;
}

// -----------------------------------------------------------------------------
//  Bounce application
// -----------------------------------------------------------------------------

void USubmarineCollisionComponent::ApplyBounce(const FHitResult& Hit,
    const FCollisionBounceEntry& BounceData)
{
    if (BounceData.BounceForce <= 0.f && BounceData.SpeedStatePenalty == 0)
        return;

    ASubmarinePawn* OwnerPawn = Cast<ASubmarinePawn>(GetOwner());
    if (!OwnerPawn) return;

    // -- Speed state penalty ------------------------------------------------
    // Move the linear state toward Stand by the penalty amount
    if (BounceData.SpeedStatePenalty > 0)
    {
        const int32 Current = static_cast<int32>(OwnerPawn->LinearSpeedState);
        const int32 StandIdx = static_cast<int32>(ELinearSpeedState::Stand);
        int32 Next = Current;

        if (Current > StandIdx)
            Next = FMath::Max(StandIdx, Current - BounceData.SpeedStatePenalty);
        else if (Current < StandIdx)
            Next = FMath::Min(StandIdx, Current + BounceData.SpeedStatePenalty);

        OwnerPawn->LinearSpeedState = static_cast<ELinearSpeedState>(Next);
    }

    // -- Bounce impulse -----------------------------------------------------
    // Apply an instant velocity kick along the impact normal
    if (BounceData.BounceForce > 0.f)
    {
        const FVector BounceVelocity = Hit.ImpactNormal * BounceData.BounceForce;
        // We inject directly into CurrentLinearSpeed along forward axis,
        // and a world-Z component for vertical bounce
        const float ForwardComponent = FVector::DotProduct(
            BounceVelocity, OwnerPawn->GetActorForwardVector());
        const float VerticalComponent = BounceVelocity.Z;

        OwnerPawn->CurrentLinearSpeed += ForwardComponent;
        OwnerPawn->CurrentVerticalSpeed += VerticalComponent;
    }
}

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------

const USubmarineCharacteristics* USubmarineCollisionComponent::GetStats() const
{
    if (const ASubmarinePawn* OwnerPawn = Cast<ASubmarinePawn>(GetOwner()))
        return OwnerPawn->Characteristics
        ? OwnerPawn->Characteristics.Get()
        : GetDefault<USubmarineCharacteristics>();
    return GetDefault<USubmarineCharacteristics>();
}