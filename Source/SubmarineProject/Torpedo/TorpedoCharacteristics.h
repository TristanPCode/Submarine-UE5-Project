#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NiagaraSystem.h"
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

    // -- Direct hit damage -------------------------------------------------

    /** Full damage dealt on direct contact */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage|Direct")
    float AttackDamage = 100.f;

    /** Push force applied to the struck submarine (cm/s) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage|Direct")
    float ImpactBounceForce = 800.f;

    /** Linear speed states lost by the struck submarine */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage|Direct",
        meta = (ClampMin = "0", ClampMax = "6"))
    int32 ImpactSpeedStatePenalty = 1;

    // -- Splash / indirect damage ------------------------------------------

    /**
     * Radius of the explosion splash (cm). 0 = no splash.
     * Submarines inside this radius but NOT directly hit receive indirect damage.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage|Splash",
        meta = (ClampMin = "0.0"))
    float ExplosionRadius = 300.f;

    /**
     * Damage multiplier at the CENTER of the explosion for indirect hits.
     * Range 0..1 (1 = full AttackDamage). Must be >= SplashDamageMin.
     * Direct hits always receive full AttackDamage regardless of this value.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage|Splash",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SplashDamageMax = 0.8f;

    /**
     * Damage multiplier at the EDGE of the explosion radius for indirect hits.
     * Range 0..1. Clamped to <= SplashDamageMax at runtime.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage|Splash",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SplashDamageMin = 0.1f;

    /**
     * If true, this torpedo CAN deal splash damage to the submarine that fired it.
     * The firing submarine's bImmuneToOwnTorpedoSplash (on SubmarineCharacteristics)
     * can still override this to false.
     *
     * Logic: splash hits firing sub only if (bCanSelfDamage == true) AND
     *        (FiringSubmarine->bImmuneToOwnTorpedoSplash == false).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage|Splash")
    bool bCanSelfDamage = false;

    /**
     * Returns the validated SplashDamageMin, clamped so it never exceeds SplashDamageMax.
     * Always use this getter in code instead of reading SplashDamageMin directly.
     */
    UFUNCTION(BlueprintPure, Category = "Torpedo|Damage")
    float GetSafeSplashDamageMin() const
    {
        return FMath::Min(SplashDamageMin, SplashDamageMax);
    }

    /**
     * Computes indirect splash damage for a target at the given distance from the explosion.
     * Returns 0 if distance >= ExplosionRadius or radius is 0.
     * Linear interpolation between SplashDamageMax (center) and SplashDamageMin (edge).
     */
    UFUNCTION(BlueprintPure, Category = "Torpedo|Damage")
    float ComputeSplashDamage(float DistanceFromExplosion) const
    {
        if (ExplosionRadius <= 0.f || DistanceFromExplosion >= ExplosionRadius)
            return 0.f;

        // Alpha: 1.0 at center, 0.0 at edge
        const float Alpha = 1.f - FMath::Clamp(DistanceFromExplosion / ExplosionRadius, 0.f, 1.f);
        const float Multiplier = FMath::Lerp(GetSafeSplashDamageMin(), SplashDamageMax, Alpha);
        return AttackDamage * Multiplier;
    }

    // -- VFX ---------------------------------------------------------------

    /**
     * Niagara particle system spawned at the explosion point.
     * Assign your NS_TorpedoExplosion asset here in the DataAsset editor.
     * See NIAGARA_SETUP_GUIDE.md for how to create this asset.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|VFX")
    TObjectPtr<UNiagaraSystem> ExplosionEffect;

    /**
     * Scale applied to the explosion Niagara system at spawn.
     * Increase for heavier torpedoes.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|VFX",
        meta = (ClampMin = "0.01"))
    float ExplosionEffectScale = 1.f;

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

    // -- Debug -------------------------------------------------------------
    /** If true, logs torpedo main messages. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
    bool bDebugMainMessages = false;

    /** If true, logs torpedo velocity/speed every tick (the [Torpedo] Vel= lines). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
    bool bDebugVelocityLogs = false;

    /** If true, logs torpedo hit/explode events (already useful, keep on by default). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
    bool bDebugHitLogs = true;
};