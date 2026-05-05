// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarinePawn.h"
#include "SubmarineCharacteristics.h"
#include "SubmarineCollisionComponent.h"
#include "SubmarinePhysicsComponent.h"
#include "TorpedoPawn.h"
#include "ScreenFadeComponent.h"
#include "CameraBlendSettings.h"
#include "Camera/CameraComponent.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/FloatingPawnMovement.h"

#include "Engine/EngineTypes.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"

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
    PhysicsHandler = CreateDefaultSubobject<USubmarinePhysicsComponent>(TEXT("PhysicsHandler"));
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
    PitchAngularMomentum = 0.f;
    PitchVelocity = 0.f;

    // Wire DA to physics component — MUST happen before physics component ticks
    if (PhysicsHandler)
        PhysicsHandler->Characteristics = Characteristics;

    // Lock roll so collisions never tilt the submarine sideways
    if (SubmarineBody)
    {
        SubmarineBody->SetConstraintMode(EDOFMode::SixDOF);
        SubmarineBody->BodyInstance.bLockXRotation = false;
        SubmarineBody->BodyInstance.bLockYRotation = false;
        SubmarineBody->BodyInstance.bLockZRotation = false;
        // We enforce roll=0 manually each tick instead
    }

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
        Prim->OnComponentEndOverlap.AddDynamic(this, &ASubmarinePawn::OnOverlapEnd);
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
}

// -----------------------------------------------------------------------------
//  Tick
// -----------------------------------------------------------------------------
void ASubmarinePawn::Tick(float DeltaTime)
{
    if (bDead) return;
    Super::Tick(DeltaTime);

    TickLinearMovement(DeltaTime);
    TickVerticalMovement(DeltaTime);
    TickYawMovement(DeltaTime);
    TickFinalMovement(DeltaTime);
    TickCameraSwitch(DeltaTime);
    //TickAntiStuck(DeltaTime);

    // Enforce zero roll every tick — collisions must never tilt the submarine sideways
    FRotator Rot = GetActorRotation();
    if (FMath::Abs(Rot.Roll) > KINDA_SMALL_NUMBER)
    {
        Rot.Roll = 0.f;
        SetActorRotation(Rot);
    }

    // Update active camera positions
    if (CameraState == ESubmarineCameraState::Periscope)
        TickPeriscopeCamera();
    else if (CameraState == ESubmarineCameraState::ThirdPerson)
        TickThirdPersonCamera();
}

// -----------------------------------------------------------------------------
//  FreezeOnDeath
// -----------------------------------------------------------------------------

