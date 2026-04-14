#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TorpedoPawn.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UTorpedoPhysicsComponent;
class UTorpedoCharacteristics;

// Broadcast when the torpedo hits something and explodes
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTorpedoImpact,
    AActor*, HitActor,
    FVector, ImpactLocation);

// Broadcast when the torpedo's lifetime expires
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTorpedoExpired);

/**
 * ATorpedoPawn
 *
 * Base class for all torpedo actors. Create a Blueprint subclass for each
 * torpedo variant (Light/Normal/Heavy) and assign its own mesh + DA there.
 *
 * Lifecycle:
 *   1. SubmarineTorpedoComponent spawns this (deferred).
 *   2. SetCharacteristics() is called to hand over the DA.
 *   3. PhysicsComponent is seeded with initial world velocity.
 *   4. FinishSpawning() completes the deferred spawn.
 *   5. Each tick: physics integrates, owner moves via AddActorWorldOffset.
 *   6. On hit OR lifetime expiry: Explode() fires delegates and destroys self.
 *
 * Camera:
 *   Both cameras are created in C++ but only activated when a player
 *   possesses this pawn (e.g. via spectator/replay system).
 *   POV camera is attached to the root and inherits all torpedo rotation.
 *   3rd person camera is detached at BeginPlay and updated manually each tick.
 */
UCLASS(Abstract)
class SUBMARINEPROJECT_API ATorpedoPawn : public APawn
{
    GENERATED_BODY()

public:
    ATorpedoPawn();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    // -- Setup (called by SubmarineTorpedoComponent after deferred spawn) ---

    /** Assigns the characteristics DA and wires it to the physics component */
    UFUNCTION(BlueprintCallable, Category = "Torpedo")
    void SetCharacteristics(UTorpedoCharacteristics* InCharacteristics);

    /**
     * Seeds the physics component with the initial world-space velocity
     * (submarine forward speed + InitialSpeedOffset from DA).
     * Must be called AFTER SetCharacteristics and BEFORE FinishSpawning.
     */
    void SetInitialVelocity(const FVector& WorldVelocity);

    // -- Shooter -----------------------------------------------------------
    /** The Shooter actor that fired this torpedo — never detonates on them */
    UPROPERTY()
    TObjectPtr<AActor> FiringShooter;

    // -- Runtime -----------------------------------------------------------

    /** Current characteristics asset (set by spawner) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Torpedo")
    TObjectPtr<UTorpedoCharacteristics> Characteristics;

    // -- Events ------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnTorpedoImpact OnImpact;

    UPROPERTY(BlueprintAssignable, Category = "Torpedo|Events")
    FOnTorpedoExpired OnExpired;

    // -- Camera state ------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Torpedo|Camera")
    void ActivatePOVCamera();

    UFUNCTION(BlueprintCallable, Category = "Torpedo|Camera")
    void ActivateThirdPersonCamera();

protected:
    // -- Components --------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> TorpedoBody;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UCameraComponent> POVCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UCameraComponent> ThirdPersonCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UTorpedoPhysicsComponent> PhysicsHandler;

    // -- Hit handling (override in Blueprint or subclass) ------------------

    UFUNCTION()
    void OnTorpedoHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, FVector NormalImpulse,
        const FHitResult& Hit);

    /** Called when the torpedo impacts something. Override in BP for FX. */
    UFUNCTION(BlueprintNativeEvent, Category = "Torpedo")
    void Explode(AActor* HitActor, const FVector& ImpactLocation);
    virtual void Explode_Implementation(AActor* HitActor, const FVector& ImpactLocation);

private:
    // -- Lifetime ----------------------------------------------------------
    float LifetimeElapsed = 0.f;
    bool  bExploded = false;

    // -- 3rd person orbit state -------------------------------------------
    float ThirdPersonOrbitYaw = 180.f;
    float ThirdPersonOrbitPitch = 10.f;
    float ThirdPersonRadius = 400.f;

    void TickThirdPersonCamera();

    const UTorpedoCharacteristics* GetStats() const;
};