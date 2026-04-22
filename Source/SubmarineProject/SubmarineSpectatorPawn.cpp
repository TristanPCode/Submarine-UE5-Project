// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineSpectatorPawn.h"
#include "SubmarinePawn.h"
#include "SubmarineCharacteristics.h"
#include "SubmarineTorpedoComponent.h"
#include "TorpedoPawn.h"
#include "CameraBlendSettings.h"
#include "Camera/CameraComponent.h"
#include "Math/UnrealMathUtility.h"

ASubmarineSpectatorPawn::ASubmarineSpectatorPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    // Invisible root
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SpectatorRoot"));

    SpectatorCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("SpectatorCamera"));
    SpectatorCamera->SetupAttachment(RootComponent);
    SpectatorCamera->bUsePawnControlRotation = false;
    SpectatorCamera->SetActive(true);
}

// -----------------------------------------------------------------------------
//  BeginPlay
// -----------------------------------------------------------------------------
void ASubmarineSpectatorPawn::BeginPlay()
{
    Super::BeginPlay();

    // Store world origin as fallback location
    FallbackLocation = FVector::ZeroVector;
}

// -----------------------------------------------------------------------------
//  InitSpectator
// -----------------------------------------------------------------------------
void ASubmarineSpectatorPawn::InitSpectator(const TArray<ASubmarinePawn*>& AllSubmarines,
    bool bStartOnFirstLiveSubmarine)
{
    TrackedSubmarines.Empty();
    for (ASubmarinePawn* Sub : AllSubmarines)
    {
        if (IsValid(Sub))
            TrackedSubmarines.Add(Sub);
    }

    SubmarineListIndex = 0;
    ViewListIndex = 0;

    TArray<ASubmarinePawn*> Live = GetLiveSubmarines();

    if (Live.Num() == 0)
    {
        // No live submarines — place at fallback
        CurrentSubTarget = nullptr;
        CurrentTorpedoTarget = nullptr;
        bWatchingTorpedo = false;

        SpectatorCamera->SetWorldLocation(FallbackLocation);
        bCameraInitialised = true;
        return;
    }
    // Default: start in 3rd person so the spectator immediately has a good view
    bSpectatorThirdPerson = true;

    if (bStartOnFirstLiveSubmarine)
        SubmarineListIndex = 0;

    ApplyCurrentTarget();

    // Sync desired orbit values so we don't start blending from defaults
    DesiredOrbitYaw = SpectatorOrbitYaw;
    DesiredOrbitPitch = SpectatorOrbitPitch;
    DesiredOrbitRadius = SpectatorOrbitRadius;
}

// -----------------------------------------------------------------------------
//  RegisterSubmarine
// -----------------------------------------------------------------------------
void ASubmarineSpectatorPawn::RegisterSubmarine(ASubmarinePawn* Submarine)
{
    if (IsValid(Submarine) && !TrackedSubmarines.Contains(Submarine))
        TrackedSubmarines.Add(Submarine);
}

