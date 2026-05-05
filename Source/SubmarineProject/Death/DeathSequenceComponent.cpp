// Fill out your copyright notice in the Description page of Project Settings.

#include "DeathSequenceComponent.h"
#include "ScreenFadeComponent.h"
#include "Replay/ReplaySettings.h"
#include "Replay/ReplayData.h"
#include "Replay/ReplayPlaybackComponent.h"
#include "Submarine/SubmarinePawn.h"
#include "Torpedo/TorpedoPawn.h"
#include "Submarine/SubmarineCharacteristics.h"
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
    UReplayData* ReplaySlice,
    UReplayPlaybackComponent* InPlaybackComponent,
    float                     InPlaybackSpeed,
    UScreenFadeComponent* InScreenFade)
{
    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (Phase != EDeathSequencePhase::Inactive)
    {
        if (S->bLogDeathSeq) {
            UE_LOG(LogTemp, Warning, TEXT("[DeathSeq] BeginDeathSequence called while already active — ignored"));
        }
        return;
    }

    if (!DeadSubmarine || !DeadController)
    {
        if (S->bLogDeathSeq) {
            UE_LOG(LogTemp, Warning, TEXT("[DeathSeq] BeginDeathSequence called with null submarine or controller"));
        }
        return;
    }


    CachedDeadSub = DeadSubmarine;
    CachedDeadController = DeadController;
    CachedKiller = Killer;
    CachedReplaySlice = ReplaySlice;
    bCameraFrozen = false;
    bHasValidFrozenFrame = false;
    ScreenFade = InScreenFade;
    PlaybackComponent = InPlaybackComponent;
    bUsingReplayPlayback = false;

    // Resolve the killer submarine for muzzle-estimation fallback
    if (ATorpedoPawn* T = Cast<ATorpedoPawn>(Killer))
        KillerSubmarine = Cast<ASubmarinePawn>(T->FiringShooter.Get());
    else
        KillerSubmarine = Cast<ASubmarinePawn>(Killer);

    ActiveDeathCamMode = DetermineDeathCamMode(Killer);

    if (S->bLogDeathSeq) {
        UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Starting for '%s' | Killer='%s' | Mode=%d | Speed=%.2f"),
            DeadSubmarine ? *DeadSubmarine->GetName() : TEXT("?"),
            Killer ? *Killer->GetName() : TEXT("None"),
            static_cast<int32>(ActiveDeathCamMode),
            InPlaybackSpeed);
    }

    // -----------------------------------------------------------------------
    //  Wire the replay playback component
    // -----------------------------------------------------------------------
    if (InPlaybackComponent && ReplaySlice && ReplaySlice->TickFrames.Num() > 0)
    {
        InPlaybackComponent->DeadSubGuid = DeadSubmarine->GetActorInstanceGuid();

        // Killer torpedo identity.
         // The torpedo is typically already Destroy()ed when we get here (4s after death).
         // GameMode pre-sets KillerTorpedoActorName + KillerTorpedoGuid before calling us.
         // Only overwrite those fields when the torpedo is still alive.
        if (ATorpedoPawn* KillerTorp = Cast<ATorpedoPawn>(Killer); IsValid(KillerTorp))
        {
            InPlaybackComponent->KillerTorpedoGuid = KillerTorp->GetActorInstanceGuid();

            if (ASubmarinePawn* Shooter =
                Cast<ASubmarinePawn>(KillerTorp->FiringShooter.Get()))
            {
                InPlaybackComponent->KillerActorGuid = Shooter->GetActorInstanceGuid();
            }
            else
            {
                InPlaybackComponent->KillerActorGuid = KillerTorp->GetActorInstanceGuid();
            }

            if (S->bLogDeathSeq) {
                UE_LOG(LogTemp, Log, TEXT("[DeathSeq] KillerSub='%s' KillerTorpedo='%s' (live)"),
                    *InPlaybackComponent->KillerActorGuid.ToString(),
                    *InPlaybackComponent->KillerTorpedoGuid.ToString());
            }
        }
        else if (!InPlaybackComponent->KillerTorpedoGuid.IsValid())
        {
            // Torpedo already destroyed AND no pre-set GUID -- direct sub kill or environmental
            if (ASubmarinePawn* KillerSub = Cast<ASubmarinePawn>(Killer); IsValid(KillerSub))
            {
                InPlaybackComponent->KillerActorGuid = KillerSub->GetActorInstanceGuid();
                InPlaybackComponent->KillerTorpedoGuid = FGuid();

                if (S->bLogDeathSeq) {
                    UE_LOG(LogTemp, Log, TEXT("[DeathSeq] KillerSub='%s' (no torpedo)"),
                        *InPlaybackComponent->KillerActorGuid.ToString());
                }
            }
            else
            {
                InPlaybackComponent->KillerActorGuid = FGuid();
                InPlaybackComponent->KillerTorpedoGuid = FGuid();

                if (S->bLogDeathSeq) {
                    UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Environmental -- no killer ghost"));
                }
            }
        }
        else
        {
            if (KillerSubmarine.IsValid())
            {
                InPlaybackComponent->KillerActorGuid = KillerSubmarine->GetActorInstanceGuid();
            }

            if (S->bLogDeathSeq) {
                UE_LOG(LogTemp, Log,
                    TEXT("[DeathSeq] KillerSub='%s' KillerTorpedo='%s' (pre-set by GameMode)"),
                    *InPlaybackComponent->KillerActorGuid.ToString(),
                    *InPlaybackComponent->KillerTorpedoGuid.ToString());
            }
        }

        // Start playback from the beginning of the slice at real speed
        InPlaybackComponent->BeginPlayback(
            ReplaySlice,
            DeadController,
            ReplaySlice->RecordStartTime,
            InPlaybackSpeed);

        bUsingReplayPlayback = true;

        if (S->bLogDeathSeq) {
            UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Replay playback started"));
        }
    }
    else
    {
        if (S->bLogDeathSeq) {
            UE_LOG(LogTemp, Warning,
                TEXT("[DeathSeq] No replay data or playback component — using live cam fallback"));
        }
    }
    
    // Start the death cam immediately (pre-delay was the GameMode timer)
    StartDeathCam(InPlaybackSpeed);
}
// ---------------------------------------------------------------------------
//  StartDeathCam
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::StartDeathCam(float InPlaybackSpeed)
{
    const UReplaySettings* S = GetSettings();
    if (!S) { FinishSequence(); return; }

    if (ActiveDeathCamMode == EDeathCamMode::None)
    {
        FinishSequence();
        return;
    }

    // Wall-clock duration = effective content seconds / playback speed
    const float WallClock = S->GetDeathCamWallClockDuration();

    if (WallClock <= 0.f)
    {
        FinishSequence();
        return;
    }

    Phase = EDeathSequencePhase::DeathCam;
    PhaseTimer = 0.f;
    PhaseDuration = WallClock;

    // Schedule fades — purely cosmetic, no effect on PhaseDuration
    ScheduleFades(WallClock);

    OnDeathCamStarted.Broadcast(ActiveDeathCamMode, WallClock);

    if (S->bLogDeathSeq) {
        UE_LOG(LogTemp, Log,
            TEXT("[DeathSeq] DeathCam started — mode=%d wallclock=%.1fs"),
            static_cast<int32>(ActiveDeathCamMode), WallClock);
    }
}

