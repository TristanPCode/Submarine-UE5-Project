#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TorpedoPhysicsComponent.generated.h"

class UTorpedoCharacteristics;

/**
 * UTorpedoPhysicsComponent
 *
 * Handles physics forces acting on a torpedo:
 *   - Gravity (downward)
 *   - Buoyancy (depth-based, matches submarine logic)
 *   - Drag (simple linear, opposes velocity)
 *   - Self-propulsion thrust toward MaxSpeed
 *
 * The owning ATorpedoPawn applies the resulting PhysicsVelocity
 * via AddActorWorldOffset each tick.
 *
 * Initial velocity is injected by the spawner (SubmarineTorpedoComponent)
 * via SetInitialVelocity() immediately after spawn.
 */
UCLASS(ClassGroup = (Torpedo), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UTorpedoPhysicsComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UTorpedoPhysicsComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -- Characteristics reference -----------------------------------------

    /** Set by ATorpedoPawn after spawn. Must be assigned before first tick. */
    UPROPERTY()
    TObjectPtr<UTorpedoCharacteristics> Characteristics;

    // -- State (read by ATorpedoPawn) --------------------------------------

    /** Current world-space velocity of the torpedo (cm/s) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Physics")
    FVector PhysicsVelocity = FVector::ZeroVector;

    /** True when the torpedo is above the water surface */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Physics")
    bool bAboveSurface = false;

    /** Current depth below water surface (cm, negative = above) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo|Physics")
    float CurrentDepth = 0.f;

    // -- Spawn interface ---------------------------------------------------

    /**
     * Called by SubmarineTorpedoComponent immediately after spawn.
     * Seeds the physics velocity so the torpedo inherits the submarine's
     * speed + the DA's InitialSpeedOffset along the forward direction.
     */
    void SetInitialVelocity(const FVector& WorldVelocity);

private:
    // -- Force computation -------------------------------------------------
    FVector ComputeGravityForce() const;
    FVector ComputeBuoyancyForce() const;
    FVector ComputeDragForce() const;
    FVector ComputeThrustForce() const;

    float GetWaterSurfaceZ() const;
    const UTorpedoCharacteristics* GetStats() const;
};