// -----------------------------------------------------------------------------
//  Tick
// -----------------------------------------------------------------------------
void ASubmarineSpectatorPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // -- Hold-key auto-repeat -----------------------------------------------
    TickHoldInput(HoldLeft, DeltaTime, SwitchHoldThreshold, SwitchHoldCooldown,
        [this] { SpectatorLeft();  });
    TickHoldInput(HoldRight, DeltaTime, SwitchHoldThreshold, SwitchHoldCooldown,
        [this] { SpectatorRight(); });
    TickHoldInput(HoldUp, DeltaTime, SwitchHoldThreshold, SwitchHoldCooldown,
        [this] { SpectatorUp();    });
    TickHoldInput(HoldDown, DeltaTime, SwitchHoldThreshold, SwitchHoldCooldown,
        [this] { SpectatorDown();  });
    TickHoldInput(HoldCameraToggle, DeltaTime,
        CameraToggleHoldThreshold, CameraToggleHoldCooldown,
        [this] { DoToggleCameraMode(); });

    // -- Torpedo validity --------------------------------------------------
    ValidateTorpedoTarget();

    // -- Orbit smooth lag --------------------------------------------------
    // Only the Desired* vars are driven by input.
    // SpectatorOrbit* lerp toward them when orbit blend is on,
    // or snap instantly when off.
    if (IsOrbitMoveBlendEnabled())
    {
        const float Speed = GetOrbitMoveBlendSpeed();
        SpectatorOrbitYaw = FMath::FInterpTo(SpectatorOrbitYaw, DesiredOrbitYaw, DeltaTime, Speed);
        SpectatorOrbitPitch = FMath::FInterpTo(SpectatorOrbitPitch, DesiredOrbitPitch, DeltaTime, Speed);
        SpectatorOrbitRadius = FMath::FInterpTo(SpectatorOrbitRadius, DesiredOrbitRadius, DeltaTime, Speed);
    }
    else
    {
        SpectatorOrbitYaw = DesiredOrbitYaw;
        SpectatorOrbitPitch = DesiredOrbitPitch;
        SpectatorOrbitRadius = DesiredOrbitRadius;
    }

    // -- Compute where the camera SHOULD be this frame ---------------------
    FVector  TargetLoc;
    FRotator TargetRot;
    GetTargetCameraTransform(TargetLoc, TargetRot);

    if (!bCameraInitialised)
    {
        // First frame — snap to wherever the target is, no blend
        CurrentCamLocation = TargetLoc;
        CurrentCamRotation = TargetRot;
        bCameraInitialised = true;
    }
    else if (bCameraModeBlending)
    {
        // Path 1: Camera mode transition blend
        CameraModeBlendTimer += DeltaTime;
        const float Alpha = (CameraModeBlendDuration > 0.f)
            ? FMath::Clamp(CameraModeBlendTimer / CameraModeBlendDuration, 0.f, 1.f)
            : 1.f;

        CurrentCamLocation = FMath::Lerp(CamModeBlendStartLoc, TargetLoc, Alpha);
        CurrentCamRotation = FMath::Lerp(CamModeBlendStartRot, TargetRot, Alpha);

        if (Alpha >= 1.f)
            bCameraModeBlending = false;
    }
    else if (bSubjectSwitchBlending)
    {
        SubjectSwitchBlendTimer += DeltaTime;
        const float Alpha = (SubjectSwitchBlendDuration > 0.f)
            ? FMath::Clamp(SubjectSwitchBlendTimer / SubjectSwitchBlendDuration, 0.f, 1.f)
            : 1.f;

        if (IsSubjectSwitchBlendEnabled())
        {
            // Lerp FROM the stored start position, not from CurrentCamLocation.
            // This gives a consistent smooth arc from wherever we were to the target.
            const float SmoothAlpha = FMath::SmoothStep(0.f, 1.f, Alpha);
            CurrentCamLocation = FMath::Lerp(SubjectSwitchBlendStartLoc, TargetLoc, SmoothAlpha);
            CurrentCamRotation = FMath::Lerp(SubjectSwitchBlendStartRot, TargetRot, SmoothAlpha);
        }
        else
        {
            CurrentCamLocation = TargetLoc;
            CurrentCamRotation = TargetRot;
        }

        if (Alpha >= 1.f)
            bSubjectSwitchBlending = false;
    }
    else
    {
        // Path 3: No blend active — snap directly to target.
        // Orbit lag is already handled above via SpectatorOrbit* lerp,
        // so 3rd person orbit movement feels smooth without this path
        // contributing any extra latency.
        CurrentCamLocation = TargetLoc;
        CurrentCamRotation = TargetRot;
    }

    SpectatorCamera->SetWorldLocation(CurrentCamLocation);
    SpectatorCamera->SetWorldRotation(CurrentCamRotation);
}

void ASubmarineSpectatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    // Bind SpectatorLeft/Right/Up/Down to your input actions in Blueprint,
    // or bind here if using Enhanced Input in C++.
}

