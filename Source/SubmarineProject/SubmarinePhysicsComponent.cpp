// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarinePhysicsComponent.h"
#include "SubmarineCharacteristics.h"
#include "GameFramework/Actor.h"

USubmarinePhysicsComponent::USubmarinePhysicsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    // Physics ticks before the pawn so velocity is ready when pawn moves
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USubmarinePhysicsComponent::BeginPlay()
{
    Super::BeginPlay();
    PhysicsVelocity = FVector::ZeroVector;
}

// -----------------------------------------------------------------------------
//  Main tick — integrate all forces into velocity
// -----------------------------------------------------------------------------
void USubmarinePhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    AActor* Owner = GetOwner();
    if (!Owner) return;

    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return;

    // Update depth
    const float WaterZ = GetWaterSurfaceZ();
    CurrentDepth = WaterZ - Owner->GetActorLocation().Z;
    bAboveSurface = (CurrentDepth < 0.f);

    // Compute each force separately for logging
    const FVector GravForce = ComputeGravityForce();
    const FVector BuoyForce = ComputeBuoyancyForce();
    const FVector DragForce = ComputeDragForce();
    const FVector DepthForce = Stats->bEnableDepthPhysics ? ComputeDepthPressureForce() : FVector::ZeroVector;
    const FVector ThrustForce = ComputeThrustForce(Owner->GetActorForwardVector());

    // Accumulate forces
    FVector TotalForce = GravForce + BuoyForce + DragForce + DepthForce + ThrustForce;
    TotalForce += AccumulatedForces;
    AccumulatedForces = FVector::ZeroVector;

    // Debug log every 2 seconds
    static float LogTimer = 0.f;
    LogTimer += DeltaTime;
    if (LogTimer >= 2.f)
    {
        LogTimer = 0.f;
        UE_LOG(LogTemp, Warning, TEXT("========= [PhysicsComponent] ========="));
        UE_LOG(LogTemp, Warning, TEXT("  DA ptr valid   : %s"),
            Characteristics ? TEXT("YES") : TEXT("NO - using CDO defaults!"));
        UE_LOG(LogTemp, Warning, TEXT("  WaterSurfaceZ  : %.1f  (from DA: %.1f)"),
            WaterZ, Stats->WaterSurfaceZ);
        UE_LOG(LogTemp, Warning, TEXT("  SubmarineZ     : %.1f"), Owner->GetActorLocation().Z);
        UE_LOG(LogTemp, Warning, TEXT("  CurrentDepth   : %.1f  (%s)"),
            CurrentDepth, bAboveSurface ? TEXT("ABOVE water") : TEXT("SUBMERGED"));
        UE_LOG(LogTemp, Warning, TEXT("  --- Forces (Z axis) ---"));
        UE_LOG(LogTemp, Warning, TEXT("  Gravity        : %.2f  (DA GravityAccel=%.1f)"),
            GravForce.Z, Stats->GravityAcceleration);
        UE_LOG(LogTemp, Warning, TEXT("  Buoyancy       : %.2f  (DA BuoyancyRatio=%.2f, SubmersionFactor=%.2f)"),
            BuoyForce.Z, Stats->BuoyancyRatio,
            bAboveSurface ? 0.f : FMath::Clamp(CurrentDepth / FMath::Max(Stats->SurfaceTransitionDepth, 1.f), 0.f, 1.f));
        UE_LOG(LogTemp, Warning, TEXT("  Drag Z         : %.2f  (ComplexDrag=%s)"),
            DragForce.Z, Stats->bUseComplexDrag ? TEXT("ON") : TEXT("OFF"));
        UE_LOG(LogTemp, Warning, TEXT("  DepthPressure  : %.2f  (Enabled=%s)"),
            DepthForce.Z, Stats->bEnableDepthPhysics ? TEXT("ON") : TEXT("OFF"));
        UE_LOG(LogTemp, Warning, TEXT("  ThrustZ        : %.2f  (TargetLinear=%.1f)"),
            ThrustForce.Z, TargetLinearSpeed);
        UE_LOG(LogTemp, Warning, TEXT("  --- Result ---"));
        UE_LOG(LogTemp, Warning, TEXT("  TotalForce Z   : %.2f"), TotalForce.Z);
        UE_LOG(LogTemp, Warning, TEXT("  PhysVelocity Z : %.2f"), PhysicsVelocity.Z);
        UE_LOG(LogTemp, Warning, TEXT("  PhysVelocity   : (%.1f, %.1f, %.1f)"),
            PhysicsVelocity.X, PhysicsVelocity.Y, PhysicsVelocity.Z);
        UE_LOG(LogTemp, Warning, TEXT("======================================"));
    }

    // -- Integrate forces -> velocity ---------------------------------------
    PhysicsVelocity += TotalForce * DeltaTime;

    // Apply impulses (instant velocity change)
    PhysicsVelocity += AccumulatedImpulse;
    AccumulatedImpulse = FVector::ZeroVector;

    // Expose net vertical acceleration for SubmarinePawn blending
    NetVerticalAcceleration = TotalForce.Z;

    // -- Clamp to prevent runaway (safety) ---------------------------------
    const float MaxSpeed = Stats->PhysicsMaxSpeed;
    if (PhysicsVelocity.SizeSquared() > MaxSpeed * MaxSpeed)
        PhysicsVelocity = PhysicsVelocity.GetSafeNormal() * MaxSpeed;
}