// ---------------------------------------------------------------------------
//  ScheduleFades
//  Fades are cosmetic overlays only. They do NOT affect PhaseDuration.
//
//  Fade-in: fires immediately at t=0 (camera fades from black to clear
//           over FadeInDuration seconds).
//  Fade-out: fires at t = WallClockDuration - FadeOutDuration so the
//            screen is fully black exactly when the cam ends.
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::ScheduleFades(float WallClockDuration)
{
    const UReplaySettings* S = GetSettings();
    if (!S || !ScreenFade) return;

    APlayerController* PC = Cast<APlayerController>(CachedDeadController.Get());
    if (!PC) return;

    UWorld* World = GetWorld();
    if (!World) return;


    const FScreenFadeSettings FadeSettings = ScreenFade->HasFadeEntry("DeathReplay")
        ? ScreenFade->GetFadeEntry("DeathReplay")
        : FScreenFadeSettings{};
    float TransitionInDuration = 0.f;

    if (S->bLogDeathSeq) {
        UE_LOG(LogTemp, Log,
            TEXT("[DeathSeq] Sechduling Fading Started at %.2fs"), PhaseTimer);
    }


    // ------------------------------------------------------------------
    //  DeathReplayFade FadeIn
    // ------------------------------------------------------------------
    if (FadeSettings.bFadeIn || FadeSettings.bBlackScreenIn)
    {
        // Start after GameplayFade finishes (minimum 0.001s to avoid t=0 edge case)
        const float FadeInDelay = 0.001f;

        TWeakObjectPtr<UScreenFadeComponent> WeakFade(ScreenFade);
        TWeakObjectPtr<APlayerController>    WeakPC(PC);
        // Getting safe settings for transition in, so fade doesn't end after the end of Replay
        FScreenFadeSettings                  CapturedSettings = SafeTransitionTransform(FadeSettings, WallClockDuration, /*TransitionIn*/true);
        
        // Updating TransitionInDuration, to restrict TransitionOutDuration
        if (CapturedSettings.bFadeIn && CapturedSettings.FadeInDuration > 0.f) {
            TransitionInDuration += CapturedSettings.FadeInDuration;
        }
        if (CapturedSettings.bBlackScreenIn && CapturedSettings.BlackScreenInDuration > 0.f) {
            TransitionInDuration += CapturedSettings.BlackScreenInDuration;
        }

        World->GetTimerManager().SetTimer(FadeInTimerHandle,
            FTimerDelegate::CreateLambda([WeakFade, WeakPC, CapturedSettings]()
                {
                    if (WeakFade.IsValid() && WeakPC.IsValid())
                        WeakFade->FadeIn(WeakPC.Get(), CapturedSettings);
                }),
            FadeInDelay,
            false);

        if (S->bLogDeathSeq) {
            UE_LOG(LogTemp, Log,
                TEXT("[DeathSeq] DeathReplayFade IN scheduled in %.2fs from %.2fs, for %.2fs"),
                FadeInDelay, PhaseTimer, TransitionInDuration);
        }
    }

    // ------------------------------------------------------------------
    //  DeathReplayFade FadeOut
    //
    //  Starts at (WallClockDuration - TransitionOutDuration + TransitionInDuration)
    // so it finishes exactly when the death cam ends.
    // ------------------------------------------------------------------
    if (FadeSettings.bFadeOut || FadeSettings.bBlackScreenOut)
    {

        FScreenFadeSettings                  CapturedSettings = SafeTransitionTransform(FadeSettings, WallClockDuration-TransitionInDuration- 0.001f, /*TransitionIn*/false);

        float TransitionOutDuration = 0.f;
        // Updating TransitionInDuration, to restrict TransitionOutDuration
        if (CapturedSettings.bFadeOut && CapturedSettings.FadeOutDuration > 0.f) {
            TransitionOutDuration += CapturedSettings.FadeOutDuration;
        }
        if (CapturedSettings.bBlackScreenOut && CapturedSettings.BlackScreenOutDuration > 0.f) {
            TransitionOutDuration += CapturedSettings.BlackScreenOutDuration;
        }

        const float FadeOutStartTime =
            TransitionInDuration + FMath::Max(0.001f, WallClockDuration - TransitionInDuration - TransitionOutDuration);

        TWeakObjectPtr<UScreenFadeComponent> WeakFade(ScreenFade);
        TWeakObjectPtr<APlayerController>    WeakPC(PC);

        World->GetTimerManager().SetTimer(FadeOutTimerHandle,
            FTimerDelegate::CreateLambda([WeakFade, WeakPC, CapturedSettings]()
                {
                    if (WeakFade.IsValid() && WeakPC.IsValid())
                        WeakFade->FadeOut(WeakPC.Get(), CapturedSettings);
                }),
            FadeOutStartTime,
            false);

        if (S->bLogDeathSeq) {
            UE_LOG(LogTemp, Log,
                TEXT("[DeathSeq] DeathReplayFade OUT scheduled in %.2fs from %.2fs - (wallclock=%.2f - transitionoutduration=%.2f)"),
                FadeOutStartTime, PhaseTimer, WallClockDuration, TransitionOutDuration);
        }
    }
}

