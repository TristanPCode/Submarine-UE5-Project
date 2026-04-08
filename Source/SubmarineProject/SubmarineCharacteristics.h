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
    int32 SpeedStatePenalty = 1;
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
    float VerticalSnapSpeed = 40.f;

    /**
     * Duration in seconds of the smooth blend when snapping to nearest pitch state.
     * During this time the submarine glides smoothly to the target angle.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vertical Movement",
        meta = (ClampMin = "0.0"))
    float PitchSnapBlendDuration = 0.12f;

    // -- Yaw (turning) ---------------------------

    /** Maximum yaw rotation speed (degrees/s) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Yaw")
    float MaxYawSpeed = 50.f;

    /** Interpolation speed for yaw (how snappy the turn feels) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Yaw")
    float YawAcceleration = 5.f;

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

    // -- Camera (POV) -----------------------------

    /** Hold duration in seconds to trigger camera switch to Periscope (P key) */
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

    /**
     * Returns the bounce entry for a given collision type.
     * Falls back to Default if the type is not found in the table.
     */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Collision")
    FCollisionBounceEntry GetCollisionBounce(ESubmarineCollisionType CollisionType) const;
};
