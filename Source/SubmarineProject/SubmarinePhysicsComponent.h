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

    // -- Force accumulation API---------------------------------------------

    /** Add a world-space force (cm/s²) for this tick only (cleared each tick) */
    void AddForce(const FVector& Force);

    /** Add a world-space impulse (cm/s) applied instantly */
    void AddImpulse(const FVector& Impulse);

    // -- Characteristics reference -----------------------------------------
    // Set by the owning pawn in BeginPlay so this works for both player
    // and non-player submarines.

    UPROPERTY()
    TObjectPtr<USubmarineCharacteristics> Characteristics;

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

private:
    // -- Internal force state ----------------------------------------------

    FVector AccumulatedForces = FVector::ZeroVector;
    FVector AccumulatedImpulse = FVector::ZeroVector;

    // -- Physics sub-systems -----------------------------------------------

    FVector ComputeGravityForce() const;
    FVector ComputeBuoyancyForce() const;
    FVector ComputeDragForce() const;
    FVector ComputeDragForceSimple() const;
    FVector ComputeDragForceTensor() const;
    FVector ComputeDepthPressureForce() const;
    FVector ComputeThrustForce(const FVector& OwnerForward, const FVector& OwnerUp) const;

    float GetWaterSurfaceZ() const;

    const USubmarineCharacteristics* GetStats() const;
};