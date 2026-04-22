// Fill out your copyright notice in the Description page of Project Settings.

#include "DeathSequenceComponent.h"
#include "ReplaySettings.h"
#include "ReplayData.h"
#include "SubmarinePawn.h"
#include "TorpedoPawn.h"
#include "SubmarineCharacteristics.h"
#include "SubmarineSpectatorPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UDeathSequenceComponent::UDeathSequenceComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

// ---------------------------------------------------------------------------
//  BeginDeathSequence
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::BeginDeathSequence(ASubmarinePawn* DeadSubmarine,
    AController* DeadController,
    AActor* Killer,
    UReplayData* ReplaySlice)
{
    if (Phase != EDeathSequencePhase::Inactive)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DeathSeq] BeginDeathSequence called while already active — ignored"));
        return;
    }

    if (!DeadSubmarine || !DeadController)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DeathSeq] BeginDeathSequence called with null submarine or controller"));
        return;
    }

    CachedDeadSub = DeadSubmarine;
    CachedDeadController = DeadController;
    CachedKiller = Killer;
    CachedReplaySlice = ReplaySlice;
    bCameraFrozen = false;

    // Resolve the killer submarine for muzzle-estimation fallback
    if (ATorpedoPawn* T = Cast<ATorpedoPawn>(Killer))
        KillerSubmarine = Cast<ASubmarinePawn>(T->FiringShooter.Get());
    else
        KillerSubmarine = Cast<ASubmarinePawn>(Killer);

    ActiveDeathCamMode = DetermineDeathCamMode(Killer);

    UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Starting for '%s' | Killer='%s' | Mode=%d"),
        *DeadSubmarine->GetName(),
        Killer ? *Killer->GetName() : TEXT("None"),
        static_cast<int32>(ActiveDeathCamMode));

    // -----------------------------------------------------------------------
    //  Step 1: Immediately hide + disable collisions on the dead submarine.
    //  The actor is NOT destroyed yet — we need it as a camera anchor during
    //  the death cam phase. Destruction happens in OnDeathSequenceComplete.
    // -----------------------------------------------------------------------
    TArray<UPrimitiveComponent*> Prims;
    DeadSubmarine->GetComponents<UPrimitiveComponent>(Prims);
    for (UPrimitiveComponent* Prim : Prims)
    {
        Prim->SetVisibility(false, /*bPropagateToChildren=*/true);
        Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    StartPreDelay();
}

// ---------------------------------------------------------------------------
//  Phase transitions
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::StartPreDelay()
{
    const UReplaySettings* S = GetSettings();
    const float Delay = S ? S->DeathPreviewDelay : 0.f;

    Phase = EDeathSequencePhase::PreDelay;
    PhaseTimer = 0.f;
    PhaseDuration = Delay;


    UE_LOG(LogTemp, Log, TEXT("[DeathSeq] PreDelay phase: %.2fs"), Delay);

    if (Delay <= 0.f)
        StartDeathCam(); // skip straight to death cam
}

void UDeathSequenceComponent::StartDeathCam()
{
    const UReplaySettings* S = GetSettings();
    const float Duration = S ? S->DeathReplayDuration : 0.f;

    if (Duration <= 0.f || ActiveDeathCamMode == EDeathCamMode::None)
    {
        UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Skipping death cam (duration=%.1f, mode=%d)"),
            Duration, static_cast<int32>(ActiveDeathCamMode));
        FinishSequence();
        return;
    }

    Phase = EDeathSequencePhase::DeathCam;
    PhaseTimer = 0.f;
    PhaseDuration = Duration;

    OnDeathCamStarted.Broadcast(ActiveDeathCamMode, Duration);
    UE_LOG(LogTemp, Log, TEXT("[DeathSeq] DeathCam phase started — mode=%d duration=%.1fs"),
        static_cast<int32>(ActiveDeathCamMode), Duration);
}

void UDeathSequenceComponent::FinishSequence()
{
    Phase = EDeathSequencePhase::Inactive;
    PhaseTimer = 0.f;
    PhaseDuration = 0.f;

    UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Sequence complete — broadcasting OnDeathSequenceComplete"));
    OnDeathSequenceComplete.Broadcast();
}

// ---------------------------------------------------------------------------
//  Skip
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::SkipDeathCam()
{
    const UReplaySettings* S = GetSettings();
    if (!S || !S->bDeathReplaySkippable) return;

    if (Phase == EDeathSequencePhase::DeathCam || Phase == EDeathSequencePhase::PreDelay)
    {
        UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Player skipped death cam"));
        FinishSequence();
    }
}

