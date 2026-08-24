// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarinePhysicsComponent.h"
#include "SubmarineCharacteristics.h"
#include "OceanSubsystem.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

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

    // Randomize agitation phase offsets so submarines in the same area
    // don't heave/roll in perfect synchrony -- each gets its own phase.
    HeavePhaseOffset = FMath::FRandRange(0.f, 2.f * PI);
    RollPhaseOffset = FMath::FRandRange(0.f, 2.f * PI);
    PitchPhaseOffset = FMath::FRandRange(0.f, 2.f * PI);
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

    // -------------------------------------------------------------------------
    //  Water surface query + depth state
    // -------------------------------------------------------------------------
    const float WaterZ = GetWaterSurfaceZ();
    CurrentDepth = WaterZ - Owner->GetActorLocation().Z;
    bAboveSurface = (CurrentDepth < 0.f);

    // -------------------------------------------------------------------------
    //  Near-surface alpha
    // -------------------------------------------------------------------------
    UpdateNearSurfaceState(DeltaTime, Stats);

    // -------------------------------------------------------------------------
    //  Core forces
    // -------------------------------------------------------------------------
    const FVector GravForce = ComputeGravityForce();
    const FVector BuoyForce = ComputeBuoyancyForce();
    const FVector DragForce = ComputeDragForce();
    const FVector DepthForce = Stats->bEnableDepthPhysics ? ComputeDepthPressureForce() : FVector::ZeroVector;
    const FVector ThrustForce = ComputeThrustForce(Owner->GetActorForwardVector());
    const FVector AgitationForce = ComputeAgitationForce(Stats);

    // -------------------------------------------------------------------------
    //  Angular perturbation smoothing
    // -------------------------------------------------------------------------
    UpdateAngularPerturbation(DeltaTime, Stats);

    // -------------------------------------------------------------------------
    //  Integrate
    // -------------------------------------------------------------------------
    FVector TotalForce = GravForce + BuoyForce + DragForce + DepthForce + ThrustForce + AgitationForce;
    TotalForce += AccumulatedForces;
    AccumulatedForces = FVector::ZeroVector;

    LogPhysicsState(DeltaTime, Stats, WaterZ, GravForce, BuoyForce, DragForce, DepthForce, ThrustForce, TotalForce);

    PhysicsVelocity += TotalForce * DeltaTime;
    PhysicsVelocity += AccumulatedImpulse;
    AccumulatedImpulse = FVector::ZeroVector;

    NetVerticalAcceleration = TotalForce.Z;

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

    FVector BaseDrag = Stats->bUseComplexDrag
        ? ComputeDragForceTensor()
        : ComputeDragForceSimple();

    // Regional drag modifier (DA-gated).
    // Murky/storm regions can increase drag slightly.
    // Scales with the submarine's position via OceanSubsystem blending.
    if (Stats->bEnableRegionalDrag)
    {
        AActor* Owner = GetOwner();
        if (Owner)
        {
            UGameInstance* GI = Owner->GetGameInstance();
            UOceanSubsystem* OceanSys = GI ? GI->GetSubsystem<UOceanSubsystem>() : nullptr;
            if (OceanSys)
            {
                const float RegionalMult = OceanSys->GetRegionalDragMultiplierAt(
                    Owner->GetActorLocation());
                BaseDrag *= RegionalMult;
            }
        }
    }

    return BaseDrag;
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

    // Surface speed bonus
    float EffectiveTargetSpeed = TargetLinearSpeed;
    if (Stats->bEnableSurfaceSpeedBonus && NearSurfaceAlpha > 0.f)
    {
        const float Multiplier = FMath::Lerp(1.f, Stats->SurfaceSpeedBonus, NearSurfaceAlpha);
        EffectiveTargetSpeed *= Multiplier;
    }

    // Only drive linear (forward/backward) speed via thrust
    const float CurrentLinear = FVector::DotProduct(PhysicsVelocity, OwnerForward);
    const float LinearError = EffectiveTargetSpeed - CurrentLinear;

    const float LinearGain = (LinearError >= 0.f)
        ? Stats->LinearAcceleration
        : Stats->LinearDeceleration;

    const FVector LinearThrust =
        OwnerForward * FMath::Clamp(LinearError * LinearGain,
            -Stats->MaxThrustForce, Stats->MaxThrustForce);

    return LinearThrust;
}