// ---------------------------------------------------------------------------
//  Hold-input tick helper
// ---------------------------------------------------------------------------
void ASubmarineSpectatorPawn::TickHoldInput(FHoldInput& Input, float DeltaTime,
    float HoldThreshold, float HoldCooldown, TFunction<void()> OnFire)
{
    if (!Input.bHeld) return;

    Input.HoldTimer += DeltaTime;
    if (Input.HoldTimer < HoldThreshold) return; // not yet in hold mode

    Input.HoldCooldownTimer += DeltaTime;
    if (Input.HoldCooldownTimer >= HoldCooldown)
    {
        Input.HoldCooldownTimer -= HoldCooldown;
        OnFire();
    }
}

// ---------------------------------------------------------------------------
//  Subject switch — Pressed/Released
// ---------------------------------------------------------------------------
void ASubmarineSpectatorPawn::SpectatorLeftPressed()
{
    HoldLeft.bHeld = true;
    HoldLeft.HoldTimer = 0.f;
    HoldLeft.HoldCooldownTimer = 0.f;
    SpectatorLeft(); // immediate fire on press
}
void ASubmarineSpectatorPawn::SpectatorLeftReleased()
{
    HoldLeft.bHeld = false;
    HoldLeft.HoldTimer = 0.f;
}

void ASubmarineSpectatorPawn::SpectatorRightPressed()
{
    HoldRight.bHeld = true;
    HoldRight.HoldTimer = 0.f;
    HoldRight.HoldCooldownTimer = 0.f;
    SpectatorRight();
}
void ASubmarineSpectatorPawn::SpectatorRightReleased()
{
    HoldRight.bHeld = false;
    HoldRight.HoldTimer = 0.f;
}

void ASubmarineSpectatorPawn::SpectatorUpPressed()
{
    HoldUp.bHeld = true;
    HoldUp.HoldTimer = 0.f;
    HoldUp.HoldCooldownTimer = 0.f;
    SpectatorUp();
}
void ASubmarineSpectatorPawn::SpectatorUpReleased()
{
    HoldUp.bHeld = false;
    HoldUp.HoldTimer = 0.f;
}

void ASubmarineSpectatorPawn::SpectatorDownPressed()
{
    HoldDown.bHeld = true;
    HoldDown.HoldTimer = 0.f;
    HoldDown.HoldCooldownTimer = 0.f;
    SpectatorDown();
}
void ASubmarineSpectatorPawn::SpectatorDownReleased()
{
    HoldDown.bHeld = false;
    HoldDown.HoldTimer = 0.f;
}

// ---------------------------------------------------------------------------
//  Camera toggle — Pressed/Released
// ---------------------------------------------------------------------------
void ASubmarineSpectatorPawn::SpectatorCameraTogglePressed()
{
    HoldCameraToggle = { true, 0.f, 0.f };
    DoToggleCameraMode(); // immediate fire on first press
}
void ASubmarineSpectatorPawn::SpectatorCameraToggleReleased()
{
    HoldCameraToggle.bHeld = false;
    HoldCameraToggle.HoldTimer = 0.f;
}

// ---------------------------------------------------------------------------
//  Single-fire switch actions (also called internally by hold system)
// ---------------------------------------------------------------------------

//  Input — Left / Right (cycle submarines)
void ASubmarineSpectatorPawn::SpectatorLeft()
{
    TArray<ASubmarinePawn*> Live = GetLiveSubmarines();
    if (Live.Num() <= 1) return;
    SubmarineListIndex = (SubmarineListIndex - 1 + Live.Num()) % Live.Num();
    ViewListIndex = 0;
    ApplyCurrentTarget();

    SubjectSwitchBlendStartLoc = CurrentCamLocation;
    SubjectSwitchBlendStartRot = CurrentCamRotation;

    // Start blend
    if (IsSubjectSwitchBlendEnabled())
    {
        bSubjectSwitchBlending = true;
        SubjectSwitchBlendTimer = 0.f;

        const float Speed = GetSubjectSwitchBlendSpeed();

        SubjectSwitchBlendDuration = (Speed > 0.f)
            ? (1.f / Speed)
            : 0.1f;
    }
}

