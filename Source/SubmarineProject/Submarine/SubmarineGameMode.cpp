// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineGameMode.h"
#include "SubmarinePawn.h"
#include "SubmarineCollisionComponent.h"
#include "SubmarineSpectatorPawn.h"
#include "TorpedoPawn.h"
#include "ReplayRecorderComponent.h"
#include "ReplayPlaybackComponent.h"
#include "ReplayData.h"
#include "ScreenFadeComponent.h"
#include "CameraBlendSettings.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

ASubmarineGameMode::ASubmarineGameMode()
{
    DefaultPawnClass = ASubmarinePawn::StaticClass();

    ReplayRecorder = CreateDefaultSubobject<UReplayRecorderComponent>(TEXT("ReplayRecorder"));
    ReplayPlayback = CreateDefaultSubobject<UReplayPlaybackComponent>(TEXT("ReplayPlayback"));
    DeathSequence = CreateDefaultSubobject<UDeathSequenceComponent>(TEXT("DeathSequence"));
    ScreenFade = CreateDefaultSubobject<UScreenFadeComponent>(TEXT("ScreenFade"));
}

// ---------------------------------------------------------------------------
//  BeginPlay — wire delegates and propagate settings
// ---------------------------------------------------------------------------
void ASubmarineGameMode::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("[GameMode] BeginPlay START"));

    // Forward settings to components
    if (ReplayRecorder)
    {
        ReplayRecorder->Settings = ReplaySettings;
        UE_LOG(LogTemp, Log, TEXT("[GameMode] ReplayRecorder settings: %s"),
            ReplaySettings ? *ReplaySettings->GetName() : TEXT("NONE - using CDO defaults"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] ReplayRecorder component is NULL!"));
    }


    if (ReplayPlayback)
    {
        ReplayPlayback->Settings = ReplaySettings;
        UE_LOG(LogTemp, Log, TEXT("[GameMode] ReplayPlayback settings: %s"),
            ReplaySettings ? *ReplaySettings->GetName() : TEXT("NONE - using CDO defaults"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] ReplayPlayback component is NULL!"));
    }

    if (DeathSequence)
    {
        DeathSequence->ReplaySettings = ReplaySettings;

        // Bind the completion delegate — clear first to avoid double-binding
        // if BeginPlay is called more than once (e.g. PIE restart)
        DeathSequence->OnDeathSequenceComplete.RemoveDynamic(
            this, &ASubmarineGameMode::OnDeathSequenceComplete);
        DeathSequence->OnDeathSequenceComplete.AddDynamic(
            this, &ASubmarineGameMode::OnDeathSequenceComplete);

        UE_LOG(LogTemp, Log, TEXT("[GameMode] DeathSequence delegate bound OK"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] DeathSequence component is NULL!"));
    }

    // Gameplay fade-in (from black when the match starts)
    const UReplaySettings* RS = ReplaySettings
        ? ReplaySettings.Get() : GetDefault<UReplaySettings>();

    if (ScreenFade && RS) {
        ScreenFade->InitFadeLibrary(RS->FadeLibrary);
    }

    // Gameplay fade-in: screen starts black, fades to clear when match begins
    if (ScreenFade && ScreenFade->HasFadeEntry("Gameplay"))
    {
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            if (APlayerController* PC = Cast<APlayerController>(It->Get()))
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[GameMode] BeginPlay GameplayFadeIn for PC='%s'"), *PC->GetName());
                ScreenFade->PlayFadeIn(PC, "Gameplay");
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[GameMode] BeginPlay complete. Recording: %s"),
        IsRecording() ? TEXT("YES") : TEXT("NO"));
}

// ---------------------------------------------------------------------------
//  OnSubmarineDied
// 
// //  Timeline:
//    t=0     : FreezeOnDeath (hide sub, deactivate cameras, unpossess)
//    t=0     : GameplayFade fade-out begins (cosmetic, fades to black)
//    t=0..Delay : Recording continues, explosion VFX captured
//    t=Delay : OnPostDeathRecordingComplete -> extract slice -> BeginDeathSequence
//              DeathSequenceComponent schedules its own DeathReplayFade independently
// ---------------------------------------------------------------------------
void ASubmarineGameMode::OnSubmarineDied(ASubmarinePawn* DeadSubmarine,
    AController* DeadController,
    AActor* Killer)
{
    UE_LOG(LogTemp, Log, TEXT("[GameMode] OnSubmarineDied called: Sub='%s' Controller='%s' Killer='%s'"),
        DeadSubmarine ? *DeadSubmarine->GetName() : TEXT("NULL"),
        DeadController ? *DeadController->GetName() : TEXT("NULL"),
        Killer ? *Killer->GetName() : TEXT("NULL"));

    if (!DeadSubmarine || !DeadController || !GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] OnSubmarineDied — invalid params, aborting"));
        return;
    }

    // Ensure DeathSequence delegate is bound even if BeginPlay was missed
    if (DeathSequence &&
        !DeathSequence->OnDeathSequenceComplete.IsBound())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[GameMode] OnDeathSequenceComplete was not bound — binding now (BeginPlay may have been skipped)"));
        DeathSequence->OnDeathSequenceComplete.AddDynamic(
            this, &ASubmarineGameMode::OnDeathSequenceComplete);
    }

    // Cache for use after the timer fires
    PendingDeadSub = DeadSubmarine;
    PendingDeadController = DeadController;

    // ------------------------------------------------------------------
    //  Torpedoes destroy themselves during or shortly after the hit frame.
    //  By the time OnPostDeathRecordingComplete fires (DeathPreviewDelay
    //  seconds later), the torpedo actor will be gone. We store everything
    //  we need right now.
    // ------------------------------------------------------------------
    PendingKillerInfo.Clear();

    if (Killer)
    {
        PendingKillerInfo.ActorName = Killer->GetName();
        PendingKillerInfo.ActorGuid = Killer->GetActorInstanceGuid();
        PendingKillerInfo.TorpedoActor = Killer;

        if (ATorpedoPawn* T = Cast<ATorpedoPawn>(Killer))
        {
            PendingKillerInfo.bWasTorpedo = true;
            PendingKillerInfo.KillerSubmarine =
                Cast<ASubmarinePawn>(T->FiringShooter.Get());

            UE_LOG(LogTemp, Log,
                TEXT("[GameMode] Killer=torpedo '%s', firedBy='%s'"),
                *Killer->GetName(),
                PendingKillerInfo.KillerSubmarine.IsValid()
                ? *PendingKillerInfo.KillerSubmarine->GetName()
                : TEXT("unknown"));
        }
        else
        {
            PendingKillerInfo.bWasTorpedo = false;
            PendingKillerInfo.KillerSubmarine = Cast<ASubmarinePawn>(Killer);

            UE_LOG(LogTemp, Log,
                TEXT("[GameMode] Killer='%s' (submarine or environmental)"),
                *Killer->GetName());
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[GameMode] No killer — environmental death"));
    }

    const UReplaySettings* RS = ReplaySettings
        ? ReplaySettings.Get() : GetDefault<UReplaySettings>();

    
    // Freeze the submarine immediately (hides mesh, disables input,
    // unpossesses controller so death cam can take over the view).
    DeadSubmarine->FreezeOnDeath();
    
    // ------------------------------------------------------------------
    //  Step 3: Schedule GameplayFade OUT
    //
    //  The fade should START at (DeathPreviewDelay - FadeOutDuration)
    //  so it FINISHES exactly when the replay begins at DeathPreviewDelay.
    //
    //  If FadeOutDuration >= DeathPreviewDelay, fire immediately (t=0).
    // ------------------------------------------------------------------
    APlayerController* DeadPC = Cast<APlayerController>(DeadController);
    const float Delay = RS->DeathPreviewDelay;

    if (ScreenFade && DeadPC && ScreenFade->HasFadeEntry("Gameplay") && Delay > 0.f)
    {
        const FScreenFadeSettings GameplayFade = ScreenFade->GetFadeEntry("Gameplay");

        if (GameplayFade.bFadeOut || GameplayFade.bBlackScreenOut)
        {
            FScreenFadeSettings SafeFade = SafeTransitionTransform(GameplayFade, Delay, /*TransitionIn*/false);
            const float FadeTransition = SafeFade.bFadeOut ? SafeFade.FadeOutDuration : 0.f;
            const float BlackScreenTransition = SafeFade.bBlackScreenOut ? SafeFade.BlackScreenOutDuration : 0.f;
            const float TransitionOutDuration = FadeTransition + BlackScreenTransition;
            const float FadeStartTime = Delay - TransitionOutDuration;

            UE_LOG(LogTemp, Log,
                TEXT("[GameMode] GameplayFade OUT (%.2fs) scheduled in %.2fs"),
                GameplayFade.FadeOutDuration, FadeStartTime);

            TWeakObjectPtr<UScreenFadeComponent> WeakFade(ScreenFade);
            TWeakObjectPtr<APlayerController>    WeakPC(DeadPC);

            if (FadeStartTime <= 0.f)
            {
                ScreenFade->FadeOut(DeadPC, SafeFade);
            }
            else
            {
                FTimerHandle FadeTimer;
                GetWorld()->GetTimerManager().SetTimer(FadeTimer,
                    FTimerDelegate::CreateLambda([WeakFade, WeakPC, SafeFade]()
                        {
                            if (WeakFade.IsValid() && WeakPC.IsValid())
                                WeakFade->FadeOut(WeakPC.Get(), SafeFade);
                        }),
                    FadeStartTime, false);
            }
        }
    }

    // ------------------------------------------------------------------
    //  Step 4: Post-death recording timer
    // ------------------------------------------------------------------
    if (Delay > 0.f)
    {
        GetWorld()->GetTimerManager().SetTimer(
            PostDeathRecordingTimer,
            this,
            &ASubmarineGameMode::OnPostDeathRecordingComplete,
            Delay,
            false);

        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] PostDeathRecording timer set for %.2fs"), Delay);
    }
    else
    {
        OnPostDeathRecordingComplete();
    }
}