void ASubmarinePawn::FreezeOnDeath()
{
    if (bDead) return;
    bDead = true;

    const USubmarineCharacteristics* Stats = GetStats();

    // Stop all movement immediately
    CurrentLinearSpeed = 0.f;
    CurrentVerticalSpeed = 0.f;
    CurrentYawSpeed = 0.f;
    ExternalLinearVelocity = 0.f;
    ExternalVerticalVelocity = 0.f;
    ExternalYawVelocity = 0.f;
    ExternalPitchVelocity = 0.f;

    if (PhysicsHandler)
    {
        PhysicsHandler->PhysicsVelocity = FVector::ZeroVector;
        PhysicsHandler->TargetLinearSpeed = 0.f;
        PhysicsHandler->TargetVerticalSpeed = 0.f;
    }

    // ------------------------------------------------------------------
    //  Hide from ALL players.
    //
    //  SetActorHiddenInGame alone does NOT reliably hide Blueprint-added
    //  child components in UE5 — they maintain independent visibility state.
    //  We explicitly call SetVisibility(false) and SetHiddenInGame(true)
    //  on every primitive, with bPropagateToChildren=true.
    // ------------------------------------------------------------------
    TArray<UPrimitiveComponent*> Prims;
    GetComponents<UPrimitiveComponent>(Prims);

    if (Stats->bLogFreezeOnDeath) {
        UE_LOG(LogTemp, Warning,
            TEXT("[FreezeOnDeath] '%s' has %d primitive components to hide:"),
            *GetName(), Prims.Num());
    }

    for (UPrimitiveComponent* Prim : Prims)
    {
        if (!Prim) continue;

        const bool bWasVisible = Prim->IsVisible();
        const bool bWasHiddenIG = Prim->bHiddenInGame;

        Prim->SetVisibility(false, /*bPropagateToChildren=*/true);
        Prim->SetHiddenInGame(true, /*bPropagateToChildren=*/true);
        Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        if (Stats->bLogFreezeOnDeath) {
            UE_LOG(LogTemp, Warning,
                TEXT("[FreezeOnDeath]   Prim='%s' WasVisible=%d WasHiddenIG=%d -> NowVisible=%d NowHiddenIG=%d"),
                *Prim->GetName(),
                bWasVisible ? 1 : 0,
                bWasHiddenIG ? 1 : 0,
                Prim->IsVisible() ? 1 : 0,
                Prim->bHiddenInGame ? 1 : 0);
        }
    }

    // Belt-and-suspenders: actor-level hidden flag
    SetActorHiddenInGame(true);

    if (Stats->bLogFreezeOnDeath) {
        UE_LOG(LogTemp, Warning,
            TEXT("[FreezeOnDeath] '%s' actor IsHidden=%d"),
            *GetName(), IsHidden() ? 1 : 0);
    }

    // ------------------------------------------------------------------
    //  Before unpossessing: cache the current active camera world
    //  transform so we can immediately restore it on the controller
    //  AFTER UnPossess(). Without this, UnPossess() resets the view
    //  target and the camera snaps to the controller default rotation.
    // ------------------------------------------------------------------
    FVector  CachedCamLoc = FVector::ZeroVector;
    FRotator CachedCamRot = FRotator::ZeroRotator;
    bool     bHasCachedCam = false;

    // Find the currently active camera component
    TArray<UCameraComponent*> AllCams;
    GetComponents<UCameraComponent>(AllCams);
    for (UCameraComponent* Cam : AllCams)
    {
        if (Cam && Cam->IsActive())
        {
            CachedCamLoc = Cam->GetComponentLocation();
            CachedCamRot = Cam->GetComponentRotation();
            bHasCachedCam = true;
            if (Stats->bLogFreezeOnDeath) {
                UE_LOG(LogTemp, Log,
                    TEXT("[FreezeOnDeath] Cached active cam '%s': Loc=%s Rot=%s"),
                    *Cam->GetName(), *CachedCamLoc.ToString(), *CachedCamRot.ToString());
            }
            break;
        }
    }

    // ------------------------------------------------------------------
    //  Detach controller — stops Enhanced Input and lets the dead player's
    //  camera be driven by DeathSequenceComponent::ApplyViewToController.
    // ------------------------------------------------------------------
    if (AController* C = GetController())
    {
        C->UnPossess();

        // Immediately re-apply the camera transform so the view does not pop.
        // DeathSequenceComponent::ApplyViewToController will take over from here.
        if (bHasCachedCam)
        {
            APlayerController* PC = Cast<APlayerController>(C);
            if (PC)
            {
                PC->SetInitialLocationAndRotation(CachedCamLoc, CachedCamRot);
                if (PC->PlayerCameraManager)
                    PC->PlayerCameraManager->SetActorLocationAndRotation(CachedCamLoc, CachedCamRot);
                if (Stats->bLogFreezeOnDeath) {
                    UE_LOG(LogTemp, Log,
                        TEXT("[FreezeOnDeath] Restored cam view on PC after UnPossess: Loc=%s Rot=%s"),
                        *CachedCamLoc.ToString(), *CachedCamRot.ToString());
                }
            }
        }
    }

    // Disable tick — no more movement, physics, or input processing
    SetActorTickEnabled(false);
}

// -----------------------------------------------------------------------------
//  Blueprint collision forwarding — thin wrappers into CollisionHandler
//  Same UFUNCTION names preserved so existing Blueprint graphs keep working.
// -----------------------------------------------------------------------------

void ASubmarinePawn::RegisterHitFromBlueprint(const FHitResult& Hit)
{
    if (CollisionHandler)
        CollisionHandler->RegisterHitFromBlueprint(Hit);
}

void ASubmarinePawn::HandleHitFromBlueprint(const FHitResult& Hit)
{
    if (CollisionHandler)
        CollisionHandler->HandleHitFromBlueprint(Hit);
}

// -----------------------------------------------------------------------------
//  Overlap callbacks (must stay on pawn — delegate binding requires UFUNCTION)
// -----------------------------------------------------------------------------

void ASubmarinePawn::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
    if (!OtherActor || !OtherActor->IsValidLowLevel() || OtherActor == this) return;
    if (CollisionHandler)
    {
        CollisionHandler->RegisterHit(OtherActor, SweepResult);
        CollisionHandler->ProcessHit(SweepResult, OtherActor);
    }
}

