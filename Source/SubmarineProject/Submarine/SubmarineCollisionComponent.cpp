// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineCollisionComponent.h"
#include "SubmarinePawn.h"
#include "SubmarinePhysicsComponent.h"
#include "TorpedoPawn.h"
#include "TorpedoCharacteristics.h"
#include "SubmarineCharacteristics.h"
#include "Replay/ReplayHelpers.h"
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
    const USubmarineCharacteristics* Stats = GetStats();

    if (bDead || CurrentHealth <= 0.f)
    {
        if (Stats->bLogCollisionComp) {
            UE_LOG(LogTemp, Warning, TEXT("[CollisionComp] ApplyDamage ignored — already dead. Owner=%s"),
                GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
        }
        return;
    }

    const float Resistance = Stats ? Stats->DamageResistance : 1.f;
    const float FinalDamage = RawDamage * Resistance;

    if (Stats->bLogCollisionComp) {
        UE_LOG(LogTemp, Warning,
            TEXT("[CollisionComp] %s takes %.1f damage (raw=%.1f resist=%.2f) from %s | HP: %.1f -> %.1f"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
            FinalDamage, RawDamage, Resistance,
            DamageCauser ? *DamageCauser->GetName() : TEXT("None"),
            CurrentHealth, FMath::Max(0.f, CurrentHealth - FinalDamage));
    }

    CurrentHealth = FMath::Max(0.f, CurrentHealth - FinalDamage);
    OnDamaged.Broadcast(FinalDamage, DamageCauser);

    if (CurrentHealth <= 0.f)
    {
        bDead = true;
        if (Stats->bLogCollisionComp) {
            UE_LOG(LogTemp, Warning, TEXT("[CollisionComp] %s DIED — broadcasting OnDied"),
                GetOwner() ? *GetOwner()->GetName() : TEXT("?"));
        }
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

        if (MyStats->bLogCollisionComp) {
            UE_LOG(LogTemp, Warning,
                TEXT("[CollisionComp] Self-splash check on %s: bCanSelfDamage=%d bImmuneToOwnTorpedoSplash=%d"),
                *GetOwner()->GetName(),
                bCanSelfDamage ? 1 : 0,
                (MyStats && MyStats->bImmuneToOwnTorpedoSplash) ? 1 : 0);
        }

        if (!bCanSelfDamage) return;

        // Also check submarine-level immunity
        if (MyStats && MyStats->bImmuneToOwnTorpedoSplash) return;
    }

    ApplyDamage(RawDamage, DamageCauser);
}

// -----------------------------------------------------------------------------
//  Cooldown helpers
// -----------------------------------------------------------------------------

bool USubmarineCollisionComponent::IsDamageCooldownExempt(AActor* OtherActor) const
{
    if (!OtherActor) return false;
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return false;

    for (const TSubclassOf<AActor>& ExemptClass : Stats->DamageCooldownExemptClasses)
        if (ExemptClass && OtherActor->IsA(ExemptClass))
            return true;

    return false;
}

bool USubmarineCollisionComponent::IsBounceCooldownExempt(AActor* OtherActor) const
{
    if (!OtherActor) return false;
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return false;

    for (const TSubclassOf<AActor>& ExemptClass : Stats->BounceCooldownExemptClasses)
        if (ExemptClass && OtherActor->IsA(ExemptClass))
            return true;

    return false;
}

bool USubmarineCollisionComponent::CanApplyDamageTo(AActor* OtherActor) const
{
    if (IsDamageCooldownExempt(OtherActor)) return true;

    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats || Stats->DamageCooldown <= 0.f) return true;

    const FContactState* State = ContactStates.Find(OtherActor);
    if (!State) return true;

    return (GetWorld()->GetTimeSeconds() - State->LastDamageTime) >= Stats->DamageCooldown;
}

