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

    //RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SubmarineBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SubmarineBody"));
    RootComponent = SubmarineBody;

    // POV camera — attached to root, inherits all rotation
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraPOV"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetRelativeLocation(CameraOffset);
    Camera->bUsePawnControlRotation = false;

    // Periscope camera — attached to root so it follows submarine movement/rotation,
    // but its yaw offset is applied manually each frame
    PeriscopeCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraPeriscope"));
    PeriscopeCamera->SetupAttachment(RootComponent);
    PeriscopeCamera->bUsePawnControlRotation = false;
    PeriscopeCamera->SetAutoActivate(false);

    // 3rd person camera — NOT attached to root, positioned in world space each frame
    ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraThirdPerson"));
    ThirdPersonCamera->SetupAttachment(RootComponent); // attachment overridden at runtime
    ThirdPersonCamera->bUsePawnControlRotation = false;
    ThirdPersonCamera->SetAutoActivate(false);

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

    const USubmarineCharacteristics* Stats = GetStats();

    SafeVerticalStateCount = Stats->GetSafeVerticalStateCount();
    VerticalStateIndex = SafeVerticalStateCount / 2;
    CurrentPitch = 0.f;
    TargetPitch = 0.f;

    // Detach 3rd person camera from root so it doesn't inherit submarine rotation
    ThirdPersonCamera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

    // Apply offsets from DA
    Camera->SetRelativeLocation(CameraOffset);
    PeriscopeCamera->SetRelativeLocation(Stats->PeriscopeCameraOffset);

    // Init 3rd person orbit state from DA
    ThirdPersonOrbitYaw = Stats->ThirdPersonInitialYaw;
    ThirdPersonOrbitPitch = Stats->ThirdPersonInitialPitch;
    ThirdPersonRadius = Stats->ThirdPersonInitialRadius;

    // Start in POV mode
    ActivateCamera(ESubmarineCameraState::POV);

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
    TickCameraSwitch(DeltaTime);

    // Update active camera positions
    if (CameraState == ESubmarineCameraState::Periscope)
        TickPeriscopeCamera();
    else if (CameraState == ESubmarineCameraState::ThirdPerson)
        TickThirdPersonCamera();
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
    {
        EIC->BindAction(IA_Turn, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnTurnTriggered);
        EIC->BindAction(IA_Turn, ETriggerEvent::Completed, this, &ASubmarinePawn::OnTurnCompleted);
    }

    if (IA_MouseX)
        EIC->BindAction(IA_MouseX, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnMouseX);

    if (IA_MouseY)
        EIC->BindAction(IA_MouseY, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnMouseY);

    if (IA_ScrollZoom)
        EIC->BindAction(IA_ScrollZoom, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnScrollZoom);

    if (IA_CameraPeriscope)
    {
        EIC->BindAction(IA_CameraPeriscope, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnCameraPeriscopeTriggered);
        EIC->BindAction(IA_CameraPeriscope, ETriggerEvent::Completed, this, &ASubmarinePawn::OnCameraPeriscopeCompleted);
    }
    if (IA_Camera3rdPerson)
    {
        EIC->BindAction(IA_Camera3rdPerson, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnCamera3rdPersonTriggered);
        EIC->BindAction(IA_Camera3rdPerson, ETriggerEvent::Completed, this, &ASubmarinePawn::OnCamera3rdPersonCompleted);
    }
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
        if (PitchSnapBlendTimer > 0.f)
        {
            // Still in blend window — smoothly move toward nearest state
            PitchSnapBlendTimer -= DeltaTime;
            const float StatePitch = GetPitchForState(FindNearestVerticalState(TargetPitch));
            const float BlendAlpha = FMath::Clamp(
                1.f - (PitchSnapBlendTimer / Stats->PitchSnapBlendDuration), 0.f, 1.f);
            TargetPitch = FMath::Lerp(TargetPitch, StatePitch, BlendAlpha * DeltaTime * 10.f);
        }
        else
        {
            // Blend done: snap to exact state pitch at normal snap speed
            const float StatePitch = GetPitchForState(VerticalStateIndex);
            TargetPitch = FMath::FInterpConstantTo(
                TargetPitch, StatePitch, DeltaTime, Stats->VerticalSnapSpeed);
        }
    }

    // External Pitch Target deviation (from collisions)
    TargetPitch += ExternalPitchVelocity * DeltaTime;
    TargetPitch = FMath::Clamp(TargetPitch, -Stats->MaxPitchAngle, Stats->MaxPitchAngle);
    ExternalPitchVelocity = FMath::FInterpTo(ExternalPitchVelocity, 0.f, DeltaTime, Stats->Deceleration_Rotation);

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

    // External Yaw Speed from collisions
    CurrentYawSpeed += ExternalYawVelocity * DeltaTime;
    ExternalYawVelocity = FMath::FInterpTo(ExternalYawVelocity, 0.f, DeltaTime, Stats->Deceleration_Rotation);

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
    const USubmarineCharacteristics* Stats = GetStats();

    // Linear movement along the submarine's forward axis (pitch is already baked in)
    const FVector LinearDelta = GetActorForwardVector() * (CurrentLinearSpeed + ExternalLinearVelocity) * DeltaTime;

    // Vertical movement is purely world Z
    const FVector VerticalDelta = FVector(0.f, 0.f, (CurrentVerticalSpeed + ExternalVerticalVelocity) * DeltaTime);

    // Decreasing External Linear Velocity (from collisions)
    ExternalLinearVelocity = FMath::FInterpTo(ExternalLinearVelocity, 0.f, DeltaTime, Stats->Deceleration_Linear);
    ExternalVerticalVelocity = FMath::FInterpTo(ExternalVerticalVelocity, 0.f, DeltaTime, Stats->Deceleration_Linear);

    FHitResult Hit;
    AddActorWorldOffset(LinearDelta + VerticalDelta, true, &Hit);

    if (Hit.IsValidBlockingHit())
    {
        UE_LOG(LogTemp, Warning, TEXT("HIT with: %s"), *Hit.GetActor()->GetName());

        if (CollisionHandler)
            CollisionHandler->ProcessHit(Hit, Hit.GetActor());
    }
}