// -----------------------------------------------------------------------------
//  Force accumulation
// -----------------------------------------------------------------------------
void USubmarinePhysicsComponent::AddForce(const FVector& Force)
{
    AccumulatedForces += Force;
}

void USubmarinePhysicsComponent::AddImpulse(const FVector& Impulse)
{
    AccumulatedImpulse += Impulse;
}

// -----------------------------------------------------------------------------
//  Gravity
// -----------------------------------------------------------------------------
FVector USubmarinePhysicsComponent::ComputeGravityForce() const
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return FVector::ZeroVector;

    // Only apply gravity below surface (above surface handled separately)
    // Gravity is always downward world Z
    return FVector(0.f, 0.f, -Stats->GravityAcceleration);
}

// -----------------------------------------------------------------------------
//  Buoyancy
//
//  F_buoyancy = BuoyancyRatio * GravityAcceleration * SubmersionFactor
//
//  SubmersionFactor: 0 = fully above surface, 1 = fully submerged
//  Near the surface, buoyancy is partial (linear blend over SurfaceTransitionDepth)
//  Depth pressure reduces buoyancy efficiency at great depth
// -----------------------------------------------------------------------------
FVector USubmarinePhysicsComponent::ComputeBuoyancyForce() const
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return FVector::ZeroVector;

    if (bAboveSurface) return FVector::ZeroVector;

    // Submersion factor: blends from 0->1 over SurfaceTransitionDepth
    const float SubmersionFactor = FMath::Clamp(
        CurrentDepth / FMath::Max(Stats->SurfaceTransitionDepth, 1.f), 0.f, 1.f);

    // Depth pressure attenuation
    float PressureAttenuation = 1.f;
    if (Stats->bEnableDepthPhysics && Stats->BuoyancyDepthNerfCoefficient > 0.f)
    {
        // Pressure increases with depth, reducing buoyancy efficiency
        PressureAttenuation = FMath::Max(0.f,
            1.f - (CurrentDepth * Stats->BuoyancyDepthNerfCoefficient));
    }

    const float BuoyancyAccel =
        Stats->BuoyancyRatio * Stats->GravityAcceleration *
        SubmersionFactor * PressureAttenuation;

    return FVector(0.f, 0.f, BuoyancyAccel);
}

// -----------------------------------------------------------------------------
//  Drag — dispatcher
// -----------------------------------------------------------------------------
FVector USubmarinePhysicsComponent::ComputeDragForce() const
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return FVector::ZeroVector;

    return Stats->bUseComplexDrag
        ? ComputeDragForceTensor()
        : ComputeDragForceSimple();
}

// -----------------------------------------------------------------------------
//  Simple drag — scalar, opposes velocity, proportional to speed²
// -----------------------------------------------------------------------------
FVector USubmarinePhysicsComponent::ComputeDragForceSimple() const
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats || PhysicsVelocity.IsNearlyZero()) return FVector::ZeroVector;

    const float Speed = PhysicsVelocity.Size();
    const float DragForce = Stats->SimpleDragCoefficient * Speed * Speed;
    //return -PhysicsVelocity.GetSafeNormal() * DragForce;
    return -PhysicsVelocity * Stats->SimpleDragCoefficient;
}