// ---------------------------------------------------------------------------
//  OnPostDeathRecordingComplete
//  Called DeathPreviewDelay seconds after the submarine died.
//  The recording now includes the explosion VFX frame(s).
// ---------------------------------------------------------------------------
void ASubmarineGameMode::OnPostDeathRecordingComplete()
{
    ASubmarinePawn* DeadSub = PendingDeadSub.Get();
    AController* DC = PendingDeadController.Get();

    if (!DC || !GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] OnPostDeathRecordingComplete — controller gone"));
        return;
    }

    const UReplaySettings* RS = ReplaySettings
        ? ReplaySettings.Get() : GetDefault<UReplaySettings>();

    // ------------------------------------------------------------------
    //  Resolve killer for death cam.
    //  If it was a torpedo: torpedo is gone — use the firing submarine.
    //  If it was a submarine: use that submarine directly.
    //  Environmental: nullptr (StaticBehindDead mode will be used).
    // ------------------------------------------------------------------
    AActor* KillerForDeathSeq = nullptr;

    if (PendingKillerInfo.bWasTorpedo)
    {
        KillerForDeathSeq = PendingKillerInfo.KillerSubmarine.IsValid()
            ? PendingKillerInfo.KillerSubmarine.Get() : nullptr;

        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Torpedo '%s' gone — using firing sub '%s' for death cam"),
            *PendingKillerInfo.ActorName,
            KillerForDeathSeq ? *KillerForDeathSeq->GetName() : TEXT("also gone"));
    }
    else if (PendingKillerInfo.KillerSubmarine.IsValid())
    {
        KillerForDeathSeq = PendingKillerInfo.KillerSubmarine.Get();
    }
    // else: environmental kill, KillerForDeathSeq stays nullptr

    UE_LOG(LogTemp, Log,
        TEXT("[GameMode] KillerForDeathSeq='%s'"),
        KillerForDeathSeq ? *KillerForDeathSeq->GetName() : TEXT("None (environmental)"));

    // -----------------------------------------------------------------------
    //  Extract replay slice
    //
    //  Effective content after death = min(DeathReplayDuration,
    //                                      DeathReplayLeadIn + DeathPreviewDelay)
    //  Slice start = Now - EffectiveContent - DeathPreviewDelay
    //              = Now - min(DeathReplayDuration, DeathReplayLeadIn + DeathPreviewDelay)
    //                    - DeathPreviewDelay
    //
    //  We are now exactly DeathPreviewDelay seconds past the death moment, so:
    //    "Now" in recording time = DeathTime + DeathPreviewDelay
    //    Slice end   = Now (captures the explosion)
    //    Slice start = Now - EffectiveContent - DeathPreviewDelay
    // -----------------------------------------------------------------------
    UReplayData* Slice = nullptr;

    if (ReplayRecorder && RS->DeathReplayDuration > 0.f)
    {
        const float Now = GetWorld()->GetTimeSeconds();
        const float EffectiveContent = RS->GetEffectivePostDeathContent();
        const float SliceStart = Now - EffectiveContent;
        const float SliceEnd = Now;

        Slice = ReplayRecorder->ExtractSlice(SliceStart, SliceEnd);

        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Slice extracted: %.1fs  %d frames  (effective=%.1fs speed=%.1fx)"),
            Slice ? Slice->GetDuration() : 0.f,
            Slice ? Slice->TickFrames.Num() : 0,
            EffectiveContent,
            RS->PlaybackSpeed);
        const float DeathTime = Now - RS->DeathPreviewDelay;
        const float DeathOffsetInSlice = DeathTime - SliceStart;
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Slice: start=%.2f end=%.2f dur=%.1fs frames=%d"),
            SliceStart, SliceEnd,
            Slice ? Slice->GetDuration() : 0.f,
            Slice ? Slice->TickFrames.Num() : 0);
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Death at t=%.2f = %.2fs into slice (should show explosion in last %.2fs)"),
            DeathTime, DeathOffsetInSlice, RS->DeathPreviewDelay);
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] WallClock will be corrected in DeathSeq: slice_dur=%.2f previewDelay=%.2f"),
            Slice ? Slice->GetDuration() : 0.f, RS->DeathPreviewDelay);
    }

    // -----------------------------------------------------------------------
    //  Start death sequence
    //  Pass PlaybackSpeed so ghost movement matches the configured replay speed.
    // -----------------------------------------------------------------------

    // Pre-populate torpedo name BEFORE BeginDeathSequence.
    // By this point the torpedo is already destroyed, so Cast<ATorpedoPawn>(Killer)
    // inside BeginDeathSequence always fails. We set the name here while we still
    // have it cached in PendingKillerInfo.
    if (ReplayPlayback && PendingKillerInfo.bWasTorpedo)
    {
        ReplayPlayback->KillerTorpedoGuid = PendingKillerInfo.ActorGuid;
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Pre-set KillerTorpedoActorName='%s' on ReplayPlayback"),
            *PendingKillerInfo.ActorGuid.ToString());
    }


    if (DeathSequence)
    {
        // DeathSequenceComponent handles DeathReplayFade internally via ScheduleFades.
        // Pass ScreenFade so it can schedule the fade-in/out on the dead PC.
        DeathSequence->BeginDeathSequence(
            DeadSub, DC,
            KillerForDeathSeq,
            Slice,
            (Slice && ReplayPlayback) ? ReplayPlayback.Get() : nullptr,
            RS->PlaybackSpeed,
            ScreenFade.Get());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] No DeathSequence — skipping to spectator"));
        OnDeathSequenceComplete();
    }
}