// ---------------------------------------------------------------------------
//  CancelFadeTimers
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::CancelFadeTimers()
{
    UWorld* World = GetWorld();
    if (!World) return;
    World->GetTimerManager().ClearTimer(FadeInTimerHandle);
    World->GetTimerManager().ClearTimer(FadeOutTimerHandle);
}

// ---------------------------------------------------------------------------
//  FinishSequence
// ---------------------------------------------------------------------------

void UDeathSequenceComponent::FinishSequence()
{
    CancelFadeTimers();

    const UReplaySettings* S = GetSettings();
    if (!S) return;

    if (PlaybackComponent && bUsingReplayPlayback)
        PlaybackComponent->StopPlayback();

    PlaybackComponent = nullptr;
    bUsingReplayPlayback = false;

    Phase = EDeathSequencePhase::Inactive;
    PhaseTimer = 0.f;
    PhaseDuration = 0.f;


    if (S->bLogDeathSeq) {
        UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Sequence complete — broadcasting OnDeathSequenceComplete"));
    }
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
        if (S->bLogDeathSeq) {
            UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Player skipped death cam"));
        }
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

    if (Phase == EDeathSequencePhase::DeathCam)
    {
        PhaseTimer += DeltaTime;
        TickDeathCam(DeltaTime);

        if (PhaseTimer >= PhaseDuration)
            FinishSequence();
    }
}