void ASubmarinePawn::OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    // Cleanup handled by anti-stuck ThresholdOut inside CollisionHandler
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

    if (IA_MoveUp_Positive)
    {
        EIC->BindAction(IA_MoveUp_Positive, ETriggerEvent::Started, this, &ASubmarinePawn::OnMoveUpPressed);
        EIC->BindAction(IA_MoveUp_Positive, ETriggerEvent::Completed, this, &ASubmarinePawn::OnMoveUpReleased);
    }

    if (IA_MoveUp_Negative)
    {
        EIC->BindAction(IA_MoveUp_Negative, ETriggerEvent::Started, this, &ASubmarinePawn::OnMoveDownPressed);
        EIC->BindAction(IA_MoveUp_Negative, ETriggerEvent::Completed, this, &ASubmarinePawn::OnMoveDownReleased);
    }
    if (IA_Turn_Positive)
    {
        EIC->BindAction(IA_Turn_Positive, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnTurnRightPressed);
        EIC->BindAction(IA_Turn_Positive, ETriggerEvent::Completed, this, &ASubmarinePawn::OnTurnRightReleased);
    }
    if (IA_Turn_Negative)
    {
        EIC->BindAction(IA_Turn_Negative, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnTurnLeftPressed);
        EIC->BindAction(IA_Turn_Negative, ETriggerEvent::Completed, this, &ASubmarinePawn::OnTurnLeftReleased);
    }

    if (IA_MouseX)
        EIC->BindAction(IA_MouseX, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnMouseX);

    if (IA_MouseY)
        EIC->BindAction(IA_MouseY, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnMouseY);

    if (IA_ScrollZoom)
        EIC->BindAction(IA_ScrollZoom, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnScrollZoom);

    if (IA_CameraPeriscope)
    {
        EIC->BindAction(IA_CameraPeriscope, ETriggerEvent::Started, this, &ASubmarinePawn::OnCameraPeriscopeStarted);
        EIC->BindAction(IA_CameraPeriscope, ETriggerEvent::Triggered, this, &ASubmarinePawn::OnCameraPeriscopeTriggered);
        EIC->BindAction(IA_CameraPeriscope, ETriggerEvent::Completed, this, &ASubmarinePawn::OnCameraPeriscopeCompleted);
    }
    if (IA_Camera3rdPerson)
    {
        EIC->BindAction(IA_Camera3rdPerson, ETriggerEvent::Started, this, &ASubmarinePawn::OnCamera3rdPersonStarted);
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

    // Feed target to physics component
    if (PhysicsHandler)
        PhysicsHandler->TargetLinearSpeed = TargetSpeed;

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
        const float PitchDelta = VerticalAxisValue * AngularRate * DeltaTime;
        TargetPitch += PitchDelta;
        TargetPitch = FMath::Clamp(TargetPitch, -Stats->MaxPitchAngle, Stats->MaxPitchAngle);

        // Accumulate angular momentum (degrees/s)
        PitchAngularMomentum = FMath::FInterpTo(
            PitchAngularMomentum, AngularRate * VerticalAxisValue, DeltaTime, 8.f);

        // Keep the vertical state index in sync with the current free pitch
        VerticalStateIndex = FindNearestVerticalState(TargetPitch);

        PitchSnapBlendTimer = Stats->PitchSnapBlendDuration;
    }
    else
    {
        // -- Momentum-based snap --------------------------------------------
        const int32 NearestState = FindNearestVerticalState(TargetPitch);
        const float NearestPitch = GetPitchForState(NearestState);
        const float DistToNearest = FMath::Abs(NearestPitch - TargetPitch);
        const float MomentumMag = FMath::Abs(PitchAngularMomentum);

        if (PitchSnapBlendTimer > 0.f)
        {
            PitchSnapBlendTimer -= DeltaTime;

            // Momentum coefficient: high momentum = carry further, low = snap sooner
            // Proximity coefficient: near a state = reduce momentum influence
            const float MomentumCoeff = FMath::Clamp(
                MomentumMag / FMath::Max(Stats->VerticalHoldAccelRate, 1.f), 0.f, 1.f)
                * Stats->PitchMomentumInfluence;
            const float ProximityCoeff = FMath::Clamp(
                DistToNearest / FMath::Max(Stats->MaxPitchAngle * 0.1f, 1.f), 0.f, 1.f);
            const float SnapCoeff = 1.f - (MomentumCoeff * ProximityCoeff);

            // Blend: momentum carries pitch, snap pulls toward nearest state
            const float MomentumContrib = PitchAngularMomentum * DeltaTime;
            const float SnapContrib = (NearestPitch - TargetPitch) * SnapCoeff * DeltaTime
                * (1.f / FMath::Max(Stats->PitchSnapBlendDuration, 0.001f));

            TargetPitch += MomentumContrib + SnapContrib;
            TargetPitch = FMath::Clamp(TargetPitch, -Stats->MaxPitchAngle, Stats->MaxPitchAngle);

            // Drain momentum over blend duration
            PitchAngularMomentum = FMath::FInterpConstantTo(
                PitchAngularMomentum, 0.f, DeltaTime,
                MomentumMag / FMath::Max(Stats->PitchSnapBlendDuration, 0.001f));
        }
        else
        {
            // Blend done — hard snap to state at normal speed
            PitchAngularMomentum = 0.f;
            VerticalStateIndex = NearestState;
            TargetPitch = FMath::FInterpConstantTo(
                TargetPitch, NearestPitch, DeltaTime, Stats->VerticalSnapSpeed);
        }
    }

    // External Pitch Target deviation (from collisions)
    TargetPitch += ExternalPitchVelocity * DeltaTime;
    TargetPitch = FMath::Clamp(TargetPitch, -Stats->MaxPitchAngle, Stats->MaxPitchAngle);
    ExternalPitchVelocity = FMath::FInterpTo(ExternalPitchVelocity, 0.f, DeltaTime, Stats->Deceleration_Rotation);

    // -- Accel/decel pitch toward TargetPitch -----------------------------
    float Delta = TargetPitch - CurrentPitch;
    const float StopThreshold = 0.1f;

    if (FMath::Abs(Delta) < StopThreshold && FMath::Abs(PitchVelocity) < 1.f)
    {
        CurrentPitch = TargetPitch;
        PitchVelocity = 0.f;
    }
    else {
        float Direction = FMath::Sign(Delta);

        float StoppingDistance = (PitchVelocity * PitchVelocity) / (2.f * FMath::Max(Stats->VerticalDeceleration, 0.001f));
        float AppliedAcceleration = 0.0f;

        if (FMath::Abs(Delta) > StoppingDistance)
        {
            // Acceleration
            PitchVelocity += Direction * Stats->VerticalAcceleration * DeltaTime;
            AppliedAcceleration = Stats->VerticalAcceleration;
        }
        else
        {
            // Deceleration
            float NewVelocity = PitchVelocity - Direction * Stats->VerticalDeceleration * DeltaTime;

            // Changing sign -> We stop
            if (FMath::Sign(NewVelocity) != FMath::Sign(PitchVelocity))
            {
                PitchVelocity = 0.f;
            }
            else
            {
                PitchVelocity = NewVelocity;
            }
            AppliedAcceleration = Stats->VerticalDeceleration;
        }

        // Prevents going further than the Target (prevents oscillations)
        if (FMath::Sign(PitchVelocity) != Direction)
        {
            PitchVelocity = 0.f;
        }

        // Apply movement
        //CurrentPitch += PitchVelocity * DeltaTime;
        CurrentPitch = FMath::FInterpConstantTo( CurrentPitch, TargetPitch, DeltaTime, AppliedAcceleration);
    }

    // Apply pitch via swept rotation so pitching into walls is detected
    const FRotator CurrentRot = GetActorRotation();
    const FRotator TargetRot = FRotator(CurrentPitch, CurrentRot.Yaw, 0.f);
    const FRotator DeltaRot = TargetRot - CurrentRot;

    if (!DeltaRot.IsNearlyZero(KINDA_SMALL_NUMBER))
    {
        FHitResult RotHit;
        AddActorLocalRotation(FRotator(DeltaRot.Pitch, 0.f, 0.f), true, &RotHit);

        if (RotHit.IsValidBlockingHit() && RotHit.GetActor()
            && Stats->bEnableBluePrintCollisions && CollisionHandler)
        {
            CollisionHandler->RegisterHitFromBlueprint(RotHit);
            CollisionHandler->HandleHitFromBlueprint(RotHit);
            PitchVelocity = 0.f;
            CurrentPitch = GetActorRotation().Pitch;
        }

        // Fallback: catches contacts missed by the sweep (pure in-place pitch)
        if (CollisionHandler)
            CollisionHandler->CheckRotationContactBP(1);

        // Always enforce zero roll after any rotation
        FRotator AfterRot = GetActorRotation();
        if (FMath::Abs(AfterRot.Roll) > KINDA_SMALL_NUMBER)
        {
            AfterRot.Roll = 0.f;
            SetActorRotation(AfterRot);
        }
    }

    // Convert pitch to vertical speed
    const float PitchRatio = CurrentPitch / FMath::Max(Stats->MaxPitchAngle, 1.f);
    float MoveSign = (FMath::Abs(CurrentLinearSpeed) > 1.f)
        ? FMath::Sign(CurrentLinearSpeed)
        : 1.f;
    float VerticalSpeed = PitchRatio * Stats->MaxVerticalSpeed;

    // Cross-boost: submarine is linearly still -> vertical gets a boost
    if (LinearSpeedState == ELinearSpeedState::Stand)
        VerticalSpeed *= (1.f + Stats->VerticalBoostWhenLinearStand);

    CurrentVerticalSpeed = VerticalSpeed;

    if (PhysicsHandler)
    {
        const float PitchBlend = FMath::Abs(PitchRatio); // 0 at flat, 1 at max pitch

        if (PitchBlend < 0.01f)
        {
            // Pitch is essentially 0 — drain Z velocity toward 0 quickly
            // so buoyancy/gravity reach equilibrium without residual movement
            PhysicsHandler->PhysicsVelocity.Z = FMath::FInterpConstantTo(
                PhysicsHandler->PhysicsVelocity.Z, 0.f,
                GetWorld()->GetDeltaSeconds(), Stats->VerticalDeceleration);
        }
        else
        {
            // Actively pitching — override Z velocity with input intent
            PhysicsHandler->PhysicsVelocity.Z = FMath::Lerp(
                PhysicsHandler->PhysicsVelocity.Z,
                VerticalSpeed,
                PitchBlend);
        }

        // Clear target so thrust doesn't fight us
        PhysicsHandler->TargetVerticalSpeed = 0.f;
    }


}