// ---------------------------------------------------------------------------
//  ComputeAgitationForce
//  Returns the heave (vertical bobbing) force from ocean wave agitation.
//  Only non-zero when near the surface. Sets the angular perturbation target
//  as a side effect (stored in AccumulatedAngularPerturbation).
// ---------------------------------------------------------------------------
FVector USubmarinePhysicsComponent::ComputeAgitationForce(
    const USubmarineCharacteristics* Stats)
{
    if (!bIsNearSurface || NearSurfaceAlpha <= 0.f) return FVector::ZeroVector;

    // Query blended agitation from ocean subsystem.
    float AgitationIntensity = 0.f;
    AActor* Owner = GetOwner();
    if (Owner)
    {
        UGameInstance* GI = Owner->GetGameInstance();
        if (GI)
        {
            UOceanSubsystem* OceanSys = GI->GetSubsystem<UOceanSubsystem>();
            if (OceanSys)
                AgitationIntensity = OceanSys->GetAgitationAtPosition(Owner->GetActorLocation());
        }
    }

    const float AgitationScale = AgitationIntensity
        * NearSurfaceAlpha
        * Stats->AgitationPhysicsScale;

    if (AgitationScale <= SMALL_NUMBER) return FVector::ZeroVector;

    const float T = GetWorld()->GetTimeSeconds();

    // Heave: two-frequency sine sum for organic, non-mechanical feel.
    const float HeavePrimary = FMath::Sin(T * Stats->AgitationHeaveFrequency * 2.f * PI
        + HeavePhaseOffset);
    const float HeaveSecondary = FMath::Sin(T * Stats->AgitationHeaveFrequency * 2.f * PI * 1.73f
        + HeavePhaseOffset + 1.1f) * 0.3f;
    const float HeaveForce = (HeavePrimary + HeaveSecondary)
        * AgitationScale * Stats->AgitationHeaveAmplitude;

    // Angular perturbation target (roll + pitch).
    // Stored for UpdateAngularPerturbation to smooth in the next step.
    const float RollTarget = FMath::Sin(T * Stats->AgitationRollFrequency * 2.f * PI
        + RollPhaseOffset)
        * AgitationScale * Stats->AgitationRollAmplitude;
    const float PitchTarget = FMath::Sin(T * Stats->AgitationPitchFrequency * 2.f * PI
        + PitchPhaseOffset)
        * AgitationScale * Stats->AgitationPitchAmplitude;

    AccumulatedAngularPerturbation = FRotator(PitchTarget, 0.f, RollTarget);

    return FVector(0.f, 0.f, HeaveForce);
}

// ---------------------------------------------------------------------------
//  UpdateNearSurfaceState
//  Computes bIsNearSurface and NearSurfaceAlpha from current depth.
//  Uses blended regional water height (already in CurrentDepth) -- NOT
//  instantaneous wave peaks -- so the state does not flicker with troughs.
// ---------------------------------------------------------------------------
void USubmarinePhysicsComponent::UpdateNearSurfaceState(float DeltaTime,
    const USubmarineCharacteristics* Stats)
{
    const float TargetAlpha = (!bAboveSurface && CurrentDepth <= Stats->NearSurfaceThreshold)
        ? FMath::Clamp(1.f - CurrentDepth / FMath::Max(Stats->NearSurfaceThreshold, 1.f), 0.f, 1.f)
        : 0.f;

    NearSurfaceAlpha = FMath::FInterpTo(NearSurfaceAlpha, TargetAlpha,
        DeltaTime, Stats->NearSurfaceTransitionRate);
    bIsNearSurface = (NearSurfaceAlpha > 0.01f);
}



// ---------------------------------------------------------------------------
//  UpdateAngularPerturbation
//  Smooths the perturbation accumulator toward CurrentAngularPerturbation,
//  applies auto-recovery when submerged, and hard-clamps the result.
// ---------------------------------------------------------------------------
void USubmarinePhysicsComponent::UpdateAngularPerturbation(float DeltaTime,
    const USubmarineCharacteristics* Stats)
{
    // Smooth toward the target set this frame by ComputeAgitationForce.
    CurrentAngularPerturbation = FMath::RInterpTo(
        CurrentAngularPerturbation,
        AccumulatedAngularPerturbation,
        DeltaTime, Stats->AngularPerturbationInterpRate);

    // Auto-recover to zero when fully submerged.
    if (!bIsNearSurface)
    {
        CurrentAngularPerturbation = FMath::RInterpTo(
            CurrentAngularPerturbation,
            FRotator::ZeroRotator,
            DeltaTime, Stats->AngularRecoveryRate);
    }

    // Hard clamp — designer-set maximum angles.
    CurrentAngularPerturbation.Roll = FMath::Clamp(CurrentAngularPerturbation.Roll,
        -Stats->MaxRollPerturbation, Stats->MaxRollPerturbation);
    CurrentAngularPerturbation.Pitch = FMath::Clamp(CurrentAngularPerturbation.Pitch,
        -Stats->MaxPitchPerturbation, Stats->MaxPitchPerturbation);

    // Reset accumulator -- repopulated next tick if near surface.
    AccumulatedAngularPerturbation = FRotator::ZeroRotator;
}

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------