// ---------------------------------------------------------------------------
//  Death cam tick — positions the dead controller's view each frame
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::TickDeathCam(float DeltaTime)
{
    if (!CachedDeadController.IsValid()) return;

    if (bCameraFrozen)
    {
        ApplyViewToController(FrozenCamLocation, FrozenCamRotation);
        return;
    }

    const UReplaySettings* S = GetSettings();
    if (!S) return;

    FVector  CamLoc;
    FRotator CamRot;
    bool bOk = false;

    if (bUsingReplayPlayback)
        bOk = ComputeReplayDeathCamTransform(CamLoc, CamRot);
    else
        bOk = ComputeLiveDeathCamTransform(CamLoc, CamRot);

    if (bOk)
    {
        FrozenCamLocation = CamLoc;
        FrozenCamRotation = CamRot;
        bHasValidFrozenFrame = true;
        ApplyViewToController(CamLoc, CamRot);

        // Throttled log every 0.5s
        static float DbgCamTimer = 0.f;
        DbgCamTimer += DeltaTime;
        if (DbgCamTimer >= 0.5f)
        {
            DbgCamTimer = 0.f;
            if (S->bLogDeathCam) {
                UE_LOG(LogTemp, Log,
                    TEXT("[DeathCam] t=%.2f/%.2f Mode=%d Replay=%d CamLoc=%s"),
                    PhaseTimer, PhaseDuration,
                    static_cast<int32>(ActiveDeathCamMode),
                    bUsingReplayPlayback ? 1 : 0,
                    *CamLoc.ToString());
            }
        }
    }
    else if (bHasValidFrozenFrame)
    {
        if (S->bLogDeathSeq) {
            UE_LOG(LogTemp, Log, TEXT("[DeathSeq] Target lost — freezing camera"));
        }
        bCameraFrozen = true;
        ApplyViewToController(FrozenCamLocation, FrozenCamRotation);
    }
    else
    {
        // Last resort: behind the dead sub's hidden position
        if (ASubmarinePawn* DeadSub = CachedDeadSub.Get())
        {
            const float R = GetSettings() ? GetSettings()->DeathCamThirdPersonRadius : 800.f;
            GetThirdPersonBehind(DeadSub->GetActorLocation(),
                DeadSub->GetActorRotation(),
                FrozenCamLocation, FrozenCamRotation, R);
            bHasValidFrozenFrame = true;
            bCameraFrozen = true;
            ApplyViewToController(FrozenCamLocation, FrozenCamRotation);
        }
    }
}