// -----------------------------------------------------------------------------
//  Yaw movement tick
// -----------------------------------------------------------------------------
void ASubmarinePawn::TickYawMovement(float DeltaTime)
{
    const USubmarineCharacteristics* Stats = GetStats();

    // Target yaw speed: MaxYawSpeed in held direction, 0 when released
    const float TargetYaw = (TurnHeldDirection != 0.f)
        ? TurnHeldDirection * Stats->MaxYawSpeed
        : 0.f;

    // Use acceleration when speeding up, deceleration when slowing down
    const float Rate = (FMath::Abs(TargetYaw) > FMath::Abs(CurrentYawSpeed))
        ? Stats->YawAcceleration
        : Stats->YawDeceleration;

    CurrentYawSpeed = FMath::FInterpConstantTo(CurrentYawSpeed, TargetYaw, DeltaTime, Rate);

    // External Yaw Speed from collisions
    CurrentYawSpeed += ExternalYawVelocity * DeltaTime;
    ExternalYawVelocity = FMath::FInterpTo(ExternalYawVelocity, 0.f, DeltaTime, Stats->Deceleration_Rotation);

    const float YawDelta = CurrentYawSpeed * DeltaTime;
    if (FMath::Abs(YawDelta) > KINDA_SMALL_NUMBER)
    {
        FHitResult RotHit;
        AddActorWorldRotation(FRotator(0.f, YawDelta, 0.f), true, &RotHit);

        // Sweep-based hit (handles contacts caused by translational displacement)
        if (RotHit.IsValidBlockingHit() && RotHit.GetActor()
            && Stats->bEnableBluePrintCollisions && CollisionHandler)
        {
            CollisionHandler->RegisterHitFromBlueprint(RotHit);
            CollisionHandler->HandleHitFromBlueprint(RotHit);
        }

        // Fallback: catches contacts missed by the sweep (pure in-place rotation)
        if (CollisionHandler)
            CollisionHandler->CheckRotationContactBP(0);
    }
}