// ---------------------------------------------------------------------------
//  OnDeathSequenceComplete  (called when delay + death cam are done)
// 
// //  At this point:
//  - DeathSequenceComponent has already called StopPlayback (ghosts destroyed)
//  - DeathSequenceComponent has already run its fade-out (if configured)
//  - Screen may be black from the DeathReplayFade fade-out
//
//  We spawn the spectator and do a SpectatorFade fade-in.
//  We do NOT do another fade-out here — that would double-fade.
// ---------------------------------------------------------------------------
void ASubmarineGameMode::OnDeathSequenceComplete()
{
    UE_LOG(LogTemp, Log, TEXT("[GameMode] OnDeathSequenceComplete fired"));

    ASubmarinePawn* DeadSub = PendingDeadSub.Get();
    AController* DC = PendingDeadController.Get();

    if (!DC || !GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] OnDeathSequenceComplete — controller invalid, skipping spectator spawn"));
        return;
    }

    if (!SpectatorPawnClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] SpectatorPawnClass not set!"));
        return;
    }

    const UReplaySettings* RS = ReplaySettings
        ? ReplaySettings.Get() : GetDefault<UReplaySettings>();

    APlayerController* PC = Cast<APlayerController>(DC);

    // Spawn spectator immediately — no extra fade-out here.
    // The DeathReplayFade already faded to black at the end of the replay.
    // SpectatorFade will fade-in from that black state.
    TArray<ASubmarinePawn*> AllSubs;
    for (TActorIterator<ASubmarinePawn> It(GetWorld()); It; ++It)
        if (IsValid(*It) && *It != DeadSub)
            AllSubs.Add(*It);

    const FTransform SpawnTransform = DeadSub
        ? DeadSub->GetActorTransform() : FTransform::Identity;

    FActorSpawnParameters Params;
    Params.Owner = DC;

    ASubmarineSpectatorPawn* Spectator = GetWorld()->SpawnActor<ASubmarineSpectatorPawn>(
        SpectatorPawnClass, SpawnTransform, Params);

    if (!Spectator)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] Failed to spawn SpectatorPawn!"));
        return;
    }

    DC->Possess(Spectator);
    Spectator->InitSpectator(AllSubs, true);
    if (CameraBlendSettings) Spectator->BlendSettings = CameraBlendSettings;

     //-- Spectator fade-in --------------------------------------------------

    if (ScreenFade && ScreenFade->HasFadeEntry("Spectator"))
    {
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            if (DC)
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[GameMode] SpectatorFade IN for PC='%s'"), *PC->GetName());
                ScreenFade->PlayFadeIn(PC, "Spectator");
            }
        }
    }

    if (IsValid(DeadSub)) DeadSub->Destroy();

    PendingDeadSub = nullptr;
    PendingDeadController = nullptr;

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Spectator spawned, SpectatorFade-in triggered"));
}

