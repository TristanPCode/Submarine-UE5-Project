#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SubmarinePhysicsComponent.generated.h"

class USubmarineCharacteristics;

/**
 * USubmarinePhysicsComponent
 *
 * Handles all physics forces acting on a submarine:
 *   - Gravity
 *   - Buoyancy (with depth-based pressure attenuation)
 *   - Drag (simple scalar OR full 6DOF tensor, selectable via DA)
 *   - External impulse forces (from collisions)
 *
 * Works on both player-possessed and unpossessed submarines.
 * The owning actor is responsible for applying the resulting
 * velocity delta via AddActorWorldOffset each tick.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API USubmarinePhysicsComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USubmarinePhysicsComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -- Characteristics reference -----------------------------------------
    // Set by the owning pawn in BeginPlay so this works for both player
    // and non-player submarines.

    UPROPERTY()
    TObjectPtr<USubmarineCharacteristics> Characteristics;

    // -- Force accumulation API---------------------------------------------

    /** Add a world-space force (cm/s²) for this tick only (cleared each tick) */
    void AddForce(const FVector& Force);

    /** Add a world-space impulse (cm/s) applied instantly */
    void AddImpulse(const FVector& Impulse);

    // -- Thrust input (set by SubmarinePawn each tick) ---------------------

    /**
     * Target linear speed requested by player input (cm/s).
     * Physics component applies a PD force to reach this.
     */
    float TargetLinearSpeed = 0.f;

    /**
     * Target vertical speed requested by player input (cm/s).
     * Physics component applies a PD force to reach this.
     */
    float TargetVerticalSpeed = 0.f;

    // -- Current state (read by SubmarinePawn) -----------------------------

    /** Current physics velocity (world space, cm/s) — excludes player input thrust */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Physics")
    FVector PhysicsVelocity = FVector::ZeroVector;

    /**
     * Net vertical force from buoyancy + gravity this tick (cm/s²).
     * Exposed so SubmarinePawn can blend it with input-driven vertical speed.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Physics")
    float NetVerticalAcceleration = 0.f;

    /**
     * True when the submarine is above the water surface Z.
     * SubmarinePawn uses this to suppress vertical boost when surfaced.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Physics")
    bool bAboveSurface = false;

    /**
     * Depth below water surface in cm (negative = above surface).
     * Used by depth pressure system.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Physics")
    float CurrentDepth = 0.f;

    // -----------------------------------------------------------------------
    //  Near-surface state
    // -----------------------------------------------------------------------

    /**
     * True when the submarine is within NearSurfaceThreshold of the average
     * regional water height. Uses blended regional height, not wave peaks,
     * so this state does not flicker with wave troughs.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics|Surface")
    bool bIsNearSurface = false;

    /**
     * Smooth blend factor: 0 = fully deep, 1 = at the surface.
     * All agitation, speed bonus, and perturbation effects scale by this.
     * Interpolates at NearSurfaceTransitionRate from SubmarineCharacteristics.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics|Surface")
    float NearSurfaceAlpha = 0.f;

    // -----------------------------------------------------------------------
    //  Angular perturbation (wave roll/pitch)
    //  Written by TickComponent. Read by SubmarinePawn::TickFinalMovement
    //  and applied additively to the submarine's rotation.
    // -----------------------------------------------------------------------

    /**
     * Current smoothed angular perturbation from wave agitation (degrees).
     * Roll and Pitch are populated. Yaw is always 0 (waves don't yaw submarines).
     * SubmarinePawn reads this and adds it to its TargetRotation each tick.
     * Hard-clamped by MaxRollPerturbation / MaxPitchPerturbation in DA.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics|Surface")
    FRotator CurrentAngularPerturbation = FRotator::ZeroRotator;

    // -----------------------------------------------------------------------
    //  Depth accessor for HUD (applies NearSurfaceDepthUIOffset)
    // -----------------------------------------------------------------------

    /**
     * Returns the depth value the Depthometer should display.
     * Applies NearSurfaceDepthUIOffset from the DA for aesthetic tuning.
     * Always >= 0 (clamped so above-surface never shows negative depth).
     */
    UFUNCTION(BlueprintPure, Category = "Physics|Surface")
    float GetDisplayDepth() const;

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    /** Returns the authoritative water surface Z at the submarine's position. */
    float GetWaterSurfaceZ() const;

    /** Safe stats accessor -- returns CDO if Characteristics is null. */
    const USubmarineCharacteristics* GetStats() const;

private:

    // -----------------------------------------------------------------------
    //  Force accumulators (cleared each tick after integration)
    // -----------------------------------------------------------------------

    FVector AccumulatedForces = FVector::ZeroVector;
    FVector AccumulatedImpulse = FVector::ZeroVector;

    // -----------------------------------------------------------------------
    //  Angular perturbation internals
    // -----------------------------------------------------------------------

    // Target perturbation computed this tick from agitation sine waves.
    // Smoothed toward CurrentAngularPerturbation over multiple frames.
    FRotator AccumulatedAngularPerturbation = FRotator::ZeroRotator;

    // Per-instance random phase offsets so submarines in proximity don't
    // heave/roll in perfect synchrony. Set once in BeginPlay via FRandRange.
    float HeavePhaseOffset = 0.f;
    float RollPhaseOffset = 0.f;
    float PitchPhaseOffset = 0.f;

    // -----------------------------------------------------------------------
    //  Debug
    // -----------------------------------------------------------------------

    float PhysicsLogTimer = 0.f;

    // -----------------------------------------------------------------------
    //  Force computation (called from TickComponent)
    // -----------------------------------------------------------------------

    FVector ComputeGravityForce()  const;
    FVector ComputeBuoyancyForce() const;
    FVector ComputeDragForce()     const;
    FVector ComputeDragForceSimple()  const;
    FVector ComputeDragForceTensor()  const;
    FVector ComputeDepthPressureForce() const;
    FVector ComputeThrustForce(const FVector& OwnerForward) const;
    FVector ComputeAgitationForce(const USubmarineCharacteristics* Stats);

    // -----------------------------------------------------------------------
    //  Helpers (extracted from TickComponent for readability)
    // -----------------------------------------------------------------------

    // Updates bIsNearSurface and NearSurfaceAlpha from current depth.
    void UpdateNearSurfaceState(float DeltaTime,
        const USubmarineCharacteristics* Stats);

    // Smooths CurrentAngularPerturbation toward the target, applies recovery,
    // clamps, and resets the accumulator.
    void UpdateAngularPerturbation(float DeltaTime,
        const USubmarineCharacteristics* Stats);

    // Throttled physics debug log. Extracted to keep TickComponent readable.
    void LogPhysicsState(float DeltaTime, const USubmarineCharacteristics* Stats,
        float WaterZ,
        const FVector& GravForce, const FVector& BuoyForce,
        const FVector& DragForce, const FVector& DepthForce,
        const FVector& ThrustForce, const FVector& TotalForce);
};