// -----------------------------------------------------------------------------
//  Final movement application
// -----------------------------------------------------------------------------
void ASubmarinePawn::TickFinalMovement(float DeltaTime)
{
    const USubmarineCharacteristics* Stats = GetStats();

    FVector MoveDelta = FVector::ZeroVector;

    if (PhysicsHandler)
    {
        const float TotalLinearSpeed = CurrentLinearSpeed + ExternalLinearVelocity;

        // When going backward, the effective travel direction is opposite to
        // the actor's forward vector. We flip the vector so that nose-down +
        // backward still moves the sub backward-and-down (not backward-and-up).
        const FVector TravelDirection = (TotalLinearSpeed >= 0.f)
            ? GetActorForwardVector()
            : -GetActorForwardVector();

        FVector LinearDelta = TravelDirection * FMath::Abs(TotalLinearSpeed) * DeltaTime;

        // Attenuate the vertical bleed from linear movement.
        // LinearInfluenceVertically = 1.0 -> full Z contribution from hull pitch.
        // LinearInfluenceVertically = 0.0 -> linear thrust is purely horizontal.
        LinearDelta.Z *= Stats->LinearInfluenceVertically;

        const float VerticalDelta =
            (PhysicsHandler->PhysicsVelocity.Z + ExternalVerticalVelocity) * DeltaTime;

        MoveDelta = LinearDelta + FVector(0.f, 0.f, VerticalDelta);

        // Debug every 2s
        static float DbgTimer = 0.f;
        DbgTimer += DeltaTime;
        if (DbgTimer >= 2.f && Stats->bDebugMovementLogs)
        {
            DbgTimer = 0.f;
            UE_LOG(LogTemp, Warning,
                TEXT("[Move] SubZ=%.0f WaterZ=%.0f Depth=%.0f | PhysVelZ=%.1f | TargetVSpd=%.1f | InputVSpd=%.1f | LinearSpd=%.1f | Above=%d"),
                GetActorLocation().Z,
                Stats->WaterSurfaceZ,
                PhysicsHandler->CurrentDepth,
                PhysicsHandler->PhysicsVelocity.Z,
                PhysicsHandler->TargetVerticalSpeed,
                CurrentVerticalSpeed,
                CurrentLinearSpeed,
                PhysicsHandler->bAboveSurface ? 1 : 0);
        }
    }
    else
    {
        // Fallback: no physics component
        const float TotalLinearSpeed = CurrentLinearSpeed + ExternalLinearVelocity;
        const FVector TravelDirection = (TotalLinearSpeed >= 0.f)
            ? GetActorForwardVector()
            : -GetActorForwardVector();

        FVector LinearDelta = TravelDirection * FMath::Abs(TotalLinearSpeed) * DeltaTime;
        LinearDelta.Z *= Stats->LinearInfluenceVertically;

        MoveDelta = LinearDelta +
            FVector(0.f, 0.f, (CurrentVerticalSpeed + ExternalVerticalVelocity) * DeltaTime);
    }

    ExternalLinearVelocity = FMath::FInterpTo(ExternalLinearVelocity, 0.f, DeltaTime, Stats->Deceleration_Linear);
    ExternalVerticalVelocity = FMath::FInterpTo(ExternalVerticalVelocity, 0.f, DeltaTime, Stats->Deceleration_Linear);

    FHitResult Hit;
    AddActorWorldOffset(MoveDelta, true, &Hit);

    if (Hit.IsValidBlockingHit() && Hit.GetActor() && !Stats->bEnableBluePrintCollisions)
    {
        // Register for anti-stuck tracking
        if (CollisionHandler)
            CollisionHandler->RegisterHit(Hit.GetActor(), Hit);

        if (CollisionHandler)
            CollisionHandler->ProcessHit(Hit, Hit.GetActor());

        // -- Push hit actor if it's another submarine ------------------------
        ASubmarinePawn* HitSub = Cast<ASubmarinePawn>(Hit.GetActor());
        if (HitSub)
        {
            // Transfer a fraction of our momentum to them
            const FVector ImpactDir = Hit.ImpactNormal * -1.f; // toward the hit sub
            const float   TransferMag = FMath::Abs(CurrentLinearSpeed) * 0.4f;
            const FVector LocalImpact =
                HitSub->GetActorTransform().InverseTransformVectorNoScale(ImpactDir * TransferMag);

            HitSub->SetExternalLinearVelocity(
                HitSub->GetExternalLinearVelocity() + LocalImpact.X);
            HitSub->SetExternalVerticalVelocity(
                HitSub->GetExternalVerticalVelocity() + LocalImpact.Z);
            HitSub->SetExternalYawVelocity(
                HitSub->GetExternalYawVelocity() + LocalImpact.Y * 0.3f);

            // Also notify their collision handler for health/state effects
            if (HitSub->CollisionHandler)
                HitSub->CollisionHandler->ProcessHit(Hit, this);
        }
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
    // Camera Switch for holding the button
    const USubmarineCharacteristics* Stats = GetStats();
    const float HoldDuration = Stats->CameraSwitchHoldDuration;

    // Periscope button (P): toggles between POV and Periscope only
    if (bPeriscopeHeld)
    {
        PeriscopeHoldTimer += DeltaTime;
        if (PeriscopeHoldTimer >= HoldDuration && !bPeriscopeHoldFired)
        {
            if (CameraState == ESubmarineCameraState::POV)
                ActivateCamera(ESubmarineCameraState::Periscope);
            else if (CameraState == ESubmarineCameraState::Periscope)
                ActivateCamera(ESubmarineCameraState::POV);
            //PeriscopeHoldTimer = 0.f;
            //bPeriscopeHoldFired = true;
            PeriscopeHoldTimer -= HoldDuration;
        }
    }

    // 3rd person button (F1): toggles between POV and ThirdPerson only
    if (bThirdPersonHeld && Stats->bEnable3rdPersonCamera)
    {
        ThirdPersonHoldTimer += DeltaTime;
        if (ThirdPersonHoldTimer >= HoldDuration && !bThirdPersonHoldFired)
        {
            if (CameraState == ESubmarineCameraState::POV)
                ActivateCamera(ESubmarineCameraState::ThirdPerson);
            else if (CameraState == ESubmarineCameraState::ThirdPerson)
                ActivateCamera(ESubmarineCameraState::POV);
            //ThirdPersonHoldTimer = 0.f;
            //bThirdPersonHoldFired = true;
            ThirdPersonHoldTimer -= HoldDuration;
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
    const ELinearSpeedState OldState = LinearSpeedState;
    LinearSpeedState = static_cast<ELinearSpeedState>(Next);
    /*if (LinearSpeedState != OldState) {
        OnLinearStateChanged.Broadcast(LinearSpeedState);
    }*/
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
    const USubmarineCharacteristics* Stats = GetStats();
    int32 BestIndex = SafeVerticalStateCount / 2;
    float BestDistance = FLT_MAX;

    for (int32 i = 0; i < SafeVerticalStateCount; ++i)
    {
        // Skip ghost states — they are never snapped to
        if (Stats && Stats->IsGhostState(i)) continue;

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

void ASubmarinePawn::UpdateVerticalInput()
{
    if (VerticalHeldDirection == 1.f && !bUpPressed)
    {
        if (bDownPressed)
        {
            VerticalHeldDirection = -1.f;
            VerticalHoldTime = 0.f;
        }
        else
        {
            VerticalHeldDirection = 0.f;
            bVerticalHeld = false;
        }
    }
    else if (VerticalHeldDirection == -1.f && !bDownPressed)
    {
        if (bUpPressed)
        {
            VerticalHeldDirection = 1.f;
            VerticalHoldTime = 0.f;
        }
        else
        {
            VerticalHeldDirection = 0.f;
            bVerticalHeld = false;
        }
    }
    else if (VerticalHeldDirection == 0.f)
    {
        if (bUpPressed)
        {
            VerticalHeldDirection = 1.f;
            bVerticalHeld = true;
            VerticalHoldTime = 0.f;
        }
        else if (bDownPressed)
        {
            VerticalHeldDirection = -1.f;
            bVerticalHeld = true;
            VerticalHoldTime = 0.f;
        }
    }

    VerticalAxisValue = VerticalHeldDirection;
}

void ASubmarinePawn::OnMoveUpPressed(const FInputActionValue&)
{
    bUpPressed = true;
    UpdateVerticalInput();
}

void ASubmarinePawn::OnMoveDownPressed(const FInputActionValue&)
{
    bDownPressed = true;
    UpdateVerticalInput();
}

void ASubmarinePawn::OnMoveUpReleased(const FInputActionValue&)
{
    bUpPressed = false;
    UpdateVerticalInput();
}

void ASubmarinePawn::OnMoveDownReleased(const FInputActionValue&)
{
    bDownPressed = false;
    UpdateVerticalInput();
}

void ASubmarinePawn::UpdateTurnInput()
{
    if (TurnHeldDirection == 1.f && !bRightPressed)
    {
        if (bLeftPressed)
        {
            TurnHeldDirection = -1.f;
        }
        else
        {
            TurnHeldDirection = 0.f;
        }
    }
    else if (TurnHeldDirection == -1.f && !bLeftPressed)
    {
        if (bRightPressed)
        {
            TurnHeldDirection = 1.f;
        }
        else
        {
            TurnHeldDirection = 0.f;
        }
    }
    else if (TurnHeldDirection == 0.f)
    {
        if (bRightPressed)
        {
            TurnHeldDirection = 1.f;
        }
        else if (bLeftPressed)
        {
            TurnHeldDirection = -1.f;
        }
    }
}

void ASubmarinePawn::OnTurnRightPressed(const FInputActionValue&)
{
    bRightPressed = true;
    UpdateTurnInput();
}

void ASubmarinePawn::OnTurnLeftPressed(const FInputActionValue&)
{
    bLeftPressed = true;
    UpdateTurnInput();
}

void ASubmarinePawn::OnTurnRightReleased(const FInputActionValue&)
{
    bRightPressed = false;
    UpdateTurnInput();
}

void ASubmarinePawn::OnTurnLeftReleased(const FInputActionValue&)
{
    bLeftPressed = false;
    UpdateTurnInput();
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

// Camera periscope — tap on Started, hold fires in Tick
void ASubmarinePawn::OnCameraPeriscopeStarted(const FInputActionValue& Value)
{
    if (CameraState == ESubmarineCameraState::ThirdPerson) return;
    // Tap: switch immediately
    if (CameraState == ESubmarineCameraState::POV)
        ActivateCamera(ESubmarineCameraState::Periscope);
    else if (CameraState == ESubmarineCameraState::Periscope)
        ActivateCamera(ESubmarineCameraState::POV);

    // Begin tracking hold
    bPeriscopeHeld = true;
    PeriscopeHoldTimer = 0.f;
    bPeriscopeHoldFired = false;
}

void ASubmarinePawn::OnCameraPeriscopeTriggered(const FInputActionValue& Value)
{
    // Hold handled in TickCameraSwitch
}

void ASubmarinePawn::OnCameraPeriscopeCompleted(const FInputActionValue& Value)
{
    bPeriscopeHeld = false;
    PeriscopeHoldTimer = 0.f;
    bPeriscopeHoldFired = false;
}

void ASubmarinePawn::OnCamera3rdPersonStarted(const FInputActionValue& Value)
{
    if (!GetStats()->bEnable3rdPersonCamera) return;
    if (CameraState == ESubmarineCameraState::Periscope) return;
    // Tap: switch immediately
    if (CameraState == ESubmarineCameraState::POV)
        ActivateCamera(ESubmarineCameraState::ThirdPerson);
    else if (CameraState == ESubmarineCameraState::ThirdPerson)
        ActivateCamera(ESubmarineCameraState::POV);

    bThirdPersonHeld = true;
    ThirdPersonHoldTimer = 0.f;
    bThirdPersonHoldFired = false;
}

void ASubmarinePawn::OnCamera3rdPersonTriggered(const FInputActionValue& Value)
{
    // Hold handled in TickCameraSwitch
}

void ASubmarinePawn::OnCamera3rdPersonCompleted(const FInputActionValue& Value)
{
    bThirdPersonHeld = false;
    ThirdPersonHoldTimer = 0.f;
    bThirdPersonHoldFired = false;
}


// ============================================================================
//  ITrackableSubmarine  --  interface implementation
//  All methods delegate to existing components. No new state introduced.
// ============================================================================

// -- Health ------------------------------------------------------------------

float ASubmarinePawn::GetHealthRatio() const
{
    if (!CollisionHandler) return 0.f;
    return CollisionHandler->GetHealthRatio();
}

// -- Movement ----------------------------------------------------------------

float ASubmarinePawn::GetCurrentSpeed() const
{
    return CurrentLinearSpeed;
}

float ASubmarinePawn::GetCurrentDepth() const
{
    if (!PhysicsHandler) return 0.f;
    return PhysicsHandler->CurrentDepth;
}

float ASubmarinePawn::GetCurrentPitch() const
{
    return CurrentPitch;
}

int32 ASubmarinePawn::GetVerticalStateIndex() const
{
    return VerticalStateIndex;
}

int32 ASubmarinePawn::GetVerticalStateCount() const
{
    return SafeVerticalStateCount;
}

ELinearSpeedState ASubmarinePawn::GetLinearSpeedState() const
{
    return LinearSpeedState;
}

// -- Torpedo / Ammo ----------------------------------------------------------

int32 ASubmarinePawn::GetNormalAmmoCount() const
{
    if (!TorpedoHandler) return 0;
    return TorpedoHandler->CurrentNormalTorpedoes;
}

int32 ASubmarinePawn::GetNormalAmmoCapacity() const
{
    if (!TorpedoHandler) return 0;
    return TorpedoHandler->NormalTorpedoCapacity;
}

int32 ASubmarinePawn::GetSpecialAmmoCount() const
{
    if (!TorpedoHandler) return 0;
    return TorpedoHandler->CurrentSpecialTorpedoes;
}

int32 ASubmarinePawn::GetSpecialAmmoCapacity() const
{
    if (!TorpedoHandler) return 0;
    return TorpedoHandler->SpecialTorpedoCapacity;
}

float ASubmarinePawn::GetFireCooldownRatio() const
{
    if (!TorpedoHandler) return 1.f;
    return TorpedoHandler->GetFireCooldownRatio();
}

float ASubmarinePawn::GetReloadRatio() const
{
    if (!TorpedoHandler) return 1.f;
    return TorpedoHandler->GetReloadRatio();
}

bool ASubmarinePawn::GetIsReloading() const
{
    if (!TorpedoHandler) return false;
    // ReloadTimeRemaining > 0 reliably indicates an active reload in both modes
    return TorpedoHandler->ReloadTimeRemaining > 0.f;
}

// -- Identity ----------------------------------------------------------------

FText ASubmarinePawn::GetDisplayName() const
{
    // Returns the actor label by default.
    // Override or extend this when the match/spawn system is implemented (Phase 5).
    return FText::FromString(GetActorLabel());
}

// -- Radar placeholder -------------------------------------------------------

const TArray<FDetectedEntry>& ASubmarinePawn::GetDetectionEntries() const
{
    // Empty until RadarComponent is implemented in Phase 4.
    static const TArray<FDetectedEntry> EmptyEntries;
    return EmptyEntries;
}

// -- Delegate getters --------------------------------------------------------
//
//  Return references to the actual delegates on the components.
//  UI modules bind directly to these -- no copies, no indirection.
//
//  check() is intentional: these components are created in the constructor
//  and must always exist for the lifetime of the pawn.
//  If they are null here, something is seriously wrong with the setup.
//
//  CALLER RESPONSIBILITY:
//    Always validate the UObject before binding:
//      UObject* Obj = DataSource.GetObject();
//      if (!IsValid(Obj)) return;
//      DataSource->GetOnDamagedDelegate().AddDynamic(...);

FOnSubmarineDamaged& ASubmarinePawn::GetOnDamagedDelegate()
{
    check(CollisionHandler);
    return CollisionHandler->OnDamaged;
}

FOnAmmoChanged& ASubmarinePawn::GetOnAmmoChangedDelegate()
{
    check(TorpedoHandler);
    return TorpedoHandler->OnAmmoChanged;
}

FOnTorpedoFired& ASubmarinePawn::GetOnTorpedoFiredDelegate()
{
    check(TorpedoHandler);
    return TorpedoHandler->OnTorpedoFired;
}

FOnReadyToFire& ASubmarinePawn::GetOnReadyToFireDelegate()
{
    check(TorpedoHandler);
    return TorpedoHandler->OnReadyToFire;
}

FOnFireCooldownComplete& ASubmarinePawn::GetOnFireCooldownDelegate()
{
    check(TorpedoHandler);
    return TorpedoHandler->OnFireCooldownComplete;
}

FOnLinearStateChanged& ASubmarinePawn::GetOnLinearStateChangedDelegate()
{
    check(Characteristics);
    return Characteristics->OnLinearStateChanged;
}