// ---------------------------------------------------------------------------
//  ComputeReplayDeathCamTransform — uses ghost actors from playback component
// ---------------------------------------------------------------------------
bool UDeathSequenceComponent::ComputeReplayDeathCamTransform(FVector& OutLoc,
    FRotator& OutRot) const
{
    if (!PlaybackComponent) return false;

    const UReplaySettings* S = GetSettings();
    if (!S) return false;

    const float Radius = S->DeathCamThirdPersonRadius;

    switch (ActiveDeathCamMode)
    {
        // -----------------------------------------------------------------------
    case EDeathCamMode::KillerThirdPerson:
    {
        // Priority: torpedo ghost > killer sub ghost > dead sub ghost (environmental)
        AActor* Target = nullptr;
        FString TargetSource = TEXT("none");

        if (PlaybackComponent->KillerTorpedoGuid.IsValid())
        {
            Target = PlaybackComponent->GetKillerTorpedoGhost();
            /*UE_LOG(LogTemp, Log,
                TEXT("[DeathCam] TorpedoGhostLookup: name='%s' ghost=%s hidden=%d loc=%s"),
                *PlaybackComponent->KillerTorpedoActorName,
                IsValid(Target) ? TEXT("FOUND") : TEXT("MISSING"),
                IsValid(Target) ? (Target->IsHidden() ? 1 : 0) : -1,
                IsValid(Target) ? *Target->GetActorLocation().ToString() : TEXT("N/A"));*/
            if (IsValid(Target)) TargetSource = TEXT("torpedo_ghost");
        }

        if (!IsValid(Target))
        {
            Target = PlaybackComponent->GetKillerGhost();
            if (S->bLogDeathSeq) {
                UE_LOG(LogTemp, Log,
                    TEXT("[DeathCam] KillerSubGhostFallback: name='%s' ghost=%s"),
                    *PlaybackComponent->KillerActorGuid.ToString(),
                    IsValid(Target) ? TEXT("FOUND") : TEXT("MISSING"));
            }
            if (IsValid(Target)) TargetSource = TEXT("killer_sub_ghost_fallback");
        }

        if (!IsValid(Target))
        {
            Target = PlaybackComponent->GetDeadSubGhost();
            if (IsValid(Target)) TargetSource = TEXT("dead_sub_ghost_fallback");
        }

        if (!IsValid(Target))
        {
            if (S->bLogDeathSeq) {
                UE_LOG(LogTemp, Warning, TEXT("[DeathCam] KillerThirdPerson: NO valid target found!"));
            }
            return false;
        }

        /*UE_LOG(LogTemp, Log,
            TEXT("[DeathCam] Tracking '%s' via %s at %s hidden=%d"),
            *Target->GetName(), *TargetSource, *Target->GetActorLocation().ToString(),
            Target->IsHidden() ? 1 : 0);*/

        GetThirdPersonBehind(Target->GetActorLocation(), Target->GetActorRotation(),
            OutLoc, OutRot, Radius);
        return true;
    }

    // -----------------------------------------------------------------------
    case EDeathCamMode::KillerPOV:
    {
        // Torpedo ghost (anchor has no camera — use its transform directly as nose-cam)
        if (PlaybackComponent->KillerTorpedoGuid.IsValid())
        {
            if (AActor* TG = PlaybackComponent->GetKillerTorpedoGhost(); IsValid(TG))
            {
                OutLoc = TG->GetActorLocation();
                OutRot = TG->GetActorRotation();
                return true;
            }
        }

        // Killer sub ghost
        if (AActor* SG = PlaybackComponent->GetKillerGhost(); IsValid(SG))
        {
            OutLoc = SG->GetActorLocation();
            OutRot = SG->GetActorRotation();
            return true;
        }

        // Environmental kill — dead sub ghost POV
        if (AActor* DG = PlaybackComponent->GetDeadSubGhost(); IsValid(DG))
        {
            OutLoc = DG->GetActorLocation();
            OutRot = DG->GetActorRotation();
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    case EDeathCamMode::StaticBehindDead:
    {
        if (AActor* DG = PlaybackComponent->GetDeadSubGhost(); IsValid(DG))
        {
            GetThirdPersonBehind(DG->GetActorLocation(), DG->GetActorRotation(),
                OutLoc, OutRot, Radius);
            return true;
        }
        return false;
    }

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
//  ComputeLiveDeathCamTransform — fallback using live actors
// ---------------------------------------------------------------------------
bool UDeathSequenceComponent::ComputeLiveDeathCamTransform(FVector& OutLoc,
    FRotator& OutRot) const
{
    const float Radius = GetSettings() ? GetSettings()->DeathCamThirdPersonRadius : 800.f;

    switch (ActiveDeathCamMode)
    {
    case EDeathCamMode::KillerThirdPerson:
    {
        if (AActor* K = CachedKiller.Get(); IsValid(K))
        {
            GetThirdPersonBehind(K->GetActorLocation(), K->GetActorRotation(),
                OutLoc, OutRot, Radius);
            return true;
        }
        return EstimateKillerTransformFromSub(OutLoc, OutRot);
    }

    case EDeathCamMode::KillerPOV:
    {
        if (ATorpedoPawn* T = Cast<ATorpedoPawn>(CachedKiller.Get()); IsValid(T))
        {
            if (UCameraComponent* Cam = T->GetPOVCamera())
            {
                OutLoc = Cam->GetComponentLocation();
                OutRot = Cam->GetComponentRotation();
            }
            else
            {
                OutLoc = T->GetActorLocation();
                OutRot = T->GetActorRotation();
            }
            return true;
        }

        if (ASubmarinePawn* S = Cast<ASubmarinePawn>(CachedKiller.Get()); IsValid(S))
        {
            TArray<UCameraComponent*> Cams;
            S->GetComponents<UCameraComponent>(Cams);
            for (UCameraComponent* C : Cams)
            {
                if (C && C->IsActive())
                {
                    OutLoc = C->GetComponentLocation();
                    OutRot = C->GetComponentRotation();
                    return true;
                }
            }
            OutLoc = S->GetActorLocation();
            OutRot = S->GetActorRotation();
            return true;
        }

        return EstimateKillerTransformFromSub(OutLoc, OutRot);
    }

    case EDeathCamMode::StaticBehindDead:
    {
        if (ASubmarinePawn* D = CachedDeadSub.Get())
        {
            GetThirdPersonBehind(D->GetActorLocation(), D->GetActorRotation(),
                OutLoc, OutRot, Radius);
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
bool UDeathSequenceComponent::EstimateKillerTransformFromSub(FVector& OutLoc,
FRotator& OutRot) const
{
    ASubmarinePawn* Sub = KillerSubmarine.Get();
    if (!IsValid(Sub)) return false;

    const float Radius = GetSettings() ? GetSettings()->DeathCamThirdPersonRadius : 800.f;

    // Get the muzzle offset from the sub's characteristics
    FVector MuzzleOffset = FVector(300.f, 0.f, 0.f); // safe fallback
    if (Sub->Characteristics)
        MuzzleOffset = Sub->Characteristics->TorpedoSpawnOffset;

    const FVector  EstLoc = Sub->GetActorLocation()
        + Sub->GetActorTransform().TransformVector(MuzzleOffset);
    const FRotator EstRot = Sub->GetActorRotation();

    const FVector  EstimatedLocation = Sub->GetActorLocation()
        + Sub->GetActorTransform().TransformVector(MuzzleOffset);
    const FRotator EstimatedRotation = Sub->GetActorRotation();

    switch (ActiveDeathCamMode)
    {
    case EDeathCamMode::KillerThirdPerson:
        GetThirdPersonBehind(EstLoc, EstRot, OutLoc, OutRot, Radius);
        return true;

    case EDeathCamMode::KillerPOV:
        // POV estimate: look forward from the muzzle point
        OutLoc = EstimatedLocation;
        OutRot = EstimatedRotation;
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
//  GetThirdPersonBehind
// ---------------------------------------------------------------------------
void UDeathSequenceComponent::GetThirdPersonBehind(const FVector& TargetLoc,
    const FRotator& TargetRot,
    FVector& OutCamLoc, FRotator& OutCamRot,
    float Radius)
{
    // Place camera directly behind the target (180 degrees from forward)
    const FVector Backward = TargetRot.Vector() * -1.f;
    // Slight upward offset so we're not at the exact same Z
    const FVector Up = FVector(0.f, 0.f, 1.f);
    const FVector Offset = (Backward * Radius) + (Up * Radius * 0.25f);

    OutCamLoc = TargetLoc + Offset;
    OutCamRot = (TargetLoc - OutCamLoc).Rotation();
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