// ---------------------------------------------------------------------------
//  Replay forwarding
// ---------------------------------------------------------------------------
void ASubmarineGameMode::StartRecording()
{
    if (ReplayRecorder) ReplayRecorder->StartRecording();
}

void ASubmarineGameMode::StopRecording()
{
    if (!ReplayRecorder) return;

    ReplayRecorder->StopRecording();

    // bAutoSaveOnStop: save the replay buffer to disk when recording stops.
    // File appears at: <ProjectDir>/Saved/SaveGames/<ReplaySaveSlot>.sav
    const UReplaySettings* S = ReplaySettings
        ? ReplaySettings.Get()
        : GetDefault<UReplaySettings>();

    if (S && S->bAutoSaveOnStop)
    {
        const bool bOk = ReplayRecorder->SaveReplay();
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Auto-save on stop: %s"), bOk ? TEXT("OK") : TEXT("FAILED"));
    }
}

bool ASubmarineGameMode::SaveReplay(const FString& Label)
{
    return ReplayRecorder ? ReplayRecorder->SaveReplay(Label) : false;
}

bool ASubmarineGameMode::LoadReplay()
{
    return ReplayRecorder ? ReplayRecorder->LoadReplay() : false;
}

bool ASubmarineGameMode::IsRecording() const
{
    return ReplayRecorder ? ReplayRecorder->IsRecording() : false;
}