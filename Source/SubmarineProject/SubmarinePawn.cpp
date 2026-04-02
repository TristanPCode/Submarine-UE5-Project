// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarinePawn.h"
#include "SubmarineCharacteristics.h"
#include "SubmarineCollisionComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/FloatingPawnMovement.h"

// Enhanced Input
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

// -----------------------------------------------------------------------------
//  Constructor
// -----------------------------------------------------------------------------
ASubmarinePawn::ASubmarinePawn()
{
    PrimaryActorTick.bCanEverTick = true;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    // Camera attaches to the DefaultSceneRoot that already parents all your
    // mesh components (body, propeller, periscope, radar) in the Blueprint.
    // We create it here so it exists; its offset is overridden from
    // Characteristics in BeginPlay (or you can set it in BP).
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraPOV"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetRelativeLocation(CameraOffset);
    //Camera->SetRelativeLocation(FVector(-1500.f, 0.f, 200.f));

    // Use bAbsoluteRotation = false (default) so the camera inherits the
    // submarine's yaw AND pitch, keeping the offset consistent during turns.
    // The camera does NOT add extra rotation of its own; pitch is driven by
    // the submarine mesh rotation directly.
    Camera->bUsePawnControlRotation = false;

    Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
    Movement->UpdatedComponent = RootComponent;

    CollisionHandler = CreateDefaultSubobject<USubmarineCollisionComponent>(TEXT("CollisionHandler"));
}

// -----------------------------------------------------------------------------
//  BeginPlay
// -----------------------------------------------------------------------------
void ASubmarinePawn::BeginPlay()
{
    Super::BeginPlay();

    SafeVerticalStateCount = GetStats()->GetSafeVerticalStateCount();
    VerticalStateIndex = SafeVerticalStateCount / 2;
    CurrentPitch = 0.f;
    TargetPitch = 0.f;

    // Bind to ALL primitive components (covers Blueprint mesh components)
    TArray<UPrimitiveComponent*> PrimComponents;
    GetComponents<UPrimitiveComponent>(PrimComponents);
    for (UPrimitiveComponent* Prim : PrimComponents)
    {
        Prim->SetGenerateOverlapEvents(true);
        Prim->OnComponentBeginOverlap.AddDynamic(this, &ASubmarinePawn::OnOverlapBegin);
        UE_LOG(LogTemp, Warning, TEXT("Bound overlap to: %s"), *Prim->GetName());
    }

    // Register Enhanced Input mapping context
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (SubmarineMappingContext)
                Subsystem->AddMappingContext(SubmarineMappingContext, 0);
        }
    }

    if (Camera)
    {
        Camera->SetRelativeLocation(CameraOffset);
        UE_LOG(LogTemp, Warning, TEXT("Applied CameraOffset: %s"),
            *Camera->GetRelativeLocation().ToString());
    }
}

// -----------------------------------------------------------------------------
//  Tick
// -----------------------------------------------------------------------------
void ASubmarinePawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    TickLinearMovement(DeltaTime);
    TickVerticalMovement(DeltaTime);
    TickYawMovement(DeltaTime);
    TickFinalMovement(DeltaTime);
}

// -----------------------------------------------------------------------------
//  Input binding
// -----------------------------------------------------------------------------
void ASubmarinePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!EIC)
    {
        UE_LOG(LogTemp, Error,
            TEXT("ASubmarinePawn: PlayerInputComponent is not UEnhancedInputComponent. "
                "Make sure Enhanced Input is enabled in Project Settings."));
        return;
    }

    if (IA_MoveForward)
    {
        EIC->BindAction(IA_MoveForward, ETriggerEvent::Triggered, this,
            &ASubmarinePawn::OnMoveForwardTriggered);
        EIC->BindAction(IA_MoveForward, ETriggerEvent::Completed, this,
            &ASubmarinePawn::OnMoveForwardCompleted);
    }
    if (IA_MoveRight)
        EIC->BindAction(IA_MoveRight, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnMoveRight);

    if (IA_MoveUp)
    {
        EIC->BindAction(IA_MoveUp, ETriggerEvent::Triggered, this,
            &ASubmarinePawn::OnMoveUpTriggered);
        EIC->BindAction(IA_MoveUp, ETriggerEvent::Completed, this,
            &ASubmarinePawn::OnMoveUpCompleted);
    }
    if (IA_Turn)
        EIC->BindAction(IA_Turn, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnTurn);
}

// -----------------------------------------------------------------------------
//  Characteristics helpers
// -----------------------------------------------------------------------------
const USubmarineCharacteristics* ASubmarinePawn::GetStats() const
{
    if (Characteristics)
        return Characteristics;

    // Fallback: use CDO defaults so the submarine always has valid stats
    return GetDefault<USubmarineCharacteristics>();
}

