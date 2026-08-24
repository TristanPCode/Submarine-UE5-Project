// Fill out your copyright notice in the Description page of Project Settings.

#include "TorpedoPhysicsComponent.h"
#include "TorpedoCharacteristics.h"
#include "OceanSubsystem.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

UTorpedoPhysicsComponent::UTorpedoPhysicsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UTorpedoPhysicsComponent::BeginPlay()
{
    Super::BeginPlay();
    // PhysicsVelocity is seeded by SetInitialVelocity() called from the spawner,
    // so we intentionally leave it zero here.
}

// -----------------------------------------------------------------------------
//  SetInitialVelocity — called by SubmarineTorpedoComponent right after spawn
// -----------------------------------------------------------------------------
void UTorpedoPhysicsComponent::SetInitialVelocity(const FVector& WorldVelocity)
{
    PhysicsVelocity = WorldVelocity;
}

// -----------------------------------------------------------------------------
//  Tick
// -----------------------------------------------------------------------------
void UTorpedoPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    AActor* Owner = GetOwner();
    if (!Owner) return;

    const UTorpedoCharacteristics* Stats = GetStats();
    if (!Stats) return;

    // Update depth
    const float WaterZ = GetWaterSurfaceZ();
    CurrentDepth = WaterZ - Owner->GetActorLocation().Z;
    bAboveSurface = (CurrentDepth < 0.f);

    // Near-surface state for wake VFX
    // Threshold is 500 UU -- torpedoes are fast and this keeps surface
    // effects tight to the actual surface zone. No smoothing needed here
    // since torpedo wake systems just need a binary gate, not a blend alpha.
    constexpr float TorpedoNearSurfaceThreshold = 500.f;
    bIsNearSurface = (!bAboveSurface && CurrentDepth <= TorpedoNearSurfaceThreshold);

    // Accumulate forces
    const FVector TotalForce =
        ComputeGravityForce() +
        ComputeBuoyancyForce() +
        ComputeDragForce() +
        ComputeThrustForce();

    // Integrate
    PhysicsVelocity += TotalForce * DeltaTime;

    // Safety clamp
    const float MaxSpd = Stats->PhysicsMaxSpeed;
    if (PhysicsVelocity.SizeSquared() > MaxSpd * MaxSpd)
        PhysicsVelocity = PhysicsVelocity.GetSafeNormal() * MaxSpd;
}

// -----------------------------------------------------------------------------
//  Gravity — always downward
// -----------------------------------------------------------------------------
FVector UTorpedoPhysicsComponent::ComputeGravityForce() const
{
    const UTorpedoCharacteristics* Stats = GetStats();
    if (!Stats) return FVector::ZeroVector;
    return FVector(0.f, 0.f, -Stats->GravityAcceleration);
}

// -----------------------------------------------------------------------------
//  Buoyancy — mirrors submarine logic (surface transition blend)
// -----------------------------------------------------------------------------
FVector UTorpedoPhysicsComponent::ComputeBuoyancyForce() const
{
    const UTorpedoCharacteristics* Stats = GetStats();
    if (!Stats || bAboveSurface) return FVector::ZeroVector;

    const float SubmersionFactor = FMath::Clamp(
        CurrentDepth / FMath::Max(Stats->SurfaceTransitionDepth, 1.f), 0.f, 1.f);

    const float BuoyancyAccel =
        Stats->BuoyancyRatio * Stats->GravityAcceleration * SubmersionFactor;

    return FVector(0.f, 0.f, BuoyancyAccel);
}

// -----------------------------------------------------------------------------
//  Drag — simple linear, opposes velocity
// -----------------------------------------------------------------------------
FVector UTorpedoPhysicsComponent::ComputeDragForce() const
{
    const UTorpedoCharacteristics* Stats = GetStats();
    if (!Stats || PhysicsVelocity.IsNearlyZero()) return FVector::ZeroVector;

    FVector BaseDrag = -PhysicsVelocity * Stats->DragCoefficient;

    // Regional drag modifier (Phase 6.3 — only if region DA enables torpedo drag).
    // Lightweight: torpedoes are fast and numerous, so we skip this if the region
    // doesn't explicitly affect torpedoes (bAffectTorpedoes flag on the DA).
    AActor* Owner = GetOwner();
    if (Owner)
    {
        UGameInstance* GI = Owner->GetGameInstance();
        UOceanSubsystem* OceanSys = GI ? GI->GetSubsystem<UOceanSubsystem>() : nullptr;
        if (OceanSys)
        {
            const float RegionalMult = OceanSys->GetRegionalDragMultiplierAt(
                Owner->GetActorLocation(), /*bTorpedoQuery=*/true);
            BaseDrag *= RegionalMult;
        }
    }

    return BaseDrag;
}

// -----------------------------------------------------------------------------
//  Thrust — PD controller driving forward speed toward MaxSpeed
// -----------------------------------------------------------------------------
FVector UTorpedoPhysicsComponent::ComputeThrustForce() const
{
    const UTorpedoCharacteristics* Stats = GetStats();
    AActor* Owner = GetOwner();
    if (!Stats || !Owner || Stats->TorpedoAcceleration <= 0.f)
        return FVector::ZeroVector;

    const FVector Forward = Owner->GetActorForwardVector();
    const float CurrentForwardSpeed = FVector::DotProduct(PhysicsVelocity, Forward);
    const float Error = Stats->MaxSpeed - CurrentForwardSpeed;

    if (Error <= 0.f) return FVector::ZeroVector; // already at or above max speed

    // Proportional thrust — clamp to acceleration cap
    const float ThrustMag = FMath::Min(Error * Stats->TorpedoAcceleration, Stats->TorpedoAcceleration);
    return Forward * ThrustMag;
}

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------
// 
// Redirected to UOceanSubsystem for authoritative water height.
// Torpedoes and submarines always query the same water authority.
// Falls back to the DataAsset WaterSurfaceZ if the subsystem is unavailable.
float UTorpedoPhysicsComponent::GetWaterSurfaceZ() const
{
    AActor* Owner = GetOwner();
    if (Owner)
    {
        UWorld* World = Owner->GetWorld();
        UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
        if (GI)
        {
            UOceanSubsystem* OceanSys = GI->GetSubsystem<UOceanSubsystem>();
            if (OceanSys)
            {
                return OceanSys->GetWaterHeightAtPosition(Owner->GetActorLocation());
            }
        }
    }

    // Fallback: Old behavior using the DataAsset scalar.
    const UTorpedoCharacteristics* Stats = GetStats();
    return Stats ? Stats->WaterSurfaceZ : 0.f;
}

const UTorpedoCharacteristics* UTorpedoPhysicsComponent::GetStats() const
{
    if (Characteristics) return Characteristics;
    return GetDefault<UTorpedoCharacteristics>();
}