bool USubmarineCollisionComponent::CanApplyBounceTo(AActor* OtherActor) const
{
    if (IsBounceCooldownExempt(OtherActor)) return true;

    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats || Stats->BounceCooldown <= 0.f) return true;

    const FContactState* State = ContactStates.Find(OtherActor);
    if (!State) return true;

    return (GetWorld()->GetTimeSeconds() - State->LastBounceTime) >= Stats->BounceCooldown;
}

void USubmarineCollisionComponent::RecordDamageTime(AActor* OtherActor)
{
    if (!OtherActor) return;
    FContactState& State = ContactStates.FindOrAdd(OtherActor);
    State.LastDamageTime = GetWorld()->GetTimeSeconds();
}

void USubmarineCollisionComponent::RecordBounceTime(AActor* OtherActor)
{
    if (!OtherActor) return;
    FContactState& State = ContactStates.FindOrAdd(OtherActor);
    State.LastBounceTime = GetWorld()->GetTimeSeconds();
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
    if (BounceData.CollisionDamage > 0.f && CanApplyDamageTo(OtherActor))
        ApplyDamage(BounceData.CollisionDamage, OtherActor);
 
    // Only bounce if submarine survived the damage
    if (!bDead && CanApplyBounceTo(OtherActor))
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
    if (BounceData.CollisionDamage > 0.f && CanApplyDamageTo(OtherActor))
        ApplyDamage(BounceData.CollisionDamage, OtherActor);

    // Only bounce if submarine survived the damage
    if (!bDead && CanApplyBounceTo(OtherActor))
        ApplyBounce(FakeHit, BounceData);

    OnBounced.Broadcast(ColType, Dir);
}

// -----------------------------------------------------------------------------
//  Blueprint hit forwarding (moved from SubmarinePawn)
// -----------------------------------------------------------------------------

void USubmarineCollisionComponent::RegisterHitFromBlueprint(const FHitResult& Hit)
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats || !Stats->bEnableBluePrintCollisions) return;
    if (!Hit.GetActor() || Hit.GetActor() == GetOwner()) return;
    if (Hit.GetActor()->ActorHasTag(UReplayHelpers::Tag_ReplayGhost)) return;

    RegisterHit(Hit.GetActor(), Hit);
}

void USubmarineCollisionComponent::HandleHitFromBlueprint(const FHitResult& Hit)
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats || !Stats->bEnableBluePrintCollisions) return;
    if (!Hit.IsValidBlockingHit() || !Hit.GetActor()) return;
    if (Hit.GetActor()->ActorHasTag(UReplayHelpers::Tag_ReplayGhost)) return;

    ProcessHit(Hit, Hit.GetActor());
}

// -----------------------------------------------------------------------------
//  CheckRotationContactBP
//  Two-phase contact detection for in-place yaw/pitch rotations.
//
//  Phase 1 — Box overlap using SubmarineBody's mesh AABB (fast broad phase).
//  Phase 2 — ComponentOverlapComponent against each candidate's actual physics
//             body (accurate narrow phase, eliminates AABB corner false positives).
//
//  On confirmed contact: synthesises a FHitResult including ImpactPoint
//  (set to the closest point on the other component's bounds, not just the
//  actor's origin) so that ApplyBounce can compute the correct lever arm
//  for angular impulse AND the linear bounce is applied just like a normal hit.
// -----------------------------------------------------------------------------