// -----------------------------------------------------------------------------
//  Camera switching
// -----------------------------------------------------------------------------
void ASubmarinePawn::ActivateCamera(ESubmarineCameraState NewState)
{
    CameraState = NewState;
    Camera->SetActive(NewState == ESubmarineCameraState::POV);
    PeriscopeCamera->SetActive(NewState == ESubmarineCameraState::Periscope);
    ThirdPersonCamera->SetActive(NewState == ESubmarineCameraState::ThirdPerson);
}

void ASubmarinePawn::TickCameraSwitch(float DeltaTime)
{
    const USubmarineCharacteristics* Stats = GetStats();
    const float HoldDuration = Stats->CameraSwitchHoldDuration;

    // Periscope button (P): toggles between POV and Periscope only
    if (bPeriscopeHeld)
    {
        PeriscopeHoldTimer += DeltaTime;
        if (PeriscopeHoldTimer >= HoldDuration)
        {
            if (CameraState == ESubmarineCameraState::POV)
                ActivateCamera(ESubmarineCameraState::Periscope);
            else if (CameraState == ESubmarineCameraState::Periscope)
                ActivateCamera(ESubmarineCameraState::POV);
            bPeriscopeHeld = false;
            PeriscopeHoldTimer = 0.f;
        }
    }

    // 3rd person button (F1): toggles between POV and ThirdPerson only
    if (bThirdPersonHeld && Stats->bEnable3rdPersonCamera)
    {
        ThirdPersonHoldTimer += DeltaTime;
        if (ThirdPersonHoldTimer >= HoldDuration)
        {
            if (CameraState == ESubmarineCameraState::POV)
                ActivateCamera(ESubmarineCameraState::ThirdPerson);
            else if (CameraState == ESubmarineCameraState::ThirdPerson)
                ActivateCamera(ESubmarineCameraState::POV);
            bThirdPersonHeld = false;
            ThirdPersonHoldTimer = 0.f;
        }
    }
}

void ASubmarinePawn::TickPeriscopeCamera()
{
    // The periscope camera is attached to the root so it follows submarine
    // movement and pitch. We only override its yaw with the stored offset.
    const FRotator SubRot = GetActorRotation();
    const FRotator PeriRot = FRotator(SubRot.Pitch, SubRot.Yaw + PeriscopeYawOffset, SubRot.Roll);
    PeriscopeCamera->SetWorldRotation(PeriRot);
    PeriscopeCamera->SetRelativeLocation(GetStats()->PeriscopeCameraOffset);
}

