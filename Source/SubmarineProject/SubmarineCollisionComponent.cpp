// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineCollisionComponent.h"
#include "SubmarinePawn.h"
#include "TorpedoPawn.h"
#include "TorpedoCharacteristics.h"
#include "SubmarineCharacteristics.h"
#include "NiagaraFunctionLibrary.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"  // FOverlapResult
#include "EngineUtils.h"          // TActorIterator
#include "Engine/World.h"        // GetWorld, Overlap
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Landscape.h"
#include "LandscapeStreamingProxy.h"


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
    if (bDead || CurrentHealth <= 0.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CollisionComp] ApplyDamage ignored — already dead. Owner=%s"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
        return;
    }

    const USubmarineCharacteristics* Stats = GetStats();
    const float Resistance = Stats ? Stats->DamageResistance : 1.f;
    const float FinalDamage = RawDamage * Resistance;

    UE_LOG(LogTemp, Warning,
        TEXT("[CollisionComp] %s takes %.1f damage (raw=%.1f resist=%.2f) from %s | HP: %.1f -> %.1f"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
        FinalDamage, RawDamage, Resistance,
        DamageCauser ? *DamageCauser->GetName() : TEXT("None"),
        CurrentHealth, FMath::Max(0.f, CurrentHealth - FinalDamage));

    CurrentHealth = FMath::Max(0.f, CurrentHealth - FinalDamage);
    OnDamaged.Broadcast(FinalDamage, DamageCauser);

    if (CurrentHealth <= 0.f)
    {
        bDead = true;
        UE_LOG(LogTemp, Warning, TEXT("[CollisionComp] %s DIED — broadcasting OnDied"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("?"));
        ASubmarinePawn* OwnerPawn = Cast<ASubmarinePawn>(GetOwner());
        OnDied.Broadcast(OwnerPawn, DamageCauser);
        TriggerDeathExplosion();
    }
}

void USubmarineCollisionComponent::ApplySplashDamage(float RawDamage, AActor* DamageCauser,
    AActor* FiringShooter)
{
    if (bDead || CurrentHealth <= 0.f) return;

    const USubmarineCharacteristics* MyStats = GetStats();

    // Check: is this submarine immune to its own torpedo splash?
    if (FiringShooter && FiringShooter == GetOwner())
    {
        // The torpedo was fired by this submarine
        // Check torpedo DA bCanSelfDamage
        bool bCanSelfDamage = false;
        if (ATorpedoPawn* Torpedo = Cast<ATorpedoPawn>(DamageCauser))
        {
            if (Torpedo->Characteristics)
                bCanSelfDamage = Torpedo->Characteristics->bCanSelfDamage;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[CollisionComp] Self-splash check on %s: bCanSelfDamage=%d bImmuneToOwnTorpedoSplash=%d"),
            *GetOwner()->GetName(),
            bCanSelfDamage ? 1 : 0,
            (MyStats && MyStats->bImmuneToOwnTorpedoSplash) ? 1 : 0);

        if (!bCanSelfDamage) return;

        // Also check submarine-level immunity
        if (MyStats && MyStats->bImmuneToOwnTorpedoSplash) return;
    }

    ApplyDamage(RawDamage, DamageCauser);
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

    // Torpedoes pass THROUGH the firing submarine — handled at spawn via IgnoreActor.
    // If a torpedo somehow still reaches ProcessHit, skip bounce entirely.
    if (ColType == ESubmarineCollisionType::Torpedo)
    {
        ATorpedoPawn* Torpedo = Cast<ATorpedoPawn>(OtherActor);
        if (Torpedo && Torpedo->FiringShooter == GetOwner())
            return;
    }

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

    // Apply damage
    if (BounceData.CollisionDamage > 0.f)
        ApplyDamage(BounceData.CollisionDamage, OtherActor);
 
    // Only bounce if submarine survived the damage
    if (!bDead)
        ApplyBounce(AdjustedHit, BounceData);

    OnBounced.Broadcast(ColType, AdjustedHit.ImpactNormal);
}

// -----------------------------------------------------------------------------
//  Overlap processing (Blueprint callable)
// -----------------------------------------------------------------------------

void USubmarineCollisionComponent::ProcessOverlap(AActor* OtherActor)
{
    if (!OtherActor || !IsValid(OtherActor) || OtherActor->IsTemplate() || OtherActor == GetOwner())
        return;

    const ESubmarineCollisionType ColType = ResolveCollisionType(OtherActor);
    if (ColType == ESubmarineCollisionType::TriggerZone)
        return;

    // Same torpedo passthrough check
    if (ColType == ESubmarineCollisionType::Torpedo)
    {
        ATorpedoPawn* Torpedo = Cast<ATorpedoPawn>(OtherActor);
        if (Torpedo && Torpedo->FiringShooter == GetOwner())
            return;
    }

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

    // Apply damage
    if (BounceData.CollisionDamage > 0.f)
        ApplyDamage(BounceData.CollisionDamage, OtherActor);

    // Only bounce if submarine survived the damage
    if (!bDead)
        ApplyBounce(FakeHit, BounceData);

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

    // Check both ALandscape and ALandscapeStreamingProxy
    // (large/streamed landscapes use proxy actors for each chunk)
    if (OtherActor->IsA<ALandscape>() || OtherActor->IsA<ALandscapeStreamingProxy>())
        return ESubmarineCollisionType::Landscape;

    // Also check by actor tag for Blueprint-placed landscape stand-ins
    if (OtherActor->ActorHasTag(FName("Landscape")))
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
//  Death explosion
// -----------------------------------------------------------------------------

void USubmarineCollisionComponent::TriggerDeathExplosion()
{
    ASubmarinePawn* OwnerPawn = Cast<ASubmarinePawn>(GetOwner());
    if (!OwnerPawn) return;

    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return;

    const FVector ExplosionLocation = OwnerPawn->GetActorLocation();

    // Niagara VFX
    if (Stats->DeathExplosionEffect)
    {
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            OwnerPawn->GetWorld(),
            Stats->DeathExplosionEffect,
            ExplosionLocation,
            FRotator::ZeroRotator,
            FVector(Stats->DeathExplosionEffectScale),
            true, true, ENCPoolMethod::None);
    }

    // Splash damage — deduplicated by actor
    if (Stats->DeathExplosionRadius > 0.f)
    {
        TArray<FOverlapResult> Overlaps;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(Stats->DeathExplosionRadius);
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(OwnerPawn);

        OwnerPawn->GetWorld()->OverlapMultiByChannel(
            Overlaps, ExplosionLocation, FQuat::Identity, ECC_Pawn, Sphere, Params);

        // Deduplicate: one damage application per actor
        TSet<AActor*> DamagedActors;

        for (const FOverlapResult& Overlap : Overlaps)
        {
            AActor* Target = Overlap.GetActor();
            if (!Target || Target == OwnerPawn) continue;
            if (DamagedActors.Contains(Target)) continue;

            if (USubmarineCollisionComponent* TargetCol =
                Target->FindComponentByClass<USubmarineCollisionComponent>())
            {
                const float Dist = FVector::Dist(ExplosionLocation, Target->GetActorLocation());
                const float Dmg = Stats->ComputeDeathSplashDamage(Dist);
                if (Dmg > 0.f)
                {
                    TargetCol->ApplyDamage(Dmg, OwnerPawn);
                    DamagedActors.Add(Target);
                }
            }
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