// ---------------------------------------------------------------------------
//  Tick
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    switch (Phase)
    {
    case EDeathSequencePhase::PreDelay:
        PhaseTimer += DeltaTime;
        if (PhaseTimer >= PhaseDuration)
            StartDeathCam();
        break;

    case EDeathSequencePhase::DeathCam:
        PhaseTimer += DeltaTime;
        TickDeathCam(DeltaTime);
        if (PhaseTimer >= PhaseDuration)
            FinishSequence();
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
//  Death cam tick — positions the dead controller's view each frame
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::TickDeathCam(float DeltaTime)
{
    AController* DC = CachedDeadController.Get();
    if (!DC) return;

    // If already frozen, just keep applying the frozen transform every frame
    if (bCameraFrozen)
    {
        ApplyViewToController(FrozenCamLocation, FrozenCamRotation);
        return;
    }

    FVector  CamLoc;
    FRotator CamRot;

    if (ComputeDeathCamTransform(CamLoc, CamRot))
    {
        // Store as last known good transform in case we need to freeze next frame
        FrozenCamLocation = CamLoc;
        FrozenCamRotation = CamRot;
        bHasValidFrozenFrame = true;
        ApplyViewToController(CamLoc, CamRot);
    }
    else
    {
        // Killer is gone and no estimate available
        if (bHasValidFrozenFrame)
        {
            UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Killer gone — freezing camera at last known position"));
            bCameraFrozen = true;
            ApplyViewToController(FrozenCamLocation, FrozenCamRotation);
        }
        else
        {
            // No valid frame ever — fall back to dead sub position
            UE_LOG(LogTemp, Warning, TEXT("[DeathSeq] No valid frame available — using dead sub fallback"));
            if (ASubmarinePawn* DeadSub = CachedDeadSub.Get())
            {
                GetThirdPersonBehind(DeadSub->GetActorLocation(),
                    DeadSub->GetActorRotation(),
                    FrozenCamLocation, FrozenCamRotation);
                bHasValidFrozenFrame = true;
                bCameraFrozen = true;
                ApplyViewToController(FrozenCamLocation, FrozenCamRotation);
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  ComputeDeathCamTransform
// ---------------------------------------------------------------------------
bool UDeathSequenceComponent::ComputeDeathCamTransform(FVector& OutLocation, FRotator& OutRotation) const
{
    switch (ActiveDeathCamMode)
    {
        // -----------------------------------------------------------------------
    case EDeathCamMode::KillerThirdPerson:
    {
        // Try the live killer first
        AActor* Killer = CachedKiller.Get();
        if (IsValid(Killer))
        {
            GetThirdPersonBehind(Killer->GetActorLocation(),
                Killer->GetActorRotation(),
                OutLocation, OutRotation);
            return true;
        }

        // Killer gone — try to estimate from the firing submarine
        return EstimateKillerTransformFromSub(OutLocation, OutRotation);
    }

    // -----------------------------------------------------------------------
    case EDeathCamMode::KillerPOV:
    {
        // Torpedo POV
        if (ATorpedoPawn* Torpedo = Cast<ATorpedoPawn>(CachedKiller.Get()))
        {
            if (IsValid(Torpedo))
            {
                if (UCameraComponent* Cam = Torpedo->GetPOVCamera())
                {
                    OutLocation = Cam->GetComponentLocation();
                    OutRotation = Cam->GetComponentRotation();
                    return true;
                }
                OutLocation = Torpedo->GetActorLocation();
                OutRotation = Torpedo->GetActorRotation();
                return true;
            }
        }

        // Submarine POV
        if (ASubmarinePawn* AttackerSub = Cast<ASubmarinePawn>(CachedKiller.Get()))
        {
            if (IsValid(AttackerSub))
            {
                TArray<UCameraComponent*> Cams;
                AttackerSub->GetComponents<UCameraComponent>(Cams);
                for (UCameraComponent* Cam : Cams)
                {
                    if (Cam && Cam->IsActive())
                    {
                        OutLocation = Cam->GetComponentLocation();
                        OutRotation = Cam->GetComponentRotation();
                        return true;
                    }
                }
                OutLocation = AttackerSub->GetActorLocation();
                OutRotation = AttackerSub->GetActorRotation();
                return true;
            }
        }

        // Killer gone — estimate from firing sub
        return EstimateKillerTransformFromSub(OutLocation, OutRotation);
    }

    // -----------------------------------------------------------------------
    case EDeathCamMode::StaticBehindDead:
    {
        // Use the dead sub's last known transform (it's hidden but still valid)
        if (ASubmarinePawn* DeadSub = CachedDeadSub.Get())
        {
            GetThirdPersonBehind(DeadSub->GetActorLocation(),
                DeadSub->GetActorRotation(),
                OutLocation, OutRotation);
            return true;
        }
        return false;
    }

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
//  EstimateKillerTransformFromSub
//  When the torpedo doesn't exist (not yet spawned, or already destroyed),
//  we use the firing submarine's muzzle transform as a stand-in.
// ---------------------------------------------------------------------------
bool UDeathSequenceComponent::EstimateKillerTransformFromSub(FVector& OutLocation,
    FRotator& OutRotation) const
{
    ASubmarinePawn* Sub = KillerSubmarine.Get();
    if (!IsValid(Sub)) return false;

    // Get the muzzle offset from the sub's characteristics
    FVector MuzzleOffset = FVector(300.f, 0.f, 0.f); // safe fallback
    if (Sub->Characteristics)
        MuzzleOffset = Sub->Characteristics->TorpedoSpawnOffset;

    const FVector  EstimatedLocation = Sub->GetActorLocation()
        + Sub->GetActorTransform().TransformVector(MuzzleOffset);
    const FRotator EstimatedRotation = Sub->GetActorRotation();

    switch (ActiveDeathCamMode)
    {
    case EDeathCamMode::KillerThirdPerson:
        GetThirdPersonBehind(EstimatedLocation, EstimatedRotation, OutLocation, OutRotation);
        return true;

    case EDeathCamMode::KillerPOV:
        // POV estimate: look forward from the muzzle point
        OutLocation = EstimatedLocation;
        OutRotation = EstimatedRotation;
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
//  GetThirdPersonBehind
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::GetThirdPersonBehind(const FVector& TargetLocation,
    const FRotator& TargetRotation,
    FVector& OutCamLocation,
    FRotator& OutCamRotation)
{
    // Place camera directly behind the target (180 degrees from forward)
    const FVector Backward = TargetRotation.Vector() * -1.f;
    // Slight upward offset so we're not at the exact same Z
    const FVector Up = FVector(0.f, 0.f, 1.f);
    const FVector Offset = (Backward * DeathCamThirdPersonRadius) + (Up * DeathCamThirdPersonRadius * 0.25f);

    OutCamLocation = TargetLocation + Offset;
    OutCamRotation = (TargetLocation - OutCamLocation).Rotation();
}

// ---------------------------------------------------------------------------
//  ApplyViewToController
//  Directly sets the dead player's view without possessing a new actor.
//  Uses SetInitialLocationAndRotation on the controller's PlayerCameraManager.
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::ApplyViewToController(const FVector& Location,
    const FRotator& Rotation) const
{
    APlayerController* PC = Cast<APlayerController>(CachedDeadController.Get());
    if (!PC) return;

    // SetViewTargetWithBlend to nullptr sets a "free" camera position using
    // the PlayerCameraManager. The cleaner UE5 approach is to set the
    // controller's view directly via SetInitialLocationAndRotation.
    PC->SetInitialLocationAndRotation(Location, Rotation);

    // Force the camera manager to update this frame
    if (PC->PlayerCameraManager)
        PC->PlayerCameraManager->SetActorLocationAndRotation(Location, Rotation);
}

// ---------------------------------------------------------------------------
//  DetermineDeathCamMode
// ---------------------------------------------------------------------------
EDeathCamMode UDeathSequenceComponent::DetermineDeathCamMode(AActor* Killer) const
{
    const UReplaySettings* S = GetSettings();
    if (!S) return EDeathCamMode::StaticBehindDead;

    // No killer info at all
    if (!Killer)
        return EDeathCamMode::StaticBehindDead;

    // Use the default from settings DA (KillerThirdPerson or KillerPOV)
    return S->DefaultDeathCamMode;
}

// ---------------------------------------------------------------------------
//  GetDeathCamProgress
// ---------------------------------------------------------------------------
float UDeathSequenceComponent::GetDeathCamProgress() const
{
    if (Phase != EDeathSequencePhase::DeathCam || PhaseDuration <= 0.f)
        return 0.f;
    return FMath::Clamp(PhaseTimer / PhaseDuration, 0.f, 1.f);
}

// ---------------------------------------------------------------------------
//  Settings helper
// ---------------------------------------------------------------------------
const UReplaySettings* UDeathSequenceComponent::GetSettings() const
{
    if (ReplaySettings) return ReplaySettings;
    return GetDefault<UReplaySettings>();
}