void ASubmarineSpectatorPawn::SpectatorRight()
{
    TArray<ASubmarinePawn*> Live = GetLiveSubmarines();
    if (Live.Num() <= 1) return;
    SubmarineListIndex = (SubmarineListIndex + 1) % Live.Num();
    ViewListIndex = 0;
    ApplyCurrentTarget();

    SubjectSwitchBlendStartLoc = CurrentCamLocation;
    SubjectSwitchBlendStartRot = CurrentCamRotation;
    
    // Start blend
    if (IsSubjectSwitchBlendEnabled())
    {
        bSubjectSwitchBlending = true;
        SubjectSwitchBlendTimer = 0.f;

        const float Speed = GetSubjectSwitchBlendSpeed();

        SubjectSwitchBlendDuration = (Speed > 0.f)
            ? (1.f / Speed)
            : 0.1f;
    }
}

//  Input — Up / Down (cycle [submarine, torpedo0, torpedo1, ...])
void ASubmarineSpectatorPawn::SpectatorUp()
{
    if (!CurrentSubTarget) return;
    TArray<UObject*> ViewList = BuildViewList(CurrentSubTarget.Get());
    if (ViewList.Num() <= 1) return;
    ViewListIndex = (ViewListIndex - 1 + ViewList.Num()) % ViewList.Num();
    ApplyCurrentTarget();

    SubjectSwitchBlendStartLoc = CurrentCamLocation;
    SubjectSwitchBlendStartRot = CurrentCamRotation;
    
    // Start blend
    if (IsSubjectSwitchBlendEnabled())
    {
        bSubjectSwitchBlending = true;
        SubjectSwitchBlendTimer = 0.f;

        const float Speed = GetSubjectSwitchBlendSpeed();

        SubjectSwitchBlendDuration = (Speed > 0.f)
            ? (1.f / Speed)
            : 0.1f;
    }
}

void ASubmarineSpectatorPawn::SpectatorDown()
{
    if (!CurrentSubTarget) return;
    TArray<UObject*> ViewList = BuildViewList(CurrentSubTarget.Get());
    if (ViewList.Num() <= 1) return;
    ViewListIndex = (ViewListIndex + 1) % ViewList.Num();
    ApplyCurrentTarget();

    SubjectSwitchBlendStartLoc = CurrentCamLocation;
    SubjectSwitchBlendStartRot = CurrentCamRotation;
    
    // Start blend
    if (IsSubjectSwitchBlendEnabled())
    {
        bSubjectSwitchBlending = true;
        SubjectSwitchBlendTimer = 0.f;

        const float Speed = GetSubjectSwitchBlendSpeed();

        SubjectSwitchBlendDuration = (Speed > 0.f)
            ? (1.f / Speed)
            : 0.1f;
    }
}

// -----------------------------------------------------------------------------
//  Mouse / zoom input — 3rd person only, POV is always static
// -----------------------------------------------------------------------------
void ASubmarineSpectatorPawn::SpectatorMouseX(float AxisValue)
{
    if (!bSpectatorThirdPerson) return; // POV = static
    DesiredOrbitYaw += AxisValue * GetOrbitYawSensitivity();
}

void ASubmarineSpectatorPawn::SpectatorMouseY(float AxisValue)
{
    if (!bSpectatorThirdPerson) return;
    DesiredOrbitPitch = FMath::Clamp(
        DesiredOrbitPitch + AxisValue * GetOrbitPitchSensitivity(),
        GetOrbitMinPitch(), GetOrbitMaxPitch());
}

void ASubmarineSpectatorPawn::SpectatorScrollZoom(float AxisValue)
{
    if (!bSpectatorThirdPerson) return;
    DesiredOrbitRadius = FMath::Clamp(
        DesiredOrbitRadius - AxisValue * GetOrbitScrollSpeed(),
        GetOrbitMinRadius(), GetOrbitMaxRadius());
}