// Redirected to UOceanSubsystem for authoritative water height.
// The subsystem query is position-aware and region-blended.
// Falls back to the DataAsset WaterSurfaceZ if the subsystem is unavailable
// (e.g. during early init or if GameInstance is not configured).
float USubmarinePhysicsComponent::GetWaterSurfaceZ() const
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        const USubmarineCharacteristics* Stats = GetStats();
        return Stats ? Stats->WaterSurfaceZ : 0.f;
    }

    UWorld* World = Owner->GetWorld();
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    if (!GI)
    {
        const USubmarineCharacteristics* Stats = GetStats();
        return Stats ? Stats->WaterSurfaceZ : 0.f;
    }

    UOceanSubsystem* OceanSys = GI->GetSubsystem<UOceanSubsystem>();
    if (!OceanSys)
    {
        const USubmarineCharacteristics* Stats = GetStats();
        return Stats ? Stats->WaterSurfaceZ : 0.f;
    }

    const FVector ActorPos = Owner->GetActorLocation();

    // Canonical surface query: regional base + primary (octave 0) Gerstner.
    // This uses the SAME formula and constants the material uses for Oct1,
    // so physics and visuals ride the identical primary wave.
    const float Time = World->GetTimeSeconds();
    const FOceanSurfaceSample Surf = OceanSys->QuerySurface(
        FVector2D(ActorPos.X, ActorPos.Y), Time);

    // QuerySurface returns the FULL-amplitude surface. We keep the existing
    // NearSurfaceAlpha gameplay behavior unchanged: deep submarines feel a
    // reduced wave. The primary wave offset is scaled by NearSurfaceAlpha
    // here (force/geometry coupling preserved as before).
    if (!bIsNearSurface)
    {
        // Deep: no wave contribution, just the regional base.
        return Surf.RegionalBaseHeight;
    }

    return Surf.RegionalBaseHeight + Surf.PrimaryWaveOffset * NearSurfaceAlpha;
}

const USubmarineCharacteristics* USubmarinePhysicsComponent::GetStats() const
{
    if (Characteristics) return Characteristics;
    return GetDefault<USubmarineCharacteristics>();
}

//  GetDisplayDepth
float USubmarinePhysicsComponent::GetDisplayDepth() const
{
    const USubmarineCharacteristics* Stats = GetStats();
    const float UIOffset = Stats ? Stats->NearSurfaceDepthUIOffset : 0.f;
    return FMath::Max(0.f, CurrentDepth + UIOffset);
}


// ---------------------------------------------------------------------------
//  LogPhysicsState
//  Throttled debug log. Extracted from TickComponent to keep tick clean.
// ---------------------------------------------------------------------------
void USubmarinePhysicsComponent::LogPhysicsState(float DeltaTime,
    const USubmarineCharacteristics* Stats,
    float WaterZ,
    const FVector& GravForce, const FVector& BuoyForce,
    const FVector& DragForce, const FVector& DepthForce,
    const FVector& ThrustForce, const FVector& TotalForce)
{
    PhysicsLogTimer += DeltaTime;
    if (PhysicsLogTimer < Stats->PhysicsLogFrequency || !Stats->bEnablePhysicsLogs)
        return;

    PhysicsLogTimer = 0.f;
    AActor* Owner = GetOwner();

    UE_LOG(LogTemp, Warning, TEXT("========= [PhysicsComponent] ========="));
    UE_LOG(LogTemp, Warning, TEXT("  DA ptr valid   : %s"),
        Characteristics ? TEXT("YES") : TEXT("NO - using CDO defaults!"));
    UE_LOG(LogTemp, Warning, TEXT("  WaterSurfaceZ  : %.1f  (from DA: %.1f)"),
        WaterZ, Stats->WaterSurfaceZ);
    UE_LOG(LogTemp, Warning, TEXT("  SubmarineZ     : %.1f"),
        Owner ? Owner->GetActorLocation().Z : 0.f);
    UE_LOG(LogTemp, Warning, TEXT("  CurrentDepth   : %.1f  (%s)"),
        CurrentDepth, bAboveSurface ? TEXT("ABOVE water") : TEXT("SUBMERGED"));
    UE_LOG(LogTemp, Warning, TEXT("  NearSurface    : %s  Alpha=%.2f"),
        bIsNearSurface ? TEXT("YES") : TEXT("NO"), NearSurfaceAlpha);
    UE_LOG(LogTemp, Warning, TEXT("  AngPerturb     : Roll=%.2f  Pitch=%.2f"),
        CurrentAngularPerturbation.Roll, CurrentAngularPerturbation.Pitch);
    UE_LOG(LogTemp, Warning, TEXT("  --- Forces (Z axis) ---"));
    UE_LOG(LogTemp, Warning, TEXT("  Gravity        : %.2f"), GravForce.Z);
    UE_LOG(LogTemp, Warning, TEXT("  Buoyancy       : %.2f"), BuoyForce.Z);
    UE_LOG(LogTemp, Warning, TEXT("  Drag Z         : %.2f"), DragForce.Z);
    UE_LOG(LogTemp, Warning, TEXT("  DepthPressure  : %.2f"), DepthForce.Z);
    UE_LOG(LogTemp, Warning, TEXT("  ThrustZ        : %.2f  (TargetLinear=%.1f)"),
        ThrustForce.Z, TargetLinearSpeed);
    UE_LOG(LogTemp, Warning, TEXT("  TotalForce Z   : %.2f"), TotalForce.Z);
    UE_LOG(LogTemp, Warning, TEXT("  PhysVelocity   : (%.1f, %.1f, %.1f)"),
        PhysicsVelocity.X, PhysicsVelocity.Y, PhysicsVelocity.Z);
    UE_LOG(LogTemp, Warning, TEXT("======================================"));
}