// -----------------------------------------------------------------------------
//  Complex drag — 6DOF tensor
//
//  Drag is computed per local axis (forward, right, up) independently.
//  Each axis has its own coefficient (forward = low, lateral/vertical = high).
//  Formula: F_drag_axis = -Cd_axis * v_axis * |v_axis|
// -----------------------------------------------------------------------------
FVector USubmarinePhysicsComponent::ComputeDragForceTensor() const
{
    const USubmarineCharacteristics* Stats = GetStats();
    AActor* Owner = GetOwner();
    if (!Stats || !Owner || PhysicsVelocity.IsNearlyZero()) return FVector::ZeroVector;

    // Project world velocity onto submarine local axes
    const FVector Forward = Owner->GetActorForwardVector();
    const FVector Right = Owner->GetActorRightVector();
    const FVector Up = Owner->GetActorUpVector();

    const float Vf = FVector::DotProduct(PhysicsVelocity, Forward); // forward component
    const float Vr = FVector::DotProduct(PhysicsVelocity, Right);   // lateral component
    const float Vu = FVector::DotProduct(PhysicsVelocity, Up);      // vertical component

    // Per-axis drag (velocity-squared, opposing direction)
    const FVector DragForward = -Forward * Stats->DragTensor.X * Vf * FMath::Abs(Vf);
    const FVector DragRight = -Right * Stats->DragTensor.Y * Vr * FMath::Abs(Vr);
    const FVector DragUp = -Up * Stats->DragTensor.Z * Vu * FMath::Abs(Vu);

    return DragForward + DragRight + DragUp;
}

// -----------------------------------------------------------------------------
//  Depth pressure
//
//  At great depth, hull compression slightly opposes upward movement
//  (net downward force proportional to depth beyond PressureDepthThreshold)
// -----------------------------------------------------------------------------
FVector USubmarinePhysicsComponent::ComputeDepthPressureForce() const
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats || !Stats->bEnableDepthPhysics) return FVector::ZeroVector;
    if (CurrentDepth <= Stats->PressureDepthThreshold) return FVector::ZeroVector;

    const float ExcessDepth = CurrentDepth - Stats->PressureDepthThreshold;
    const float PressureForce = ExcessDepth * Stats->DepthPressureCoefficient
        * Stats->DepthPhysicsInfluence;

    return FVector(0.f, 0.f, -PressureForce);
}

// -----------------------------------------------------------------------------
//  Thrust — PD controller
//
//  Rather than setting velocity directly, we compute the force needed to
//  reach TargetLinearSpeed and TargetVerticalSpeed. This preserves inertia
//  and makes external perturbations (drag, buoyancy) interact naturally.
//
//  F_thrust = Kp * (target - current) — proportional term only
//  (derivative term is implicit via drag opposing overshoot)
// -----------------------------------------------------------------------------
FVector USubmarinePhysicsComponent::ComputeThrustForce(const FVector& OwnerForward) const
{
    const USubmarineCharacteristics* Stats = GetStats();
    if (!Stats) return FVector::ZeroVector;

    // Only drive linear (forward/backward) speed via thrust
    const float CurrentLinear = FVector::DotProduct(PhysicsVelocity, OwnerForward);
    const float LinearError = TargetLinearSpeed - CurrentLinear;

    const float LinearGain = (LinearError >= 0.f)
        ? Stats->LinearAcceleration
        : Stats->LinearDeceleration;

    const FVector LinearThrust =
        OwnerForward * FMath::Clamp(LinearError * LinearGain,
            -Stats->MaxThrustForce, Stats->MaxThrustForce);

    return LinearThrust;
}

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------
float USubmarinePhysicsComponent::GetWaterSurfaceZ() const
{
    const USubmarineCharacteristics* Stats = GetStats();
    return Stats ? Stats->WaterSurfaceZ : 0.f;
}

const USubmarineCharacteristics* USubmarinePhysicsComponent::GetStats() const
{
    if (Characteristics) return Characteristics;
    return GetDefault<USubmarineCharacteristics>();
}