// ---------------------------------------------------------------------------
//  DoToggleCameraMode — internal, starts the camera-mode blend
// ---------------------------------------------------------------------------
void ASubmarineSpectatorPawn::DoToggleCameraMode()
{
    bSpectatorThirdPerson = !bSpectatorThirdPerson;

    if (IsCameraModeBlendEnabled())
    {
        bCameraModeBlending = true;
        CameraModeBlendTimer = 0.f;
        const float Speed = GetCameraModeBlendSpeed();
        CameraModeBlendDuration = FMath::Clamp(
            (Speed > 0.f) ? (1.f / Speed) : 0.1f,
            0.05f, 0.5f);
        CamModeBlendStartLoc = CurrentCamLocation;
        CamModeBlendStartRot = CurrentCamRotation;

        // Also cancel any in-progress subject-switch blend so they don't fight
        bSubjectSwitchBlending = false;
    }
}

// -----------------------------------------------------------------------------
//  GetTargetCameraTransform — dispatches to POV or 3rd person
// -----------------------------------------------------------------------------
void ASubmarineSpectatorPawn::GetTargetCameraTransform(FVector& OutLocation,
    FRotator& OutRotation) const
{
    if (bSpectatorThirdPerson)
    {
        GetThirdPersonTransform(OutLocation, OutRotation);
        return;
    }

    // Torpedo POV
    if (bWatchingTorpedo && IsValid(CurrentTorpedoTarget))
    {
        if (UCameraComponent* Cam = CurrentTorpedoTarget->GetPOVCamera())
        {
            OutLocation = Cam->GetComponentLocation();
            OutRotation = Cam->GetComponentRotation();
            return;
        }
        OutLocation = CurrentTorpedoTarget->GetActorLocation();
        OutRotation = CurrentTorpedoTarget->GetActorRotation();
        return;
    }

    // Submarine POV
    if (IsValid(CurrentSubTarget))
    {
        TArray<UCameraComponent*> Cams;
        CurrentSubTarget->GetComponents<UCameraComponent>(Cams);
        for (UCameraComponent* Cam : Cams)
        {
            if (Cam && Cam->IsActive())
            {
                OutLocation = Cam->GetComponentLocation();
                OutRotation = Cam->GetComponentRotation();
                return;
            }
        }
        OutLocation = CurrentSubTarget->GetActorLocation();
        OutRotation = CurrentSubTarget->GetActorRotation();
        return;
    }

    OutLocation = FallbackLocation;
    OutRotation = FRotator::ZeroRotator;
}

// -----------------------------------------------------------------------------
//  GetThirdPersonTransform — orbits around the current target
// -----------------------------------------------------------------------------
void ASubmarineSpectatorPawn::GetThirdPersonTransform(FVector& OutLocation,
    FRotator& OutRotation) const
{
    // Pivot: current target's world location
    FVector Pivot = FallbackLocation;

    if (bWatchingTorpedo && IsValid(CurrentTorpedoTarget))
        Pivot = CurrentTorpedoTarget->GetActorLocation();
    else if (IsValid(CurrentSubTarget))
        Pivot = CurrentSubTarget->GetActorLocation();

    const float YawRad = FMath::DegreesToRadians(SpectatorOrbitYaw);
    const float PitchRad = FMath::DegreesToRadians(SpectatorOrbitPitch);

    const FVector Offset(
        SpectatorOrbitRadius * FMath::Cos(PitchRad) * FMath::Cos(YawRad),
        SpectatorOrbitRadius * FMath::Cos(PitchRad) * FMath::Sin(YawRad),
        SpectatorOrbitRadius * FMath::Sin(PitchRad));

    OutLocation = Pivot + Offset;
    OutRotation = (Pivot - OutLocation).Rotation();
}