void ASubmarinePawn::LoadCharacteristics(USubmarineCharacteristics* NewCharacteristics)
{
    if (!NewCharacteristics) return;

    Characteristics = NewCharacteristics;

    // Re-cache vertical state count and clamp current index
    SafeVerticalStateCount = GetStats()->GetSafeVerticalStateCount();
    VerticalStateIndex = FMath::Clamp(VerticalStateIndex, 0, SafeVerticalStateCount - 1);
}

// -----------------------------------------------------------------------------
//  Linear movement tick
// -----------------------------------------------------------------------------
void ASubmarinePawn::TickLinearMovement(float DeltaTime)
{
    const USubmarineCharacteristics* Stats = GetStats();

    if (bForwardHeld)
    {
        ForwardHoldTime += DeltaTime;

        // How many intervals have elapsed?
        const int32 IntervalsElapsed =
            FMath::FloorToInt(ForwardHoldTime / Stats->LinearStateHoldInterval);

        // Fire one state increment per new interval consumed
        while (ForwardHoldIntervalsConsumed < IntervalsElapsed)
        {
            IncrementLinearState(ForwardAxisValue > 0.f ? 1 : -1);
            ForwardHoldIntervalsConsumed++;
        }
    }

    // Resolve target speed
    float TargetSpeed = Stats->GetLinearTargetSpeed(LinearSpeedState);

    // Cross-boost: vertical is not moving -> linear gets a boost
    const bool bVerticalStand = (VerticalStateIndex == SafeVerticalStateCount / 2);
    if (bVerticalStand)
        TargetSpeed *= (1.f + Stats->LinearBoostWhenVerticalStand);

    // Choose acceleration or deceleration rate
    const float Rate = (FMath::Abs(TargetSpeed) >= FMath::Abs(CurrentLinearSpeed))
        ? Stats->LinearAcceleration
        : Stats->LinearDeceleration;

    CurrentLinearSpeed = FMath::FInterpConstantTo(CurrentLinearSpeed, TargetSpeed, DeltaTime, Rate);
}

// -----------------------------------------------------------------------------
//  Vertical movement tick
// -----------------------------------------------------------------------------
void ASubmarinePawn::TickVerticalMovement(float DeltaTime)
{
    const USubmarineCharacteristics* Stats = GetStats();

    if (bVerticalHeld)
    {
        // Non-linear hold: angular speed grows over time
        VerticalHoldTime += DeltaTime;
        const float AngularRate =
            Stats->VerticalHoldInitialRate +
            Stats->VerticalHoldAccelRate * VerticalHoldTime;

        // Apply free-form angle delta (bypasses states while held)
        TargetPitch += VerticalAxisValue * AngularRate * DeltaTime;
        TargetPitch = FMath::Clamp(TargetPitch, -Stats->MaxPitchAngle, Stats->MaxPitchAngle);

        // Keep the vertical state index in sync with the current free pitch
        VerticalStateIndex = FindNearestVerticalState(TargetPitch);
    }
    else
    {
        // Snap toward nearest state angle
        const float StatePitch = GetPitchForState(VerticalStateIndex);
        TargetPitch = FMath::FInterpConstantTo(
            TargetPitch, StatePitch, DeltaTime, Stats->VerticalSnapSpeed);
    }

    // Interpolate current pitch toward target pitch
    CurrentPitch = FMath::FInterpConstantTo(
        CurrentPitch, TargetPitch, DeltaTime, Stats->VerticalSnapSpeed * 2.f);

    // Apply pitch to actor rotation (this also moves the camera since it's attached)
    FRotator Rot = GetActorRotation();
    Rot.Pitch = CurrentPitch;
    SetActorRotation(Rot);

    // Convert pitch to vertical speed
    const float PitchRatio = CurrentPitch / FMath::Max(Stats->MaxPitchAngle, 1.f);
    float VerticalSpeed = PitchRatio * Stats->MaxVerticalSpeed;

    // Cross-boost: submarine is linearly still -> vertical gets a boost
    if (LinearSpeedState == ELinearSpeedState::Stand)
        VerticalSpeed *= (1.f + Stats->VerticalBoostWhenLinearStand);

    CurrentVerticalSpeed = VerticalSpeed;
}

// -----------------------------------------------------------------------------
//  Yaw movement tick
// -----------------------------------------------------------------------------
void ASubmarinePawn::TickYawMovement(float DeltaTime)
{
    // TargetYawSpeed is set each frame by OnTurn; if no input it will be 0
    // (ETriggerEvent::Triggered only fires while held)
    const USubmarineCharacteristics* Stats = GetStats();

    CurrentYawSpeed = FMath::FInterpTo(CurrentYawSpeed, 0.f, DeltaTime, Stats->YawAcceleration);
    // Note: the actual target yaw is applied in OnTurn every frame while held.
    // When released, CurrentYawSpeed decays to 0 here.

    FRotator Rot = GetActorRotation();
    Rot.Yaw += CurrentYawSpeed * DeltaTime;
    SetActorRotation(Rot);
}