void USubmarineCollisionComponent::CheckRotationContactBP(int32 RotationAxis)
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats || !Stats->bEnableBluePrintCollisions) return;

    ASubmarinePawn* OwnerPawn = Cast<ASubmarinePawn>(GetOwner());
    if (!OwnerPawn || OwnerPawn->bDead) return;

    UStaticMeshComponent* Body = OwnerPawn->GetSubmarineBody();
    if (!Body) return;

    UWorld* World = GetWorld();
    if (!World) return;

    // -----------------------------------------------------------------------
    //  Phase 1: Box broad-phase using mesh AABB
    // -----------------------------------------------------------------------
    const FBoxSphereBounds MeshBounds = Body->Bounds;
    const FVector  BoxHalfExtent = MeshBounds.BoxExtent;
    const FVector  BoxCenter = MeshBounds.Origin;

    TArray<FOverlapResult> BoxOverlaps;
    FCollisionQueryParams BoxParams;
    BoxParams.AddIgnoredActor(OwnerPawn);
    // Ghost actors skipped here — don't add them to query, just skip in loop

    World->OverlapMultiByChannel(
        BoxOverlaps,
        BoxCenter,
        OwnerPawn->GetActorQuat(),
        ECC_Pawn,
        FCollisionShape::MakeBox(BoxHalfExtent),
        BoxParams);

    if (BoxOverlaps.Num() == 0) return;

    // -----------------------------------------------------------------------
    //  Phase 2: Mesh narrow-phase per candidate
    // -----------------------------------------------------------------------
    for (const FOverlapResult& BoxOvR : BoxOverlaps)
    {
        AActor* Other = BoxOvR.GetActor();
        if (!Other || Other == OwnerPawn || !IsValid(Other)) continue;

        // Skip ghost actors
        if (Other->ActorHasTag(UReplayHelpers::Tag_ReplayGhost)) continue;

        // Skip our own firing torpedoes
        if (ATorpedoPawn* T = Cast<ATorpedoPawn>(Other))
            if (T->FiringShooter == OwnerPawn) continue;

        UPrimitiveComponent* OtherComp = BoxOvR.GetComponent();
        if (!OtherComp) continue;

        // Narrow-phase: does the actual mesh overlap this component?
        TArray<FOverlapResult> MeshOverlaps;
        FComponentQueryParams MeshParams;
        MeshParams.AddIgnoredActor(OwnerPawn);

        const bool bMeshContact =
            Body->ComponentOverlapComponent(
                OtherComp,
                BoxCenter,
                OwnerPawn->GetActorQuat(),
                MeshParams
            );

        if (!bMeshContact) continue;

        // -----------------------------------------------------------------------
        //  Confirmed contact — build a FHitResult with a meaningful ImpactPoint.
        //  We use the closest point on the other component's AABB to our centre
        //  as the ImpactPoint, giving ApplyBounce a real lever arm.
        // -----------------------------------------------------------------------
        const FBoxSphereBounds OtherBounds = OtherComp->Bounds;
        const FVector ClosestPoint = OtherBounds.GetBox().GetClosestPointTo(BoxCenter);

        FHitResult SynthHit;
        SynthHit.bBlockingHit = true;
        SynthHit.HitObjectHandle = FActorInstanceHandle(Other);
        SynthHit.Component = OtherComp;
        SynthHit.ImpactPoint = ClosestPoint;
        SynthHit.Location = BoxCenter;

        // Impact normal: push away from the contact point
        FVector Dir = BoxCenter - ClosestPoint;
        SynthHit.ImpactNormal = Dir.IsNearlyZero()
            ? (BoxCenter - Other->GetActorLocation()).GetSafeNormal()
            : Dir.GetSafeNormal();
        SynthHit.Normal = SynthHit.ImpactNormal;

        if (Stats->bLogCollisionComp) {
            UE_LOG(LogTemp, Warning,
                TEXT("[RotContact-%s] Confirmed mesh contact with '%s'"),
                RotationAxis == 0 ? TEXT("Yaw") : TEXT("Pitch"),
                *Other->GetName());
        }

        // Feed anti-stuck tracker
        RegisterHit(Other, SynthHit);

        // Process through full collision pipeline
        // (applies damage if CollisionDamage > 0, AND applies bounce with
        // correct ImpactPoint so both angular and linear impulse are correct)
        ProcessHit(SynthHit, Other);

        break; // one confirmed contact per rotation tick is enough
    }
}

// -----------------------------------------------------------------------------
//  RegisterHit — anti-stuck tracker
// -----------------------------------------------------------------------------