void ASubmarinePawn::TickThirdPersonCamera()
{
    const USubmarineCharacteristics* Stats = GetStats();

    // Pivot = submarine world position + pivot offset (world-space)
    const FVector Pivot = GetActorLocation() + FVector(0.f, 0.f, Stats->ThirdPersonPivotOffset.Z);

    // Convert spherical coords to cartesian offset
    const float YawRad = FMath::DegreesToRadians(ThirdPersonOrbitYaw);
    const float PitchRad = FMath::DegreesToRadians(ThirdPersonOrbitPitch);

    const FVector Offset(
        ThirdPersonRadius * FMath::Cos(PitchRad) * FMath::Cos(YawRad),
        ThirdPersonRadius * FMath::Cos(PitchRad) * FMath::Sin(YawRad),
        ThirdPersonRadius * FMath::Sin(PitchRad)
    );

    const FVector CamPos = Pivot + Offset;
    ThirdPersonCamera->SetWorldLocation(CamPos);

    // Always look at the pivot
    const FRotator LookAt = (Pivot - CamPos).Rotation();
    ThirdPersonCamera->SetWorldRotation(LookAt);
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
    const float Axis = Value.Get<float>();

    if (!bVerticalHeld)
    {
        // First press — start fresh
        bVerticalHeld = true;
        VerticalHoldTime = 0.f;
        VerticalAxisValue = Axis;
        VerticalHeldDirection = Axis;
    }
    else if (FMath::Sign(Axis) != FMath::Sign(VerticalHeldDirection))
    {
        // Opposite direction pressed while held — ignore (first-held wins)
        // Do nothing: VerticalAxisValue stays as the original direction
        return;
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
    VerticalHeldDirection = 0.f;
    // FindNearestVerticalState will be called next tick during snap blend
    VerticalStateIndex = FindNearestVerticalState(TargetPitch);
}

void ASubmarinePawn::OnTurnTriggered(const FInputActionValue& Value)
{
    const float Axis = Value.Get<float>();

    if (TurnHeldDirection == 0.f)
    {
        // No turn currently held — accept this input
        TurnHeldDirection = FMath::Sign(Axis);
        CurrentYawSpeed = Axis * GetStats()->MaxYawSpeed;
    }
    else if (FMath::Sign(Axis) == FMath::Sign(TurnHeldDirection))
    {
        // Same direction — update normally
        CurrentYawSpeed = Axis * GetStats()->MaxYawSpeed;
    }
    // Opposite direction while held — ignored (first-held wins)
}

void ASubmarinePawn::OnTurnCompleted(const FInputActionValue& Value)
{
    TurnHeldDirection = 0.f;
    // CurrentYawSpeed decays to 0 naturally via FInterpTo in TickYawMovement
}

void ASubmarinePawn::OnMouseX(const FInputActionValue& Value)
{
    const float Axis = Value.Get<float>();
    const USubmarineCharacteristics* Stats = GetStats();

    if (CameraState == ESubmarineCameraState::Periscope)
    {
        PeriscopeYawOffset += Axis * Stats->PeriscopeYawSensitivity;
    }
    else if (CameraState == ESubmarineCameraState::ThirdPerson)
    {
        ThirdPersonOrbitYaw += Axis * Stats->ThirdPersonYawSensitivity;
    }
    // POV mode: mouse X does nothing (turning is done with keyboard)
}

void ASubmarinePawn::OnMouseY(const FInputActionValue& Value)
{
    const float Axis = Value.Get<float>();
    const USubmarineCharacteristics* Stats = GetStats();

    if (CameraState == ESubmarineCameraState::ThirdPerson)
    {
        ThirdPersonOrbitPitch = FMath::Clamp(
            ThirdPersonOrbitPitch + Axis * Stats->ThirdPersonPitchSensitivity,
            Stats->ThirdPersonMinPitch,
            Stats->ThirdPersonMaxPitch);
    }
    // POV and Periscope: mouse Y does nothing
}

void ASubmarinePawn::OnScrollZoom(const FInputActionValue& Value)
{
    if (CameraState != ESubmarineCameraState::ThirdPerson) return;

    const USubmarineCharacteristics* Stats = GetStats();
    ThirdPersonRadius = FMath::Clamp(
        ThirdPersonRadius - Value.Get<float>() * Stats->ThirdPersonScrollSpeed,
        Stats->ThirdPersonMinRadius,
        Stats->ThirdPersonMaxRadius);
}

void ASubmarinePawn::OnCameraPeriscopeTriggered(const FInputActionValue& Value)
{
    // Only allowed from POV or Periscope — not from 3rd person
    if (CameraState == ESubmarineCameraState::ThirdPerson) return;
    bPeriscopeHeld = true;
}

void ASubmarinePawn::OnCameraPeriscopeCompleted(const FInputActionValue& Value)
{
    bPeriscopeHeld = false;
    PeriscopeHoldTimer = 0.f;
}

void ASubmarinePawn::OnCamera3rdPersonTriggered(const FInputActionValue& Value)
{
    if (!GetStats()->bEnable3rdPersonCamera) return;
    // Only allowed from POV or ThirdPerson — not from Periscope
    if (CameraState == ESubmarineCameraState::Periscope) return;
    bThirdPersonHeld = true;
}

void ASubmarinePawn::OnCamera3rdPersonCompleted(const FInputActionValue& Value)
{
    bThirdPersonHeld = false;
    ThirdPersonHoldTimer = 0.f;
}

// -----------------------------------------------------------------------------
//  Collision Overlap
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