// -----------------------------------------------------------------------------
//  Final movement application
// -----------------------------------------------------------------------------
void ASubmarinePawn::TickFinalMovement(float DeltaTime)
{
    // Linear movement along the submarine's forward axis (pitch is already baked in)
    const FVector LinearDelta = GetActorForwardVector() * CurrentLinearSpeed * DeltaTime;

    // Vertical movement is purely world Z
    const FVector VerticalDelta = FVector(0.f, 0.f, CurrentVerticalSpeed * DeltaTime);

    AddActorWorldOffset(LinearDelta + VerticalDelta, true);
}

// -----------------------------------------------------------------------------
//  Linear state machine helper
// -----------------------------------------------------------------------------
void ASubmarinePawn::IncrementLinearState(int32 Direction)
{
    // ELinearSpeedState is ordered 0..6 (BackwardMAX -> ForwardMAX)
    const int32 Current = static_cast<int32>(LinearSpeedState);
    const int32 Next = FMath::Clamp(Current + Direction, 0, 6);
    LinearSpeedState = static_cast<ELinearSpeedState>(Next);
}

// -----------------------------------------------------------------------------
//  Vertical state helpers
// -----------------------------------------------------------------------------
float ASubmarinePawn::GetPitchForState(int32 StateIdx) const
{
    return GetStats()->GetPitchForVerticalState(StateIdx);
}

int32 ASubmarinePawn::FindNearestVerticalState(float Pitch) const
{
    int32 BestIndex = SafeVerticalStateCount / 2;
    float BestDistance = FLT_MAX;

    for (int32 i = 0; i < SafeVerticalStateCount; ++i)
    {
        const float Dist = FMath::Abs(GetPitchForState(i) - Pitch);
        if (Dist < BestDistance)
        {
            BestDistance = Dist;
            BestIndex = i;
        }
    }
    return BestIndex;
}

// -----------------------------------------------------------------------------
//  Input callbacks
// -----------------------------------------------------------------------------

void ASubmarinePawn::OnMoveForwardTriggered(const FInputActionValue& Value)
{
    const float Axis = Value.Get<float>(); // +1 or -1

    if (!bForwardHeld)
    {
        // First frame of press: immediately fire one state increment (tap behaviour)
        bForwardHeld = true;
        ForwardHoldTime = 0.f;
        ForwardHoldIntervalsConsumed = 0;
        ForwardAxisValue = Axis;
        IncrementLinearState(Axis > 0.f ? 1 : -1);
    }
    else if (Axis != ForwardAxisValue)
    {
        // Direction changed while held: reset hold tracking
        ForwardAxisValue = Axis;
        ForwardHoldTime = 0.f;
        ForwardHoldIntervalsConsumed = 0;
        IncrementLinearState(Axis > 0.f ? 1 : -1);
    }
    // Ongoing hold is handled in TickLinearMovement via ForwardHoldTime
}

void ASubmarinePawn::OnMoveForwardCompleted(const FInputActionValue& Value)
{
    bForwardHeld = false;
    ForwardHoldTime = 0.f;
    ForwardHoldIntervalsConsumed = 0;
    ForwardAxisValue = 0.f;
    // NOTE: We intentionally do NOT reset LinearSpeedState here.
    // The state persists until the player actively changes it.
}

void ASubmarinePawn::OnMoveRight(const FInputActionValue& Value)
{
    const float Axis = Value.Get<float>();
    AddMovementInput(GetActorRightVector() * Axis);
}

void ASubmarinePawn::OnMoveUpTriggered(const FInputActionValue& Value)
{
    const float Axis = Value.Get<float>(); // +1 or -1

    if (!bVerticalHeld)
    {
        bVerticalHeld = true;
        VerticalHoldTime = 0.f;
        VerticalAxisValue = Axis;
    }
    else
    {
        VerticalAxisValue = Axis;
    }
}

void ASubmarinePawn::OnMoveUpCompleted(const FInputActionValue& Value)
{
    bVerticalHeld = false;
    VerticalHoldTime = 0.f;
    VerticalAxisValue = 0.f;
    // Snap to nearest state is now handled automatically in TickVerticalMovement
}

void ASubmarinePawn::OnTurn(const FInputActionValue& Value)
{
    const float Axis = Value.Get<float>();
    // Override current yaw speed target each frame while held
    CurrentYawSpeed = Axis * GetStats()->MaxYawSpeed;
}

// -----------------------------------------------------------------------------
//  Collision hit
// -----------------------------------------------------------------------------
void ASubmarinePawn::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
    if (!OtherActor || !OtherActor->IsValidLowLevel() || OtherActor == this)
        return;

    UE_LOG(LogTemp, Warning, TEXT("OVERLAP! Other: %s"), *OtherActor->GetName());
    if (CollisionHandler)
        CollisionHandler->ProcessHit(SweepResult, OtherActor);
}