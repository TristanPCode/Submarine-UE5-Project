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
    TObjectPtr<UInputAction> IA_MoveForward;   // Triggered / Completed

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_MoveRight;     // Axis 1D

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_MoveUp;        // Triggered / Completed

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Input")
    TObjectPtr<UInputAction> IA_Turn;          // Axis 1D

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

    // -- Camera Offset --------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Submarine|Camera")
    FVector CameraOffset = FVector(245.f, 0.f, 140.f);

private:
    // -- Components --------------------------------------------------------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UFloatingPawnMovement> Movement;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USubmarineCollisionComponent> CollisionHandler;

    // -- Hit callback --------------------------------------------------------
    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);

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

    /** Target pitch we are interpolating toward (degrees) */
    float TargetPitch = 0.f;

    /** Resolved vertical state count (safe, odd, >=3) cached on BeginPlay */
    int32 SafeVerticalStateCount = 11;

    /** Returns the pitch angle for a given state index */
    float GetPitchForState(int32 StateIdx) const;

    /** Finds the nearest state index to a given pitch */
    int32 FindNearestVerticalState(float Pitch) const;

    void TickLinearMovement(float DeltaTime);
    void TickVerticalMovement(float DeltaTime);
    void TickYawMovement(float DeltaTime);
    void TickFinalMovement(float DeltaTime);

    // -- Input callbacks ---------------------------------------------------
    void OnMoveForwardTriggered(const FInputActionValue& Value);
    void OnMoveForwardCompleted(const FInputActionValue& Value);

    void OnMoveRight(const FInputActionValue& Value);

    void OnMoveUpTriggered(const FInputActionValue& Value);
    void OnMoveUpCompleted(const FInputActionValue& Value);

    void OnTurn(const FInputActionValue& Value);

    // -- Helpers -----------------------------------------------------------

    /** Safe accessor — returns Characteristics CDO if no asset assigned */
    const USubmarineCharacteristics* GetStats() const;
};