// -----------------------------------------------------------------------------
//  ApplyCurrentTarget
// -----------------------------------------------------------------------------
void ASubmarineSpectatorPawn::ApplyCurrentTarget()
{
    TArray<ASubmarinePawn*> Live = GetLiveSubmarines();

    if (Live.Num() == 0)
    {
        // No submarines alive — fallback camera
        CurrentSubTarget = nullptr;
        CurrentTorpedoTarget = nullptr;
        bWatchingTorpedo = false;

        // Snap to fallback immediately
        CurrentCamLocation = FallbackLocation;
        CurrentCamRotation = FRotator::ZeroRotator;
        SpectatorCamera->SetWorldLocation(FallbackLocation);
        SpectatorCamera->SetWorldRotation(FRotator::ZeroRotator);
        return;
    }

    // Clamp index
    SubmarineListIndex = FMath::Clamp(SubmarineListIndex, 0, Live.Num() - 1);
    CurrentSubTarget = Live[SubmarineListIndex];

    // Build view list for this submarine
    TArray<UObject*> ViewList = BuildViewList(CurrentSubTarget.Get());
    ViewListIndex = FMath::Clamp(ViewListIndex, 0, ViewList.Num() - 1);

    UObject* Target = ViewList[ViewListIndex];

    if (ATorpedoPawn* Torpedo = Cast<ATorpedoPawn>(Target))
    {
        CurrentTorpedoTarget = Torpedo;
        bWatchingTorpedo = true;
    }
    else
    {
        // Index 0 = submarine itself
        CurrentTorpedoTarget = nullptr;
        bWatchingTorpedo = false;
    }
}

// -----------------------------------------------------------------------------
//  ValidateTorpedoTarget — called every tick
// -----------------------------------------------------------------------------
void ASubmarineSpectatorPawn::ValidateTorpedoTarget()
{
    if (!bWatchingTorpedo) return;

    if (!IsValid(CurrentTorpedoTarget))
    {
        // Torpedo died — immediately revert to submarine, no blend needed
        CurrentTorpedoTarget = nullptr;
        bWatchingTorpedo = false;
        ViewListIndex = 0;

        // Snap camera to submarine position right now
        if (IsValid(CurrentSubTarget))
        {
            FVector  Loc;
            FRotator Rot;
            GetTargetCameraTransform(Loc, Rot);
            CurrentCamLocation = Loc;
            CurrentCamRotation = Rot;
        }
    }
}

// -----------------------------------------------------------------------------
//  GetLiveSubmarines
// -----------------------------------------------------------------------------
TArray<ASubmarinePawn*> ASubmarineSpectatorPawn::GetLiveSubmarines() const
{
    TArray<ASubmarinePawn*> Result;
    for (const TObjectPtr<ASubmarinePawn>& Sub : TrackedSubmarines)
    {
        if (IsValid(Sub))
            Result.Add(Sub);
    }
    return Result;
}

// -----------------------------------------------------------------------------
//  BuildViewList
// -----------------------------------------------------------------------------
TArray<UObject*> ASubmarineSpectatorPawn::BuildViewList(ASubmarinePawn* Sub) const
{
    TArray<UObject*> List;
    if (!IsValid(Sub)) return List;

    // Index 0 is always the submarine
    List.Add(Sub);

    // Add live torpedoes from the submarine's torpedo component
    if (USubmarineTorpedoComponent* TorpComp =
        Sub->FindComponentByClass<USubmarineTorpedoComponent>())
    {
        for (const TObjectPtr<ATorpedoPawn>& Torpedo : TorpComp->ActiveTorpedoes)
        {
            if (IsValid(Torpedo))
                List.Add(Torpedo.Get());
        }
    }

    return List;
}

// ---------------------------------------------------------------------------
//  Blend settings helpers
// ---------------------------------------------------------------------------
bool ASubmarineSpectatorPawn::IsSubjectSwitchBlendEnabled() const
{
    if (BlendSettings) return BlendSettings->IsSubjectSwitchBlendEnabled();
    // Fallback: read from submarine DA (original behaviour)
    if (IsValid(CurrentSubTarget))
    {
        const USubmarineCharacteristics* S = GetSubStats(CurrentSubTarget.Get());
        if (S) return S->bSpectatorBlendCamera;
    }
    return true;
}

