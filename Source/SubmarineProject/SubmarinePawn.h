#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SubmarineCharacteristics.h"
#include "SubmarinePawn.generated.h"

// -- Forward declarations ---------------------------------------------------
class UCameraComponent;
class UStaticMeshComponent;
class UFloatingPawnMovement;
class UInputMappingContext;
class UInputAction;
class USubmarineCollisionComponent;
class USubmarinePhysicsComponent;
struct FInputActionValue;

// -----------------------------------------------------------------------------
//  ASubmarinePawn
// -----------------------------------------------------------------------------
UCLASS()
class SUBMARINEPROJECT_API ASubmarinePawn : public APawn
{
    GENERATED_BODY()

public:
    ASubmarinePawn();

    // -- External velocity accessors (used by collision component) ------------
    float GetExternalLinearVelocity() { return ExternalLinearVelocity; }
    float GetExternalVerticalVelocity() { return ExternalVerticalVelocity; }
    float GetExternalYawVelocity() { return ExternalYawVelocity; }
    float GetExternalPitchVelocity() { return ExternalPitchVelocity; }

    void SetExternalLinearVelocity(float V) { ExternalLinearVelocity = V; }
    void SetExternalVerticalVelocity(float V) { ExternalVerticalVelocity = V; }
    void SetExternalYawVelocity(float V) { ExternalYawVelocity = V; }
    void SetExternalPitchVelocity(float V) { ExternalPitchVelocity = V; }

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    // -- Characteristics ---------------------------------------------------

    /**
     * DataAsset holding all submarine stats.
     * Assign your custom DataAsset here in the Blueprint.
     * If left empty, default values are used (built-in defaults from a
     * fallback USubmarineCharacteristics CDO).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Submarine|Stats")
    TObjectPtr<USubmarineCharacteristics> Characteristics;

    /** Applies a new characteristics asset at runtime */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Stats")
    void LoadCharacteristics(USubmarineCharacteristics* NewCharacteristics);

    // -- Enhanced Input ----------------------------------------------------

    /** Add this to your player controller's IMC stack in-editor */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputMappingContext> SubmarineMappingContext;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_MoveForward;     // Triggered / Completed

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_MoveRight;       // Axis 1D

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_MoveUp_Positive; // Triggered / Completed

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_MoveUp_Negative; // Triggered / Completed

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_Turn_Positive;  // Triggered / Completed

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_Turn_Negative;  // Triggered / Completed

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_MouseX;          // Axis 1D

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_MouseY;          // Axis 1D

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_ScrollZoom;      // Axis 1D

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_CameraPeriscope; // Digital

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_Camera3rdPerson; // Digital

    // -- Input Pending (when both directions hold) -------------------------

    bool bUpPressed = false;
    bool bDownPressed = false;

    bool bRightPressed = false;
    bool bLeftPressed = false;

    // -- Runtime state (read-only from Blueprint) --------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    ELinearSpeedState LinearSpeedState = ELinearSpeedState::Stand;

    /** Current vertical angle state index (0 = full down, mid = 0°, last = full up) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    int32 VerticalStateIndex = 0; // initialised in BeginPlay once we know the count

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float CurrentLinearSpeed = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float CurrentVerticalSpeed = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float CurrentYawSpeed = 0.f;

    /** Current pitch of the submarine in degrees */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float CurrentPitch = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    ESubmarineCameraState CameraState = ESubmarineCameraState::POV;

    // -- Camera Offset (POV) -----------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Submarine|Camera")
    FVector CameraOffset = FVector(245.f, 0.f, 140.f);

    USubmarineCollisionComponent* GetCollisionHandler() const { return CollisionHandler; }

    /** Switch camera from external code (spectator/replay system) */
    UFUNCTION(BlueprintCallable, Category = "Submarine|Camera")
    void ActivateCamera(ESubmarineCameraState NewState);

private:
    // -- Components --------------------------------------------------------
    
    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<UStaticMeshComponent> SubmarineBody;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UCameraComponent> Camera;

    /** Periscope camera — attached to root, rotates independently on yaw */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UCameraComponent> PeriscopeCamera;

    /**
     * 3rd person camera — NOT attached to submarine root so it doesn't
     * inherit submarine rotation. Updated manually in Tick.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UCameraComponent> ThirdPersonCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UFloatingPawnMovement> Movement;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USubmarineCollisionComponent> CollisionHandler;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USubmarinePhysicsComponent> PhysicsHandler;

    // -- Overlap & anti-stuck collision -------------------------------------
    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    /** Tracks how long we have been overlapping each actor (seconds) */
    TMap<TWeakObjectPtr<AActor>, float> OverlapDurations;

    void TickAntiStuck(float DeltaTime);

    // -- Linear state machine internals -------------------------------------

    /** Current raw forward axis value (-1, 0, +1) */
    float ForwardAxisValue = 0.f;

