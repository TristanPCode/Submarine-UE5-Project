// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineCollisionComponent.h"
#include "SubmarinePawn.h"
#include "TorpedoCharacteristics.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Landscape.h"

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
    if (!OtherActor || !IsValid(OtherActor) || OtherActor->IsTemplate() || OtherActor == GetOwner())
        return;

    const ESubmarineCollisionType ColType = ResolveCollisionType(OtherActor);
    if (ColType == ESubmarineCollisionType::TriggerZone)
        return;

    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return;

    const FCollisionBounceEntry BounceData = Stats->GetCollisionBounce(ColType);

    // Compute bounce direction from positions if normal is invalid
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
//  Overlap processing (Blueprint callable)
// -----------------------------------------------------------------------------

void USubmarineCollisionComponent::ProcessOverlap(AActor* OtherActor)
{
    if (!OtherActor || !IsValid(OtherActor) || OtherActor->IsTemplate() || OtherActor == GetOwner())
        return;

    UE_LOG(LogTemp, Warning, TEXT("ProcessOverlap with: %s"), *OtherActor->GetName());

    const ESubmarineCollisionType ColType = ResolveCollisionType(OtherActor);
    if (ColType == ESubmarineCollisionType::TriggerZone)
        return;

    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return;

    const FCollisionBounceEntry BounceData = Stats->GetCollisionBounce(ColType);

    // Compute bounce direction from actor positions
    FVector Dir = FVector::ZeroVector;
    if (AActor* Owner = GetOwner())
    {
        Dir = Owner->GetActorLocation() - OtherActor->GetActorLocation();
        Dir.Normalize();
    }

    FHitResult FakeHit;
    FakeHit.ImpactNormal = Dir;

    ApplyBounce(FakeHit, BounceData);

    if (ColType == ESubmarineCollisionType::Torpedo)
        ApplyDamage(25.f, OtherActor);

    OnBounced.Broadcast(ColType, Dir);
}

// -----------------------------------------------------------------------------
//  Collision type resolution
// -----------------------------------------------------------------------------

ESubmarineCollisionType USubmarineCollisionComponent::ResolveCollisionType(AActor* OtherActor) const
{
    if (!OtherActor || !IsValid(OtherActor))
        return ESubmarineCollisionType::Default;

    // Torpedo check
    if (OtherActor->ActorHasTag(FName("Torpedo")))
        return ESubmarineCollisionType::Torpedo;

    // Other submarine check
    if (OtherActor->IsA<ASubmarinePawn>() || OtherActor->ActorHasTag(FName("Submarine")))
        return ESubmarineCollisionType::OtherSubmarine;

    // Trigger zone check
    if (OtherActor->ActorHasTag(FName("TriggerZone")))
        return ESubmarineCollisionType::TriggerZone;

    // Landscape check by class name
    if (OtherActor->IsA<ALandscape>())
        return ESubmarineCollisionType::Landscape;

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

    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return;

    // -- SpeedLost reduce CurrentLinearSpeed by percentage -----------------
    if (BounceData.SpeedLost > 0.f)
        OwnerPawn->CurrentLinearSpeed *= (1.f - BounceData.SpeedLost);

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
        const float SpeedScale = Stats->DefaultMult + FMath::Abs(OwnerPawn->CurrentLinearSpeed) / FMath::Max(Stats->BounceSpeedDivisor, 1.f);

        const FVector BounceVelocity = Hit.ImpactNormal * BounceData.BounceForce;
        // We inject directly into CurrentLinearSpeed along forward axis,
        // and a world-Z component for vertical bounce
        /*const float ForwardComponent = FVector::DotProduct(
            BounceVelocity, OwnerPawn->GetActorForwardVector());
        const float VerticalComponent = BounceVelocity.Z;*/

        const FVector LocalBounce =
            OwnerPawn->GetActorTransform().InverseTransformVectorNoScale(BounceVelocity);

        float NewExternalLinearVelocity = OwnerPawn->GetExternalLinearVelocity() + LocalBounce.X * SpeedScale;
        OwnerPawn->SetExternalLinearVelocity(NewExternalLinearVelocity);

        float NewExternalVerticalVelocity = OwnerPawn->GetExternalVerticalVelocity() + LocalBounce.Z;
        OwnerPawn->SetExternalVerticalVelocity(NewExternalVerticalVelocity);

        const bool bHasValidImpactPoint = !Hit.ImpactPoint.IsNearlyZero() &&
            FVector::DistSquared(Hit.ImpactPoint, OwnerPawn->GetActorLocation()) > 1.f;

        if (bHasValidImpactPoint)
        {

            // Injecting rotation bounce based of Moment
            const FVector COM = OwnerPawn->GetActorLocation();
            const FVector ImpactPoint = Hit.ImpactPoint;

            // Lever Arm
            const FVector Lever = ImpactPoint - COM;

            // Torque
            const FVector Torque = FVector::CrossProduct(Lever, BounceVelocity);
            const FVector LocalTorque =
                OwnerPawn->GetActorTransform().InverseTransformVectorNoScale(Torque);

            // Extract axes
            float PitchTorque = LocalTorque.Y;
            float YawTorque = LocalTorque.Z;

            // Clamp
            const float MaxTorque = Stats->MaxTorque;
            PitchTorque = FMath::Clamp(PitchTorque, -MaxTorque, MaxTorque);
            YawTorque = FMath::Clamp(YawTorque, -MaxTorque, MaxTorque);

            const float YawRotationFactor = BounceData.bEnableYawPitchSplitFactors ? BounceData.Collision_YawRotationFactor : BounceData.Collision_RotationFactor;
            const float PitchRotationFactor = BounceData.bEnableYawPitchSplitFactors ? BounceData.Collision_PitchRotationFactor : BounceData.Collision_RotationFactor;

            // Apply
            float NewExternalYawVelocity = OwnerPawn->GetExternalYawVelocity() + YawTorque * YawRotationFactor;
            NewExternalYawVelocity = FMath::Clamp(NewExternalYawVelocity, -Stats->MaxYawSpeed, Stats->MaxYawSpeed);
            OwnerPawn->SetExternalYawVelocity(NewExternalYawVelocity);

            float NewExternalPitchVelocity = OwnerPawn->GetExternalPitchVelocity() - PitchTorque * PitchRotationFactor;
            NewExternalPitchVelocity = FMath::Clamp(NewExternalPitchVelocity, -Stats->MaxVerticalSpeed, Stats->MaxVerticalSpeed);
            OwnerPawn->SetExternalPitchVelocity(NewExternalPitchVelocity);
        }
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