float ASubmarineSpectatorPawn::GetSubjectSwitchBlendSpeed() const
{
    if (BlendSettings) return BlendSettings->GetSubjectSwitchSpeed();
    if (IsValid(CurrentSubTarget))
    {
        const USubmarineCharacteristics* S = GetSubStats(CurrentSubTarget.Get());
        if (S) return S->SpectatorBlendSpeed;
    }
    return 5.f;
}

bool ASubmarineSpectatorPawn::IsCameraModeBlendEnabled() const
{
    if (BlendSettings) return BlendSettings->IsCameraModeBlendEnabled();
    return false;
}

float ASubmarineSpectatorPawn::GetCameraModeBlendSpeed() const
{
    if (BlendSettings) return BlendSettings->GetCameraModeSpeed();
    return 8.f;
}

bool ASubmarineSpectatorPawn::IsOrbitMoveBlendEnabled() const
{
    if (BlendSettings) return BlendSettings->IsOrbitMoveBlendEnabled();
    return false; // original behaviour: no orbit lag
}

float ASubmarineSpectatorPawn::GetOrbitMoveBlendSpeed() const
{
    if (BlendSettings) return BlendSettings->GetOrbitMoveSpeed();
    return 12.f;
}

// -----------------------------------------------------------------------------
//  Orbit sensitivity helpers — fall back to sensible defaults if no DA
// -----------------------------------------------------------------------------
float ASubmarineSpectatorPawn::GetOrbitYawSensitivity() const
{
    if (IsValid(CurrentSubTarget))
        if (const USubmarineCharacteristics* S = GetSubStats(CurrentSubTarget.Get()))
            return S->ThirdPersonYawSensitivity;
    return 1.f;
}

float ASubmarineSpectatorPawn::GetOrbitPitchSensitivity() const
{
    if (IsValid(CurrentSubTarget))
        if (const USubmarineCharacteristics* S = GetSubStats(CurrentSubTarget.Get()))
            return S->ThirdPersonPitchSensitivity;
    return 1.f;
}

float ASubmarineSpectatorPawn::GetOrbitScrollSpeed() const
{
    if (IsValid(CurrentSubTarget))
        if (const USubmarineCharacteristics* S = GetSubStats(CurrentSubTarget.Get()))
            return S->ThirdPersonScrollSpeed;
    return 200.f;
}

float ASubmarineSpectatorPawn::GetOrbitMinPitch() const
{
    if (IsValid(CurrentSubTarget))
        if (const USubmarineCharacteristics* S = GetSubStats(CurrentSubTarget.Get()))
            return S->ThirdPersonMinPitch;
    return -80.f;
}

float ASubmarineSpectatorPawn::GetOrbitMaxPitch() const
{
    if (IsValid(CurrentSubTarget))
        if (const USubmarineCharacteristics* S = GetSubStats(CurrentSubTarget.Get()))
            return S->ThirdPersonMaxPitch;
    return 80.f;
}

float ASubmarineSpectatorPawn::GetOrbitMinRadius() const
{
    if (IsValid(CurrentSubTarget))
        if (const USubmarineCharacteristics* S = GetSubStats(CurrentSubTarget.Get()))
            return S->ThirdPersonMinRadius;
    return 200.f;
}

float ASubmarineSpectatorPawn::GetOrbitMaxRadius() const
{
    if (IsValid(CurrentSubTarget))
        if (const USubmarineCharacteristics* S = GetSubStats(CurrentSubTarget.Get()))
            return S->ThirdPersonMaxRadius;
    return 5000.f;
}

// -----------------------------------------------------------------------------
//  GetSubStats
// -----------------------------------------------------------------------------
const USubmarineCharacteristics* ASubmarineSpectatorPawn::GetSubStats(ASubmarinePawn* Sub) const
{
    if (!Sub) return GetDefault<USubmarineCharacteristics>();
    return Sub->Characteristics
        ? Sub->Characteristics.Get()
        : GetDefault<USubmarineCharacteristics>();
}