    /** True while the forward key is physically held */
    bool bForwardHeld = false;

    /** How long the forward key has been held continuously */
    float ForwardHoldTime = 0.f;

    /** Which hold interval we have already consumed (to avoid double-firing) */
    int32 ForwardHoldIntervalsConsumed = 0;

    void IncrementLinearState(int32 Direction); // Direction: +1 forward, -1 backward

    // -- Vertical angle internals ------------------------------------------

    float VerticalAxisValue = 0.f;
    bool  bVerticalHeld = false;

    /** How long the vertical key has been held (for non-linear ramp) */
    float VerticalHoldTime = 0.f;

    /** Direction of the currently held vertical key (+1 up, -1 down, 0 none) */
    float VerticalHeldDirection = 0.f;

    /** Target pitch we are interpolating toward (degrees) */
    float TargetPitch = 0.f;

    /** Pitch velocity between 2 Ticks (degrees) */
    float PitchVelocity = 0.f;

    /** Countdown timer for smooth pitch snap blend after releasing vertical key */
    float PitchSnapBlendTimer = 0.f;

    /** Accumulated angular momentum while holding vertical key (degrees/s) */
    float PitchAngularMomentum = 0.f;

    /** Resolved vertical state count (safe, odd, >=3) cached on BeginPlay */
    int32 SafeVerticalStateCount = 11;

    /** Returns the pitch angle for a given state index */
    float GetPitchForState(int32 StateIdx) const;

    /** Finds the nearest state index to a given pitch */
    int32 FindNearestVerticalState(float Pitch) const;

    // -- Turn (yaw) --------------------------------------------------------

    /** Direction of currently held turn key (+1 right, -1 left, 0 none) */
    float TurnHeldDirection = 0.f;

    // -- Exterernal Velocity (from collisions) -----------------------------

    float ExternalLinearVelocity = 0.f;
    float ExternalVerticalVelocity = 0.f;
    float ExternalYawVelocity = 0.f;
    float ExternalPitchVelocity = 0.f;

    // -- Camera state ------------------------------------------------------

    /** Accumulated hold time for periscope switch button */
    float PeriscopeHoldTimer = 0.f;
    bool  bPeriscopeHeld = false;
    bool  bPeriscopeHoldFired = false;

    /** Accumulated hold time for 3rd person switch button */
    float ThirdPersonHoldTimer = 0.f;
    bool  bThirdPersonHeld = false;
    bool  bThirdPersonHoldFired = false;

    /** Periscope yaw offset relative to submarine forward (degrees) */
    float PeriscopeYawOffset = 0.f;

    /** 3rd person spherical orbit angles (world-space yaw, pitch) */
    float ThirdPersonOrbitYaw = 180.f;
    float ThirdPersonOrbitPitch = 20.f;
    float ThirdPersonRadius = 1200.f;

    void TickCameraSwitch(float DeltaTime);
    void TickPeriscopeCamera();
    void TickThirdPersonCamera();

    // -- Tick helpers -------------------------------------------------------

    void TickLinearMovement(float DeltaTime);
    void TickVerticalMovement(float DeltaTime);
    void TickYawMovement(float DeltaTime);
    void TickFinalMovement(float DeltaTime);

    // -- Input callbacks ---------------------------------------------------
    void OnMoveForwardTriggered(const FInputActionValue& Value);
    void OnMoveForwardCompleted(const FInputActionValue& Value);

    void OnMoveRight(const FInputActionValue& Value);

    void UpdateVerticalInput();
    void OnMoveUpPressed(const FInputActionValue&);
    void OnMoveDownPressed(const FInputActionValue&);
    void OnMoveUpReleased(const FInputActionValue&);
    void OnMoveDownReleased(const FInputActionValue&);


    void UpdateTurnInput();
    void OnTurnRightPressed(const FInputActionValue&);
    void OnTurnLeftPressed(const FInputActionValue&);
    void OnTurnRightReleased(const FInputActionValue&);
    void OnTurnLeftReleased(const FInputActionValue&);

    void OnMouseX(const FInputActionValue& Value);
    void OnMouseY(const FInputActionValue& Value);
    void OnScrollZoom(const FInputActionValue& Value);

    void OnCameraPeriscopeStarted(const FInputActionValue& Value);
    void OnCameraPeriscopeTriggered(const FInputActionValue& Value);
    void OnCameraPeriscopeCompleted(const FInputActionValue& Value);

    void OnCamera3rdPersonStarted(const FInputActionValue& Value);
    void OnCamera3rdPersonTriggered(const FInputActionValue& Value);
    void OnCamera3rdPersonCompleted(const FInputActionValue& Value);

    // -- Helpers -----------------------------------------------------------

    /** Safe accessor — returns Characteristics CDO if no asset assigned */
    const USubmarineCharacteristics* GetStats() const;
};