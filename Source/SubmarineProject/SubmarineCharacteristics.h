#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SubmarineCharacteristics.generated.h"


// ---------------------------------------------
//  Collision object types
//  Used to look up per-type bounce strengths
// ---------------------------------------------
UENUM(BlueprintType)
enum class ESubmarineCollisionType : uint8
{
    Landscape,      // Terrain, sea floor
    StaticObstacle, // Rocks, walls, structures
    OtherSubmarine, // Another submarine/vehicle    
    Torpedo,        // Torpedo impact (force only, damage handled separately)
    TriggerZone,    // Currents, damage zones (no bounce)
    Default         // Fallback for unknown objects
};

// ---------------------------------------------
//  Camera state enum
// ---------------------------------------------
UENUM(BlueprintType)
enum class ESubmarineCameraState : uint8
{
    POV,
    Periscope,
    ThirdPerson
};

// ---------------------------------------------
//  One entry in the bounce force table
// ---------------------------------------------
USTRUCT(BlueprintType)
struct FCollisionBounceEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
    ESubmarineCollisionType CollisionType = ESubmarineCollisionType::Default;

    /**
     * Impulse force (cm/s) applied away from the collision normal on impact.
     * Higher = stronger bounce.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
    float BounceForce = 600.f;

    /**
     * Multiply the spinning force after an impact.
     * Higher = stronger spin.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
    float Collision_RotationFactor = 0.5f;

    /**
     * Percentage of speed lost after an impact.
     * 0.0: no lose, 1.0: full stop.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SpeedLost = 0.2f;

    /**
     * How many linear speed states are lost on impact.
     * 0 = no penalty, 1 = drops one state, 2 = drops two, etc.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision",
        meta = (ClampMin = "0", ClampMax = "6"))
    int32 SpeedStatePenalty = 0;
};

// ---------------------------------------------
//  Linear speed state enum
//  Order matters: index is used for state math
// ---------------------------------------------
UENUM(BlueprintType)
enum class ELinearSpeedState : uint8
{
    BackwardMAX = 0,
    BackwardMED,
    BackwardMIN,
    Stand,
    ForwardMIN,
    ForwardMED,
    ForwardMAX
};

// ---------------------------------------------
//  Linear speed table entry
// ---------------------------------------------
USTRUCT(BlueprintType)
struct FLinearSpeedEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
    ELinearSpeedState State = ELinearSpeedState::Stand;

    /** Target speed in cm/s for this state (negative = backward) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
    float TargetSpeed = 0.f;
};

// ---------------------------------------------
//  Main DataAsset
// ---------------------------------------------
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API USubmarineCharacteristics : public UDataAsset
{
    GENERATED_BODY()

public:
    USubmarineCharacteristics();

    // -- Linear movement -----------------------

    /** Speed table: one entry per ELinearSpeedState */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Linear Movement")
    TArray<FLinearSpeedEntry> LinearSpeedTable;

    /** How fast the submarine accelerates toward a higher speed state (cm/s²) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Linear Movement")
    float LinearAcceleration = 1400.f;

    /** How fast the submarine decelerates toward a lower speed state (cm/s²) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Linear Movement")
    float LinearDeceleration = 2000.f;

    /**
     * Delay in seconds between automatic state increments when holding
     * a forward/backward key. Spamming the key skips this delay.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Linear Movement")
    float LinearStateHoldInterval = 0.25f;

    // -- Vertical movement -----------------------

    /**
     * Number of vertical angle states (must be odd, >= 3).
     * States are symmetrically distributed between -MaxPitchAngle and +MaxPitchAngle.
     * The middle state is always 0° (Stand).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement",
        meta = (ClampMin = "3"))
    int32 VerticalStateCount = 11;

    /**
     * Number of states around Stand (center) that are SKIPPED (ghost states).
     * Must be even. If odd, +1 is added. Max = VerticalStateCount - 3.
     * These angles exist mathematically but are never snapped to.
     * Example: VerticalStateCount=21, GhostVerticalStateCount=2 skips
     * the two states closest to 0° (the deadzone near horizontal).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement",
        meta = (ClampMin = "0"))
    int32 GhostVerticalStateCount = 1;

    /** Maximum pitch angle in degrees (both up and down) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement",
        meta = (ClampMin = "1.0", ClampMax = "89.0"))
    float MaxPitchAngle = 35.f;

    /** Maximum vertical speed (cm/s) reached at MaxPitchAngle */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement")
    float MaxVerticalSpeed = 800.f;

    /**
     * When holding a vertical key, angle change starts slow and grows.
     * This is the initial angular speed (degrees/s) at the moment you press the key.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement")
    float VerticalHoldInitialRate = 5.f;

    /**
     * Rate at which the angular speed grows while holding (degrees/s per second).
     * Higher = faster ramp-up.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement")
    float VerticalHoldAccelRate = 30.f;

    /** How fast the pitch snaps to the nearest angle state when no key is held (degrees/s) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement")
    float VerticalSnapSpeed = 20.f;

    /** Acceleration when actively pitching up/down (degrees/s²) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement")
    float VerticalAcceleration = 800.f;

    /** Deceleration when releasing vertical input (degrees/s²) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement")
    float VerticalDeceleration = 1200.f;

    /**
     * Duration in seconds of the smooth blend when snapping to nearest pitch state.
     * During this time the submarine glides smoothly to the target angle.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement",
        meta = (ClampMin = "0.0"))
    float PitchSnapBlendDuration = 0.12f;

    /**
     * How much accumulated angular momentum influences the snap target.
     * Higher = momentum carries further before snapping to nearest state.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float PitchMomentumInfluence = 0.4f;

    // -- Yaw (turning) ---------------------------

    /** Maximum yaw rotation speed (degrees/s) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Yaw")
    float MaxYawSpeed = 50.f;

    /** How fast yaw accelerates when turning key is held (degrees/s²) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Yaw")
    float YawAcceleration = 50.f;

    /** How fast yaw decelerates when turning key is released (degrees/s²) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Yaw")
    float YawDeceleration = 30.f;

    // -- Speed boost cross-effects ---------------

    /** % boost applied to vertical speed when LinearSpeedState == Stand (0.4 = 40%) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed Boost",
        meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float VerticalBoostWhenLinearStand = 0.4f;

    /** % boost applied to linear speed when vertical angle state is 0° (0.2 = 20%) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed Boost",
        meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float LinearBoostWhenVerticalStand = 0.2f;

    // -- Health ----------------------------------

    /** Maximum health of the submarine */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health",
        meta = (ClampMin = "1.0"))
    float MaxHealth = 1000.f;

    /**
     * Damage resistance multiplier (0.0 = immune, 1.0 = full damage, 2.0 = double damage).
     * Applied to all incoming damage before subtracting from health.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health",
        meta = (ClampMin = "0.0"))
    float DamageResistance = 1.f;

    // -- Collision --------------------------------

    /**
     * Per-object-type bounce force and speed penalty table.
     * Add one entry per ESubmarineCollisionType you want to customize.
     * If a type is missing, the Default entry is used as fallback.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
    TArray<FCollisionBounceEntry> CollisionBounceTable;

    /**
     * Deceleration of the bounce force for linear movement.
     * Higher = stronger spin.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
    float Deceleration_Linear = 10.0f;

    /**
     * Deceleration of the bounce force for rotation.
     * Higher = stronger spin.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
    float Deceleration_Rotation = 60.0f;

    /**
     * Divisor for speed-proportional bounce force scaling.
     * BounceForce is multiplied by (DefaultMult + |CurrentLinearSpeed| / BounceSpeedDivisor).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision",
        meta = (ClampMin = "1.0"))
    float BounceSpeedDivisor = 800.f;

    /**
     * Multiplier for bounce force with no speed.
     * BounceForce is multiplied by (0.2 + |CurrentLinearSpeed| / BounceSpeedDivisor).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DefaultMult = 0.2f;

    // -- Anti-stuck ------------------------------

    /** Seconds of continuous overlap before the expulsion force fires */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision|AntiStuck",
        meta = (ClampMin = "0.06"))
    float AntiStuckThreshold = 0.3f;

    /** Expulsion impulse magnitude (cm/s) away from penetrating object */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision|AntiStuck")
    float AntiStuckForce = 3000.f;

    /** Cooldown (seconds) between anti-stuck firings against the same actor */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision|AntiStuck",
        meta = (ClampMin = "0.06"))
    float AntiStuckCooldown = 0.15f;

    // -- Physics: General ----------------------

    /** Gravity acceleration (cm/s²) — positive value, applied downward */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|General")
    float GravityAcceleration = 980.f;

    /** Safety clamp on total physics velocity magnitude (cm/s) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|General")
    float PhysicsMaxSpeed = 5000.f;

    /** Max thrust force the engines can apply (cm/s²) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|General")
    float MaxThrustForce = 3000.f;

    // -- Physics: Buoyancy ---------------------

    /**
     * Ratio of buoyancy to gravity at full submersion.
     * 1.0 = perfectly neutral, <1.0 = slowly sinks, >1.0 = rises.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Buoyancy",
        meta = (ClampMin = "0.0"))
    float BuoyancyRatio = 1.0f;

    /** World Z coordinate of the water surface (cm) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Buoyancy")
    float WaterSurfaceZ = 0.f;

    /**
     * Depth range over which buoyancy transitions from 0 to full (cm).
     * Models partial submersion near the surface.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Buoyancy",
        meta = (ClampMin = "1.0"))
    float SurfaceTransitionDepth = 200.f;

    // -- Physics: Drag -------------------------

    /**
     * If true, uses the full 6DOF drag tensor (different per axis).
     * If false, uses simple scalar drag.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Drag")
    bool bUseComplexDrag = false;

    /** Simple drag: scalar coefficient (F = -Cd * v²) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Drag",
        meta = (ClampMin = "0.0", EditCondition = "!bUseComplexDrag"))
    float SimpleDragCoefficient = 0.001f;

    /**
     * Complex drag tensor coefficients (X=forward, Y=lateral, Z=vertical).
     * Forward drag is low (streamlined hull), lateral/vertical much higher.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Drag",
        meta = (EditCondition = "bUseComplexDrag"))
    FVector DragTensor = FVector(0.0005f, 0.005f, 0.003f);

    // -- Physics: Depth Pressure ---------------

    /** Enable depth pressure effects */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Depth")
    bool bEnableDepthPhysics = false;

    /**
     * Global influence multiplier for all depth pressure effects.
     * 0 = no effect, 1 = full effect.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Depth",
        meta = (ClampMin = "0.0", ClampMax = "1.0",
            EditCondition = "bEnableDepthPhysics"))
    float DepthPhysicsInfluence = 1.0f;

    /**
     * Depth (cm below surface) at which pressure effects begin.
     * Above this depth, no pressure penalty.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Depth",
        meta = (ClampMin = "0.0", EditCondition = "bEnableDepthPhysics"))
    float PressureDepthThreshold = 5000.f;

    /**
     * Rate at which pressure increases per cm of depth beyond threshold.
     * Also attenuates buoyancy at depth.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|Depth",
        meta = (ClampMin = "0.0", EditCondition = "bEnableDepthPhysics"))
    float DepthPressureCoefficient = 0.00001f;

    // -- Camera (POV) -----------------------------

    /** Hold duration in seconds to trigger camera switch */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Switching")
    float CameraSwitchHoldDuration = 0.3f;

    /** If false, F1 / 3rd person mode is disabled entirely */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Switching")
    bool bEnable3rdPersonCamera = false;

    // -- Camera (Periscope) -----------------------

    /** Local offset of the periscope camera relative to submarine root */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Periscope")
    FVector PeriscopeCameraOffset = FVector(50.f, 0.f, 280.f);

    /** Sensitivity of mouse X rotation in periscope mode (degrees/unit) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Periscope")
    float PeriscopeYawSensitivity = 1.f;

    // -- Camera (3rd Person) ----------------------

    /** Local offset of the 3rd person camera pivot relative to submarine root */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson")
    FVector ThirdPersonPivotOffset = FVector(0.f, 0.f, 50.f);

    /** Starting distance from submarine center */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson")
    float ThirdPersonInitialRadius = 1200.f;

    /** Minimum zoom distance */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson",
        meta = (ClampMin = "100.0"))
    float ThirdPersonMinRadius = 400.f;

    /** Maximum zoom distance */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson")
    float ThirdPersonMaxRadius = 3000.f;

    /** Scroll zoom speed (radius change per scroll unit) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson")
    float ThirdPersonScrollSpeed = 200.f;

    /** Mouse X sensitivity for orbiting (degrees/unit) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson")
    float ThirdPersonYawSensitivity = 1.f;

    /** Mouse Y sensitivity for orbiting (degrees/unit) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson")
    float ThirdPersonPitchSensitivity = 1.f;

    /** Initial horizontal orbit angle in degrees */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson")
    float ThirdPersonInitialYaw = 180.f;

    /** Initial vertical orbit angle in degrees (0=horizon, 90=top) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson",
        meta = (ClampMin = "-89.0", ClampMax = "89.0"))
    float ThirdPersonInitialPitch = 20.f;

    /** Minimum vertical orbit angle (prevents going below submarine) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson",
        meta = (ClampMin = "-89.0"))
    float ThirdPersonMinPitch = -80.f;

    /** Maximum vertical orbit angle (prevents flipping over top) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|ThirdPerson",
        meta = (ClampMin = "0.0", ClampMax = "89.0"))
    float ThirdPersonMaxPitch = 80.f;

    // -- Helpers (callable from C++ & BP) ------

    /** Returns the target speed (cm/s) for a given linear state */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Speed")
    float GetLinearTargetSpeed(ELinearSpeedState State) const;

    /**
     * Returns the sanitised vertical state count:
     * enforces odd number and >= 3.
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Vertical")
    int32 GetSafeVerticalStateCount() const;

    /**
     * Returns the pitch angle (degrees) for a given vertical state index.
     * Index 0 = most negative angle, middle = 0, last = most positive.
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Vertical")
    float GetPitchForVerticalState(int32 StateIndex) const;

    /** Returns sanitised ghost state count (even, clamped to VerticalStateCount-3) */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Vertical")
    int32 GetSafeGhostStateCount() const;

    /**
     * Returns whether a given state index is a ghost (skipped) state.
     * Ghost states are the ones immediately surrounding the center (Stand).
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Vertical")
    bool IsGhostState(int32 StateIndex) const;

    /**
     * Returns the bounce entry for a given collision type.
     * Falls back to Default if the type is not found in the table.
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Collision")
    FCollisionBounceEntry GetCollisionBounce(ESubmarineCollisionType CollisionType) const;
};