void USubmarineCollisionComponent::RegisterHit(AActor* OtherActor, const FHitResult& Hit)
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!OtherActor || !IsValid(OtherActor) || !Stats) return;
    if (!Stats->bEnableAntiStuckPhysics) return;
    if (OtherActor->IsA<ATorpedoPawn>()) return;
    if (OtherActor->ActorHasTag(UReplayHelpers::Tag_ReplayGhost)) return;

    const float Now = GetWorld()->GetTimeSeconds();

    FContactState& State = ContactStates.FindOrAdd(OtherActor);

    if (State.FirstHitTime == 0.f)
        State.FirstHitTime = Now;

    State.LastHitTime = Now;

    if (!Hit.ImpactNormal.IsNearlyZero())
    {
        State.NormalSum += Hit.ImpactNormal;
        State.NormalCount++;
    }

    // Run the anti-stuck check immediately after every registered hit
    TickAntiStuck();
}

// -----------------------------------------------------------------------------
//  TickAntiStuck — moved from SubmarinePawn, called by RegisterHit
// -----------------------------------------------------------------------------

void USubmarineCollisionComponent::TickAntiStuck()
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats || ContactStates.Num() == 0) return;

    ASubmarinePawn* OwnerPawn = Cast<ASubmarinePawn>(GetOwner());
    if (!OwnerPawn) return;

    const float Now = GetWorld()->GetTimeSeconds();
    TArray<AActor*> ToRemove;

    for (auto& Pair : ContactStates)
    {
        AActor* Other = Pair.Key;
        FContactState& State = Pair.Value;

        if (!IsValid(Other)) { ToRemove.Add(Other); continue; }

        const float TimeSinceLast = Now - State.LastHitTime;
        if (TimeSinceLast > Stats->AntiStuckThresholdOut)
        {
            ToRemove.Add(Other);
            continue;
        }

        const float ContactDuration = State.LastHitTime - State.FirstHitTime;
        if (ContactDuration < Stats->AntiStuckThresholdIn) continue;

        const float TimeSinceExpulsion = Now - State.LastExpulsionTime;
        if (TimeSinceExpulsion < Stats->AntiStuckCooldown) continue;

        // Compute escape direction
        FVector EscapeDir = FVector::ZeroVector;
        if (State.NormalCount > 0)
            EscapeDir = (State.NormalSum / State.NormalCount).GetSafeNormal();

        if (EscapeDir.IsNearlyZero())
        {
            EscapeDir = OwnerPawn->GetActorLocation() - Other->GetActorLocation();
            if (EscapeDir.IsNearlyZero()) EscapeDir = FVector::UpVector;
            EscapeDir.Normalize();
        }

        if (Stats->bLogAntiStuck) {
            UE_LOG(LogTemp, Warning,
                TEXT("[AntiStuck] FIRE on %s! Contact=%.2fs Force=%.0f"),
                *Other->GetName(), ContactDuration, Stats->AntiStuckForce);
        }

        // Brute-force nudge + escape velocity
        OwnerPawn->AddActorWorldOffset(EscapeDir * 5.f, false);

        const FVector WorldImpulse = EscapeDir * Stats->AntiStuckForce;
        const float   ForwardComp = FVector::DotProduct(WorldImpulse, OwnerPawn->GetActorForwardVector());
        OwnerPawn->SetExternalLinearVelocity(OwnerPawn->GetExternalLinearVelocity() + ForwardComp);
        OwnerPawn->SetExternalVerticalVelocity(OwnerPawn->GetExternalVerticalVelocity() + WorldImpulse.Z);

        if (USubmarinePhysicsComponent* OtherPhys =
            Other->FindComponentByClass<USubmarinePhysicsComponent>())
        {
            OtherPhys->AddImpulse(-WorldImpulse);
        }

        State.LastExpulsionTime = Now;
        State.NormalSum = FVector::ZeroVector;
        State.NormalCount = 0;
    }

    for (AActor* Dead : ToRemove)
        ContactStates.Remove(Dead);
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
        UReplayHelpers::SpawnGameplayVFXAtLocation(
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