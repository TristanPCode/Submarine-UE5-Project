#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TorpedoCharacteristics.generated.h"

// ---------------------------------------------
//  Torpedo weight class — affects DA defaults
//  and is exposed for Blueprint logic
// ---------------------------------------------
UENUM(BlueprintType)
enum class ETorpedoType : uint8
{
    Light,   // Fast, low damage, low drag
    Normal,  // Balanced
    Heavy    // Slow, high damage, high drag
};

UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UTorpedoCharacteristics : public UDataAsset
{
    GENERATED_BODY()

public:
    // -- Identity ----------------------------------------------------------

    /** Visual/logical weight class of this torpedo */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Identity")
    ETorpedoType TorpedoType = ETorpedoType::Normal;

    // -- Movement ----------------------------------------------------------

    /**
     * Extra speed (cm/s) added ON TOP of the launching submarine's current
     * linear speed at the moment of firing.
     * This is the "kick" the torpedo gets from the launch tube.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Movement")
    float InitialSpeedOffset = 1500.f;

    /**
     * Engine thrust acceleration (cm/s²) applied each tick toward MaxSpeed.
     * Set to 0 for a purely ballistic torpedo (no self-propulsion after launch).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Movement")
    float TorpedoAcceleration = 800.f;

    /** Maximum self-propelled speed of the torpedo (cm/s) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Movement")
    float MaxSpeed = 4500.f;

    /** Maximum lifetime in seconds before the torpedo self-destructs */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Movement",
        meta = (ClampMin = "0.5"))
    float MaxLifetime = 12.f;

    // -- Physics -----------------------------------------------------------

    /**
     * Ratio of buoyancy to gravity at full submersion.
     * 1.0 = neutrally buoyant, <1.0 = sinks, >1.0 = rises.
     * Torpedoes are typically slightly negative (sink slowly if not thrusting).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Physics",
        meta = (ClampMin = "0.0"))
    float BuoyancyRatio = 0.85f;

    /** Gravity acceleration acting on the torpedo (cm/s²) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Physics")
    float GravityAcceleration = 980.f;

    /** World Z of the water surface. Should match the level's submarine DA value. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Physics")
    float WaterSurfaceZ = 1000.f;

    /**
     * Depth range (cm) over which buoyancy transitions from 0 to full near surface.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Physics",
        meta = (ClampMin = "1.0"))
    float SurfaceTransitionDepth = 100.f;

    /**
     * Simple drag coefficient — opposes velocity linearly.
     * F_drag = -Cd * v  (lighter than the sub; torpedoes are streamlined)
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Physics",
        meta = (ClampMin = "0.0"))
    float DragCoefficient = 0.0008f;

    /** Safety clamp on total physics velocity (cm/s) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Physics")
    float PhysicsMaxSpeed = 8000.f;

    // -- Impact / Damage ---------------------------------------------------

    /** Direct damage dealt to a submarine on contact */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage")
    float AttackDamage = 100.f;

    /** Radius of explosion splash damage (0 = no splash) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage",
        meta = (ClampMin = "0.0"))
    float ExplosionRadius = 300.f;

    /**
     * Damage falloff at the edge of the explosion radius.
     * 0 = zero damage at edge, 1 = full damage all the way to edge.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SplashDamageFalloff = 0.2f;

    /**
     * Bounce/push force (cm/s) applied to the hit submarine.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage")
    float ImpactBounceForce = 800.f;

    /**
     * How many linear speed states the struck submarine loses on impact.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage",
        meta = (ClampMin = "0", ClampMax = "6"))
    int32 ImpactSpeedStatePenalty = 1;

    // -- Camera (POV) ------------------------------------------------------

    /** Local offset of the torpedo's POV camera relative to its root */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|POV")
    FVector POVCameraOffset = FVector(80.f, 0.f, 10.f);

    // -- Camera (3rd Person) -----------------------------------------------

    /** Starting orbit radius for the torpedo's 3rd-person camera (cm) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson")
    float ThirdPersonInitialRadius = 400.f;

    /** Min zoom distance */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson",
        meta = (ClampMin = "50.0"))
    float ThirdPersonMinRadius = 100.f;

    /** Max zoom distance */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson")
    float ThirdPersonMaxRadius = 1200.f;

    /** Initial horizontal orbit angle (degrees) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson")
    float ThirdPersonInitialYaw = 180.f;

    /** Initial vertical orbit angle (degrees, 0=horizon, 90=top) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson",
        meta = (ClampMin = "-89.0", ClampMax = "89.0"))
    float ThirdPersonInitialPitch = 10.f;

    /** Min vertical orbit angle */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson",
        meta = (ClampMin = "-89.0"))
    float ThirdPersonMinPitch = -80.f;

    /** Max vertical orbit angle */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson",
        meta = (ClampMin = "0.0", ClampMax = "89.0"))
    float ThirdPersonMaxPitch = 80.f;

    /** Mouse X sensitivity when orbiting */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson")
    float ThirdPersonYawSensitivity = 1.f;

    /** Mouse Y sensitivity when orbiting */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson")
    float ThirdPersonPitchSensitivity = 1.f;

    /** Scroll zoom speed */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson")
    float ThirdPersonScrollSpeed = 100.f;

    /** Pivot height offset above torpedo centre for 3rd person look-at */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Camera|ThirdPerson")
    float ThirdPersonPivotOffsetZ = 0.f;
};